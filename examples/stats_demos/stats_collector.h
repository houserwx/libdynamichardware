// ============================================================================
// stats_collector.h — Dedicated-thread statistics reporter for RT demos.
// 
// Owns a worker thread that drains two SPSC rings every ~5 seconds:
//   • timing_ring : int64_t wall-clock period_ns samples → accumulated into 
//     windowed min/max/avg/stddev/jitter metrics, printed periodically.
//   • event_ring  : unsigned channel index on each light switch → printed immediately.
// Zero-cost on producer side: only atomic stores in hot path. All string ops here.
// ============================================================================

#pragma once

#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "spsc_ring.h"

class StatsCollector {
public:
    explicit StatsCollector(
        SpscRing<int64_t, 8192>& timing_ring,
        SpscRing<unsigned, 256>& event_ring,
        long long target_period_ns,
        const std::vector<std::string>* channel_names = nullptr)  // Optional: for named channel display in stats thread only
        : timing_ring_(timing_ring), event_ring_(event_ring), 
          target_period_ns_(target_period_ns), channelNames_(channel_names),
          running_(true), last_channel_(0), worker_([this]() { runLoop(); }) {}

    ~StatsCollector() { stop(); }

    StatsCollector(const StatsCollector&) = delete;
    StatsCollector& operator=(const StatsCollector&) = delete;

private:
    SpscRing<int64_t, 8192>& timing_ring_;       // Wall-clock period_ns samples from RT loop.
    SpscRing<unsigned, 256>& event_ring_;         // Channel switch notifications from RT loop.
    const long long target_period_ns_;            // Expected inter-cycle period for drift calc.
    const std::vector<std::string>* channelNames_; // Optional: maps channel index → human-readable name
    std::atomic<bool> running_;                   // Shutdown flag for stats thread.
    std::atomic<unsigned> last_channel_{0};      // Latest channel-switch index (set by event drain).
    unsigned event_count_ = 0;                    // Monotonic counter for event display numbering.
    std::thread worker_;                          // Dedicated printing thread.

    void stop() noexcept {
        if (running_.exchange(false, std::memory_order_relaxed))
            if (worker_.joinable()) worker_.join();
    }

    void runLoop() noexcept {
        constexpr auto kPrintInterval = std::chrono::seconds(5);

        unsigned window_count  = 0;
        double   sum_period_ns = 0.0;
        double   sum_sq_ns     = 0.0;
        double   min_period_ns = std::numeric_limits<double>::max();
        double   max_period_ns = 0.0;
        unsigned total_cycles  = 0;

        auto next_print = std::chrono::steady_clock::now() + kPrintInterval;

        while (running_.load(std::memory_order_relaxed)) {
            timing_ring_.drain_all([&](int64_t elapsed_ns) noexcept {
                ++window_count;
                double p = static_cast<double>(elapsed_ns);
                sum_period_ns += p;
                sum_sq_ns     += p * p;
                if (p < min_period_ns) min_period_ns = p;
                if (p > max_period_ns) max_period_ns = p;
            });

            event_ring_.drain_all([this](unsigned ch) noexcept {
                last_channel_.store(ch, std::memory_order_relaxed);
                if (channelNames_ && ch < channelNames_->size()) {
                    std::printf("[evt #%u] -> %s\n", ++event_count_, (*channelNames_)[ch].c_str());
                } else {
                    std::printf("[evt #%u] channel -> %u\n", ++event_count_, ch);
                }
            });

            struct timespec req{}; req.tv_sec = 0; req.tv_nsec = 10'000'000LL;
            nanosleep(&req, nullptr);

            auto now = std::chrono::steady_clock::now();
            if (now >= next_print && window_count > 0) {
                printWindow(window_count, sum_period_ns, sum_sq_ns,
                           min_period_ns, max_period_ns, total_cycles);
                total_cycles += window_count;
                window_count  = 0;
                sum_period_ns = 0.0;
                sum_sq_ns     = 0.0;
                min_period_ns = std::numeric_limits<double>::max();
                max_period_ns = 0.0;
                next_print    = now + kPrintInterval;
            }
        }

        if (window_count > 0)
            printWindow(window_count, sum_period_ns, sum_sq_ns,
                       min_period_ns, max_period_ns, total_cycles);
    }

    void printWindow(unsigned count, double sum_p, double sum_sq,
                     double min_p, double max_p,
                     unsigned& /*total*/) const noexcept {
        double avg   = sum_p / count;
        double var   = (sum_sq / count) - (avg * avg);
        if (var < 0.0) var = 0.0;
        double sd    = std::sqrt(var);
        double drift = ((avg - target_period_ns_) / target_period_ns_) * 100.0;

        std::printf("\n" "╔══════════════════════════════════════╗\n"
                    "║ RT Cycle Stats (%u samples)\n", count);
        std::printf("╠══════════════════════════════════════╣\n");
        std::printf("║ Target:     %15.1f ns          ║\n",
                    static_cast<double>(target_period_ns_));
        std::printf("║ Avg period: %15.1f ns  %+.3f%% ║\n", avg, drift);
        std::printf("║ Std dev:    %15.1f ns  %.2f µs ║\n", sd, sd / 1000.0);
        std::printf("║ Min period: %15.1f ns          ║\n", min_p);
        std::printf("║ Max period: %15.1f ns          ║\n", max_p);
        std::printf("╚══════════════════════════════════════╝\n");
    }
};
