// ============================================================================
// rt_clock.h — PREEMPT_RT-safe high-resolution timestamp helpers.
// All timing via CLOCK_MONOTONIC which is NTP-immune on Linux RT kernels.
// ============================================================================

#pragma once

#include <cstdint>
#include <ctime>

/// Absolute nanosecond timestamp from CLOCK_MONOTONIC (never wraps at reasonable intervals).
static inline int64_t now_ns() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + static_cast<int64_t>(ts.tv_nsec);
}

inline double ns_to_us(int64_t ns) noexcept { return static_cast<double>(ns) / 1000.0; }
inline double ns_to_ms(int64_t ns) noexcept { return static_cast<double>(ns) / 1'000'000.0; }
