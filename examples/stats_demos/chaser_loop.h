// ============================================================================
// chaser_loop.h — Generic walking-light RT cycle with timing measurement.
// 
// HOT PATH INVARIANT: ZERO output — no printf, no chrono, no string ops.
// Only atomic stores into two SPSC rings + nanosleep(). All printing handled 
// by the stats thread consuming ring events independently.
// ============================================================================

#pragma once

#include <cstddef>
#include <ctime>
#include <vector>

#include "rt_clock.h"
#include "spsc_ring.h"

template <typename OutputPtrT>
void runChaserLoop(
    std::vector<OutputPtrT>& outputs,       // Live DHDOEntry* pointers (post-freeze)
    SpscRing<int64_t, 8192>& timing_ring,   // Wall-clock period_ns per cycle (every iteration)
    SpscRing<unsigned, 256>& event_ring,     // Channel index on each light switch (sparse pushes)
    long long target_period_ns,             // Target inter-cycle period in ns (e.g. 1'000'000 for 1ms)
    unsigned cycles_per_light               // How many RT cycles each light stays ON before advancing
) {
    unsigned current_light      = 0;
    unsigned cycles_since_switch = 0;

    int64_t prev_cycle_end = now_ns();      // Wall-clock anchor from last iteration's end.

    while (true) {
        for (size_t i = 0; i < outputs.size(); ++i)
            outputs[i]->setBool(i == current_light);

        int64_t compute_end = now_ns();
        if (compute_end - prev_cycle_end < target_period_ns) {
            long long sleep_ns = target_period_ns + prev_cycle_end - compute_end;
            struct timespec req{};
            req.tv_sec  = static_cast<time_t>(sleep_ns / 1'000'000'000LL);
            req.tv_nsec = static_cast<long>(sleep_ns % 1'000'000'000LL);
            nanosleep(&req, nullptr);
        }

        int64_t cycle_end   = now_ns();
        int64_t wall_period = cycle_end - prev_cycle_end;     // Full wall-clock inter-cycle delta.

        timing_ring.push(wall_period);                        // Atomic store — ONLY operation in hot path.
        prev_cycle_end = cycle_end;                            // Anchor for next iteration.

        ++cycles_since_switch;
        if (cycles_since_switch >= cycles_per_light) {
            cycles_since_switch = 0;
            event_ring.push(current_light);                    // Notify stats thread of channel switch.
            current_light = (current_light + 1) % outputs.size();
        }
    }
}
