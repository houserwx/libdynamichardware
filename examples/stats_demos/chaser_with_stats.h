// ==============================================================================
// chaser_with_stats.h — Single-thread PREEMPT_RT loop with deferred I/O.
//
// ONE thread at SCHED_FIFO priority 85. On PREEMPT_RT this is the correct model:
//   - No second thread → no cross-core MESI coherence traffic → low max jitter.
//   - All work (setBool, Welford's accumulation, conditional print) in one loop.
//   - I/O happens AFTER deadline advance so it doesn't steal compute budget.
//   - Printf/fflush block predictably within our own thread; nothing preempts us.
//
// Stats are CUMULATIVE via Welford's online algorithm (purely additive).
// ============================================================================

#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <sched.h>
#include <unistd.h>

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

template <typename OutputPtrT>
void runChaserWithStats(
    std::vector<OutputPtrT>& outputs,           // Live DHDOEntry* pointers (post-freeze)
    const std::vector<std::string>& channel_names, // Display names for [light] events
    long long target_period_ns,                 // e.g., 1'000'000 for 1ms cycle
    unsigned cycles_per_light                   // RT cycles per light step = walk delay / cycle period
) {
    // ── PREEMPT_RT setup: SCHED_FIFO on CURRENT thread only ────────────────
    struct sched_param param{};
    param.sched_priority = 85;

    int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    if (rc != 0) perror("[RT] WARNING: failed to set SCHED_FIFO priority=85");

    // Prefault stack pages before RT loop starts
    volatile char* stack_page = static_cast<volatile char*>(alloca(32 * 4096));
    for (std::size_t i = 0; i < 32 * 4096; i += 4096) stack_page[i] = 0;

    // ── Cumulative Welford's accumulators (purely additive — no subtraction) ────
    uint64_t cum_count{0};
    double   cum_welford_m{0.0}, cum_welford_m2{0.0};
    double   cum_min_p{std::numeric_limits<double>::max()}, cum_max_p{0.0};
    int64_t  cum_prev_sample_ns{0};
    int64_t  cum_prev_periodNs{0};      ///< Previous cycle's period for jitter delta calculation
    double   cum_jitter_sum{0}, cum_jitter_min{std::numeric_limits<double>::max()}, cum_jitter_max{0.0};

    // ── Period-window accumulators (reset every print cycle) ───────────────
    unsigned win_count{0};
    double   win_sum{0};
    double   win_min_p{std::numeric_limits<double>::max()}, win_max_p{0.0};
    int64_t  win_prev_sample_ns{0};
    double   win_jitter_sum{0};
    unsigned win_jitter_count{0};
    double   win_jitter_min{std::numeric_limits<double>::max()}, win_jitter_max{0.0};

    constexpr unsigned kWarmupSamples = 5000; ///< Discard first N samples to let caches warm + scheduler settle
    unsigned warmupRemaining = kWarmupSamples;
    const double kTargetPeriodUs = static_cast<double>(target_period_ns) / 1000.0;
    bool warmupDoneLogged = false;

    // Cycles between stat prints (~5 seconds worth at current period rate)
    const uint64_t logEvery = 5'000'000'000ULL / static_cast<uint64_t>(target_period_ns);

    // ── Absolute-deadline RT loop: ONE thread, SCHED_FIFO 85 ────────────────
    struct timespec nextWakeup{};
    clock_gettime(CLOCK_MONOTONIC, &nextWakeup);
    nextWakeup = addNsToTs(nextWakeup, 100'000LL);

    uint64_t cycleCount{0};
    unsigned current_light      = 0;
    unsigned cycles_since_switch = 0;

        while (true) {
        // Sleep until absolute deadline — no drift accumulation from relative sleeps
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &nextWakeup, nullptr);

        // Advance deadline for NEXT cycle BEFORE RT work so compute budget doesn't steal time
        nextWakeup = addNsToTs(nextWakeup, static_cast<int64_t>(target_period_ns));
        ++cycleCount;

        // Single measurement point: capture arrival immediately after wakeup (matches DemoApplication.cpp)
        struct timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);

        // ── RT work phase (setBool on each output channel) ────────────────
        for (size_t i = 0; i < outputs.size(); ++i)
            outputs[i]->setBool(i == current_light);

        // Light step tracking
        ++cycles_since_switch;
        if (cycles_since_switch >= cycles_per_light) {
            cycles_since_switch = 0;
            printf("[light] %s\n", channel_names.empty() ? "channel" : channel_names[current_light].c_str());
            fflush(stdout);
            current_light = (current_light + 1) % outputs.size();
        }

        // ── Period sample accumulation — inline arithmetic only ────────────
        if (warmupRemaining > 0) {
            --warmupRemaining;
            if (warmupRemaining == 0 && !warmupDoneLogged) {
                printf("[info] Warmup complete (%u cycles discarded) — starting metrics\n", kWarmupSamples);
                fflush(stdout);
                warmupDoneLogged = true;
            }
        } else {
            int64_t arrivalNs = static_cast<int64_t>(now.tv_sec) * 1'000'000'000LL + now.tv_nsec;

            // Compute period between consecutive arrivals (ns → µs for display)
            int64_t periodNs{0};
            if (cum_prev_sample_ns != 0) {
                periodNs = arrivalNs - cum_prev_sample_ns;
            }
            double p_us = (periodNs > 0) ? static_cast<double>(periodNs) / 1000.0 : 0.0;

            // Skip first sample after warmup (no period to measure yet)
            if (periodNs <= 0) {
                cum_prev_sample_ns = arrivalNs;
                win_prev_sample_ns = arrivalNs;
                continue;
            }

            // --- Window accumulators ---
            ++win_count;
            win_sum += p_us;
            if (p_us < win_min_p) win_min_p = p_us;
            if (p_us > win_max_p) win_max_p = p_us;

            if (win_prev_sample_ns != 0 && periodNs > 0) {
                double jit_us = std::abs(static_cast<double>(arrivalNs - win_prev_sample_ns) / 1000.0);
                win_jitter_sum += jit_us;
                ++win_jitter_count;
                if (jit_us < win_jitter_min) win_jitter_min = jit_us;
                if (jit_us > win_jitter_max) win_jitter_max = jit_us;
            }
            win_prev_sample_ns = arrivalNs;

            // --- Cumulative Welford's online algorithm (ns domain — purely additive) ---
            ++cum_count;
            double delta = static_cast<double>(periodNs) - cum_welford_m;
            cum_welford_m += delta / static_cast<double>(cum_count);
            cum_welford_m2 += delta * (static_cast<double>(periodNs) - cum_welford_m);
            if (p_us < cum_min_p) cum_min_p = p_us;
            if (p_us > cum_max_p) cum_max_p = p_us;

            // Jitter: abs(delta between consecutive periods) in µs
            if (cum_prev_periodNs != 0) {
                double jit_us = std::abs(static_cast<double>(periodNs - cum_prev_periodNs)) / 1000.0;
                cum_jitter_sum += jit_us;
                if (jit_us < cum_jitter_min) cum_jitter_min = jit_us;
                if (jit_us > cum_jitter_max) cum_jitter_max = jit_us;
            }
            cum_prev_periodNs = periodNs;
            cum_prev_sample_ns = arrivalNs;
        }

        // ── Print stats every ~5 seconds (cycle-count driven, inline on RT thread) ──
        if (logEvery > 0 && warmupDoneLogged && win_count > 0 && (cycleCount % logEvery == 0)) {
            double win_avg_us  = win_sum / static_cast<double>(win_count);
            double cum_avg_us  = cum_welford_m / 1000.0;                        // ns → µs
            double cum_sd_us   = std::sqrt(cum_welford_m2 / static_cast<double>(cum_count)) / 1000.0;

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

            printf("Cumulative   | Cycles: %6llu | Set: %.3fµs | Min: %.3fµs | Max: %.3fµs | Avg: %.3fµs\n",
                   static_cast<unsigned long long>(cum_count), kTargetPeriodUs, cum_min_p, cum_max_p, cum_avg_us);

            if (cum_count > 1) {
                int64_t jit_deltas = static_cast<int64_t>(cum_count - 1);
                double cum_jit_avg = cum_jitter_sum / static_cast<double>(jit_deltas > 0 ? jit_deltas : 1);
                printf("             Jitter:                          Min: %.3fµs | Max: %.3fµs | Avg: %.3fµs ±%.3fµs\n",
                       cum_jitter_min, cum_jitter_max, cum_jit_avg, cum_sd_us);
            } else {
                printf("             Jitter:                          —\n");
            }

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
        }
    }
} // runChaserWithStats
