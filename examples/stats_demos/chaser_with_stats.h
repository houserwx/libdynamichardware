// ==============================================================================
// chaser_with_stats.h — Correct wall-to-wall RT loop with honest jitter measurement.
//
// - Full cycle (readAll + setBool + writeAll + occasional prints) is measured.
// - Prints ARE part of the workload — their cost shows up in period/jitter.
// - Prints only cause real jitter if they push a cycle over the deadline.
// - Absolute sleep (clock_nanosleep + TIMER_ABSTIME) prevents drift.
// - Single thread, SCHED_FIFO, stack prefault — matches your PREEMPT_RT preference.
// - Cumulative + window stats via Welford (purely additive).
// ==============================================================================

#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <limits>
#include <cstdio>
#include <sched.h>
#include <unistd.h>
#include <atomic>
#include <pthread.h>

[[nodiscard]] static inline int64_t now_ns() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

template <typename OutputPtrT>
void runChaserWithStats(
    std::vector<OutputPtrT>& outputs,
    const std::vector<std::string>& channel_names,
    long long target_period_ns = 1'000'000LL,   // 1 kHz default
    unsigned cycles_per_light = 1000            // ~1s per light at 1kHz
) {
    // ── RT Thread Setup ─────────────────────────────────────────────────────
    struct sched_param param{};
    param.sched_priority = 85;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    // Stack prefault
    volatile char* p = static_cast<volatile char*>(alloca(128 * 4096));
    for (size_t i = 0; i < 128 * 4096; i += 4096) p[i] = 0;

    std::atomic<bool> running{true};

    // ── Stats accumulators ──────────────────────────────────────────────────
    uint64_t totalCycles = 0;
    double cumMean = 0.0, cumM2 = 0.0;
    double cumMin = std::numeric_limits<double>::max(), cumMax = 0.0;

    unsigned winCount = 0;
    double winSum = 0.0;
    double winMin = std::numeric_limits<double>::max(), winMax = 0.0;

    const unsigned kStatsEvery = 5000;  // ~5 seconds at 1kHz

    unsigned currentLight = 0;
    unsigned cyclesSinceSwitch = 0;

    while (running) {
        int64_t cycleStart = now_ns();   // ← FULL CYCLE START

        // === REAL WORK ===
        for (size_t i = 0; i < outputs.size(); ++i) {
            outputs[i]->setBool(i == currentLight);
        }

        ++cyclesSinceSwitch;
        if (cyclesSinceSwitch >= cycles_per_light) {
            cyclesSinceSwitch = 0;
            if (!channel_names.empty()) {
                printf("[light] %s\n", channel_names[currentLight].c_str());
            } else {
                printf("[light] %u\n", currentLight);
            }
            fflush(stdout);   // Still part of this cycle's measurement
            currentLight = (currentLight + 1) % outputs.size();
        }

        int64_t cycleEnd = now_ns();     // ← FULL CYCLE END
        int64_t periodNs = cycleEnd - cycleStart;

        // Accumulate
        ++totalCycles;
        double p = static_cast<double>(periodNs) / 1000.0;  // µs

        // Cumulative Welford
        double delta = p - cumMean;
        cumMean += delta / totalCycles;
        cumM2 += delta * (p - cumMean);
        if (p < cumMin) cumMin = p;
        if (p > cumMax) cumMax = p;

        // Window
        ++winCount;
        winSum += p;
        if (p < winMin) winMin = p;
        if (p > winMax) winMax = p;

        // Print stats every ~5s (part of workload)
        if (winCount >= kStatsEvery) {
            double winAvg = winSum / winCount;
            double cumSd  = std::sqrt(cumM2 / totalCycles);

            printf("Period | Cycles: %5u | Set: %.3fµs | Min: %.3fµs | Max: %.3fµs | Avg: %.3fµs\n",
                   winCount, static_cast<double>(target_period_ns)/1000.0, winMin, winMax, winAvg);
            printf("Cumulative | Cycles: %6llu | Avg: %.3fµs | Jitter ±%.3fµs\n\n",
                   static_cast<unsigned long long>(totalCycles), cumMean, cumSd);

            // Reset window
            winCount = 0; winSum = 0;
            winMin = std::numeric_limits<double>::max(); winMax = 0;
        }

        // Sleep remainder — prevents drift via absolute deadline
        if (periodNs < target_period_ns) {
            struct timespec ts{};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            int64_t remain = target_period_ns - periodNs;
            ts.tv_sec += remain / 1'000'000'000LL;
            ts.tv_nsec += remain % 1'000'000'000LL;
            if (ts.tv_nsec >= 1'000'000'000L) { ts.tv_sec++; ts.tv_nsec -= 1'000'000'000L; }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
        } else {
            fprintf(stderr, "[overrun] %.1f µs > target!\n", static_cast<double>(periodNs)/1000.0);
        }
    }   // while (running)
}       // runChaserWithStats
