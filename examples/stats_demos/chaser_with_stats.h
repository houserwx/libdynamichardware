// ============================================================================
// chaser_with_stats.h — Walking-light RT loop + cumulative stats.
// 
// Architecture (clean separation):
//   Hot-path thread → walks lights, pushes period samples & channel switches to SPSC rings.
//   Main thread     → drains rings, accumulates Welford's stats, prints every second.
// 
// Stats are CUMULATIVE — count grows monotonically from start to shutdown.
// All accumulation is additive via Welford's online algorithm (no subtraction).
// ============================================================================

#pragma once

#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "spsc_ring.h"

// ---------------------------------------------------------------------------
// addNsToTs / diffNs helpers — pure integer arithmetic, no kernel calls.
// Matches CIVControl-ARM Application.cpp pattern for absolute-deadline timing.
// ---------------------------------------------------------------------------
[[nodiscard]] static inline struct timespec addNsToTs(struct timespec ts, int64_t ns) noexcept {
    ts.tv_nsec += static_cast<long>(ns);
    while (ts.tv_nsec >= 1'000'000'000L) { ts.tv_sec++; ts.tv_nsec -= 1'000'000'000L; }
    while (ts.tv_nsec < 0L)              { ts.tv_sec--; ts.tv_nsec += 1'000'000'000L; }
    return ts;
}

[[nodiscard]] static inline int64_t diffNs(const struct timespec& a, const struct timespec& b) noexcept {
    return ((a.tv_sec - b.tv_sec) * 1'000'000'000LL)
         + static_cast<int64_t>(a.tv_nsec - b.tv_nsec);
}

/// Wall-clock timestamp helper — CLOCK_MONOTONIC → int64_t nanoseconds.
static inline int64_t now_ns() noexcept {
    struct timespec ts{}; clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

template <typename OutputPtrT>
void runChaserWithStats(
    std::vector<OutputPtrT>& outputs,           // Live DHDOEntry* pointers (post-freeze)
    const std::vector<std::string>& channel_names, // Display names for [light] events
    long long target_period_ns,                 // e.g., 1'000'000 for 1ms cycle
    unsigned cycles_per_light                   // RT cycles per light step = walk delay / cycle period
) {
    // ── SPSC rings: hot path → main thread ────────────────────────────────
    // Timing ring holds one sample per cycle → must survive a full stats window without dropping.
    // Capacity = 16384 (power-of-two ≥ 10k) gives ~16s headroom at 1kHz push rate.
    // Event ring is sparse — one notification per light step (~every 1 second).
    SpscRing<int64_t, 16384> timing_ring;   // ≥10k cycles capacity at power-of-two boundary
    SpscRing<unsigned, 256>  event_ring;     // Sparse — one notification per light step (~1/s)

   // ── Cumulative Welford's accumulators (written only by main thread) ────
    unsigned cum_count       = 0;
    double   cum_welford_m   = 0.0;         // Running mean — additive updates only
    double   cum_welford_m2  = 0.0;         // Sum of squared deviations — purely additive
    double   cum_min_p       = std::numeric_limits<double>::max();
    double   cum_max_p       = 0.0;

    // Cumulative jitter: abs(delta between consecutive periods)
    int64_t  cum_prev_sample_ns{0};
    double   cum_jitter_sum   = 0.0;        // Sum of all inter-period deltas (µs)
    double   cum_jitter_min   = std::numeric_limits<double>::max();
    double   cum_jitter_max   = 0.0;

    // ── Period-window accumulators (reset every print cycle) ───────────────
    unsigned win_count        = 0;
    double   win_sum          = 0.0;        // Sum of period values in µs
    double   win_min_p        = std::numeric_limits<double>::max();
    double   win_max_p        = 0.0;
    int64_t  win_prev_sample_ns{0};
    double   win_jitter_sum   = 0.0;        // Sum of jitter deltas this window (µs)
    unsigned win_jitter_count = 0;
    double   win_jitter_min   = std::numeric_limits<double>::max();
    double   win_jitter_max   = 0.0;

    constexpr unsigned kWarmupSamples = 5000; ///< Discard first N collected samples to let caches warm + scheduler settle
    unsigned warmupRemaining = kWarmupSamples; ///< Tracked by MAIN thread only — hot path is oblivious
    const double       kTargetPeriodUs = static_cast<double>(target_period_ns) / 1000.0;
    bool warmupDoneLogged = false;

    std::atomic<bool> running{true};
    auto last_stats_print = std::chrono::steady_clock::now();

    // ── Spawn hot-path worker thread ───────────────────────────────────────
    std::thread chaserThread([&]() noexcept {
        // --- Configure RT scheduling (matches CIVControl-ARM Threadrunner.cpp) ---
        struct sched_param param{};
        param.sched_priority = 85;  // High priority, same as Application.cpp

        int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
        if (rc != 0) {
            perror("[HotPath] WARNING: failed to set SCHED_FIFO");
        } else {
            printf("[info] Hot path thread: SCHED_FIFO priority=85\n");
        }

        // Prefault stack pages — touch memory before RT loop starts (same pattern as Threadrunner.cpp)
        volatile char* stack_page = static_cast<volatile char*>(alloca(32 * 4096));
        for (std::size_t i = 0; i < 32 * 4096; i += 4096) stack_page[i] = 0;
        asm volatile("" : : "r"(stack_page) : "memory");

        unsigned current_light      = 0;
        unsigned cycles_since_switch = 0;

        // --- Absolute-deadline timing loop (matches CIVControl-ARM Application.cpp) ---
        struct timespec nextWakeup{};
        clock_gettime(CLOCK_MONOTONIC, &nextWakeup);
        nextWakeup = addNsToTs(nextWakeup, 100'000LL);  // Small offset for first cycle

        int64_t prevArrivalNs{0};  // Arrival timestamp of previous cycle (for wall-period measurement)

        while (running.load(std::memory_order_relaxed)) {
            // Sleep until absolute deadline — no drift accumulation from relative sleeps
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &nextWakeup, nullptr);

            // Single measurement point: capture arrival time immediately after wakeup.
            // This is the "signalProcessTickNow" equivalent — cached once/cycle.
            struct timespec now{};
            clock_gettime(CLOCK_MONOTONIC, &now);
            int64_t arrivalNs = static_cast<int64_t>(now.tv_sec) * 1'000'000'000LL + now.tv_nsec;

            // Advance deadline for NEXT cycle BEFORE RT work so we don't steal compute budget
            nextWakeup = addNsToTs(nextWakeup, static_cast<int64_t>(target_period_ns));

            // --- RT work phase (matches Application.cpp: readAll → rtCycle → writeAll) ---
            for (size_t i = 0; i < outputs.size(); ++i)
                outputs[i]->setBool(i == current_light);

            // Compute wall period between consecutive arrivals and push to ring.
            // Producer pushes EVERY sample — main thread handles warmup discard.
            if (prevArrivalNs != 0) {
                timing_ring.push(arrivalNs - prevArrivalNs);   // Wall-to-wall period in ns
            }
            prevArrivalNs = arrivalNs;

            ++cycles_since_switch;
            if (cycles_since_switch >= cycles_per_light) {
                cycles_since_switch = 0;
                event_ring.push(current_light);// Notify main thread of channel switch.
                current_light = (current_light + 1) % outputs.size();
            }
        }
    });

    // ── Print helper: Period + Cumulative lines ────────────────────────────
    auto printStats = [&]() {
        double win_avg_us   = (win_count > 0) ? (win_sum / static_cast<double>(win_count)) : 0.0;
        double cum_avg_us   = cum_welford_m / 1000.0;                        // ns → µs
        double cum_sd_us    = std::sqrt(cum_welford_m2 / static_cast<double>(cum_count)) / 1000.0;

        printf("\n");
        printf("Period       | Cycles: %5u | Set: %.3fµs | Min: %.3fµs | Max: %.3fµs | Avg: %.3fµs\n",
               win_count, kTargetPeriodUs, win_min_p, win_max_p, win_avg_us);

        if (win_jitter_count > 0) {
            double win_jit_avg = win_jitter_sum / static_cast<double>(win_jitter_count);
            printf("             Jitter:                          Min: %.3fµs | Max: %.3fµs | Avg: %.3fµs\n",
                   win_jitter_min, win_jitter_max, win_jit_avg);
        } else {
            printf("             Jitter:                          —\n");
        }

        printf("Cumulative   | Cycles: %5u | Set: %.3fµs | Min: %.3fµs | Max: %.3fµs | Avg: %.3fµs\n",
               cum_count, kTargetPeriodUs, cum_min_p, cum_max_p, cum_avg_us);

        if (cum_prev_sample_ns != 0) {
            double cum_jit_avg = cum_jitter_sum / static_cast<double>(cum_count - 1); // N-1 deltas for N samples
            printf("             Jitter:                          Min: %.3fµs | Max: %.3fµs | Avg: %.3fµs ±%.3fµs\n",
                   cum_jitter_min, cum_jitter_max, cum_jit_avg, cum_sd_us);
        } else {
            printf("             Jitter:                          —\n");
        }
    };

    // ── Main thread: drain rings + print every second ──────────────────────
    try {
        while (true) {
            int64_t sample{};
            while (timing_ring.pop(sample)) {
                double p_us = static_cast<double>(sample) / 1000.0;   // ns → µs

                // --- Warmup filter: discard first N samples on MAIN thread only ---
                if (warmupRemaining > 0) {
                    --warmupRemaining;
                    if (warmupRemaining == 0 && !warmupDoneLogged) {
                        printf("[info] Warmup complete (%u cycles discarded) — starting metrics\n", kWarmupSamples);
                        warmupDoneLogged = true;
                    }
                    continue;   // Skip stats accumulation for this sample
                }
                if (p_us < win_min_p) win_min_p = p_us;
                if (p_us > win_max_p) win_max_p = p_us;

                // Period jitter: abs(delta between consecutive periods) in µs
                if (win_prev_sample_ns != 0) {
                    double jit_us = std::abs(static_cast<double>(sample - win_prev_sample_ns) / 1000.0);
                    win_jitter_sum += jit_us;
                    ++win_jitter_count;
                    if (jit_us < win_jitter_min) win_jitter_min = jit_us;
                    if (jit_us > win_jitter_max) win_jitter_max = jit_us;
                }
                win_prev_sample_ns = sample;

                // --- Cumulative Welford's accumulators (ns domain for precision) ---
                ++cum_count;
                double p     = static_cast<double>(sample);
                double delta = p - cum_welford_m;
                cum_welford_m += delta / static_cast<double>(cum_count);
                cum_welford_m2 += delta * (p - cum_welford_m);
                if (p_us < cum_min_p) cum_min_p = p_us;
                if (p_us > cum_max_p) cum_max_p = p_us;

                // Cumulative jitter tracking
                if (cum_prev_sample_ns != 0) {
                    double jit_us = std::abs(static_cast<double>(sample - cum_prev_sample_ns) / 1000.0);
                    cum_jitter_sum += jit_us;
                    if (jit_us < cum_jitter_min) cum_jitter_min = jit_us;
                    if (jit_us > cum_jitter_max) cum_jitter_max = jit_us;
                }
                cum_prev_sample_ns = sample;
            }

            unsigned chIdx{};
            while (event_ring.pop(chIdx)) {
                if (chIdx < channel_names.size()) {
                    printf("[light] %s\n", channel_names[chIdx].c_str());
                } else {
                    printf("[light] channel %u\n", chIdx);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            auto now = std::chrono::steady_clock::now();
            if ((now - last_stats_print) >= std::chrono::seconds(1) && win_count > 0) {
                printStats();
                fflush(stdout);

                // Reset period window for next cycle — cumulative stays intact!
                win_count        = 0;
                win_sum          = 0.0;
                win_min_p        = std::numeric_limits<double>::max();
                win_max_p        = 0.0;
                win_prev_sample_ns = 0;
                win_jitter_sum   = 0.0;
                win_jitter_count = 0;
                win_jitter_min   = std::numeric_limits<double>::max();
                win_jitter_max   = 0.0;

                last_stats_print = now;
            }
        }
    } catch (...) {
        fprintf(stderr, "[Demo] Unexpected exception — shutting down.\n");
    }

    running.store(false, std::memory_order_relaxed);
    if (chaserThread.joinable()) chaserThread.join();

    // Final dump on shutdown
    if (cum_count > 0) {
        printf("\n--- Shutdown ---\n");
        printStats();
    }
}
