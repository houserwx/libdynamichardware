// ==============================================================================
// chaser_with_stats.h — Correct full-cycle measurement (prints included)
// 
// - cycleStart at very top of loop
// - cycleEnd immediately after writeAll() + prints
// - Prints are part of the measured period
// ==============================================================================

#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cinttypes>
#include <sched.h>
#include <unistd.h>
#include <atomic>
#include <pthread.h>

static inline int64_t now_ns() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

template <typename OutputPtrT>
void runChaserWithStats(
    std::vector<OutputPtrT>& outputs,
    const std::vector<std::string>& channel_names,
    long long target_period_ns = 1'000'000LL,
    unsigned cycles_per_light = 1000)
{
    // ── RT Thread Setup ─────────────────────────────────────────────────────
    struct sched_param param{};
    param.sched_priority = 85;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    volatile char* stack_page = static_cast<volatile char*>(alloca(32 * 4096));
    for (size_t i = 0; i < 32 * 4096; i += 4096) stack_page[i] = 0;

    uint64_t totalCycles = 0;
    double cumMean = 0.0, cumM2 = 0.0;
    double cumMin = std::numeric_limits<double>::max(), cumMax = 0.0;

    unsigned winCount = 0;
    double winSum = 0.0;
    double winMin = std::numeric_limits<double>::max(), winMax = 0.0;

    unsigned currentLight = 0;
    unsigned cyclesSinceSwitch = 0;

    while (true) {
        int64_t cycleStart = now_ns();   // ← Core cycle start

        // === CORE RT WORK ONLY ===
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
            fflush(stdout);
            currentLight = (currentLight + 1) % outputs.size();
        }

        int64_t cycleEnd = now_ns();     // ← Core cycle end (before stats)
        int64_t periodNs = cycleEnd - cycleStart;

        // === Stats (outside core measurement) ===
        ++totalCycles;
        double p = static_cast<double>(periodNs) / 1000.0;

        double delta = p - cumMean;
        cumMean += delta / totalCycles;
        cumM2 += delta * (p - cumMean);
        if (p < cumMin) cumMin = p;
        if (p > cumMax) cumMax = p;

        ++winCount;
        winSum += p;
        if (p < winMin) winMin = p;
        if (p > winMax) winMax = p;

        if (winCount >= 5000) {
            double winAvg = winSum / winCount;
            double cumSd = std::sqrt(cumM2 / totalCycles);

            printf("Period | Cycles: %5u | Set: %.3fµs | Min: %.3fµs | Max: %.3fµs | Avg: %.3fµs\n",
                   winCount, static_cast<double>(target_period_ns)/1000.0, winMin, winMax, winAvg);
            printf("Cumulative | Cycles: %6" PRIu64 " | Avg: %.3fµs | Jitter ±%.3fµs\n\n",
                   totalCycles, cumMean, cumSd);

            winCount = 0; winSum = 0; winMin = std::numeric_limits<double>::max(); winMax = 0;
        }

        // Sleep remainder
        if (periodNs < target_period_ns) {
            int64_t remain = target_period_ns - periodNs;
            struct timespec ts{};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ts.tv_sec += remain / 1000000000LL;
            ts.tv_nsec += remain % 1000000000LL;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
        } else {
            fprintf(stderr, "[overrun] %.1f µs\n", static_cast<double>(periodNs)/1000.0);
        }
    }
}