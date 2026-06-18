#pragma once
#include <cstdint>
#include <time.h>

// ---------------------------------------------------------------------------
// Cycle Time Service
// ---------------------------------------------------------------------------
// signalProcessTickNow()  — call ONCE at the top of each RT cycle.
//   Reads CLOCK_MONOTONIC via vDSO (~10 ns on ARM), caches the result in
//   gSignalProcessNowNs, and returns it.
//
// signalProcessNowNs()    — call anywhere within the cycle.
//   Returns the cached value written by the most recent tickNow() — zero
//   cost (single load instruction).  Never calls clock_gettime.
//
// Both are single-RT-thread only.  gSignalProcessNowNs is not atomic.
// ---------------------------------------------------------------------------
namespace common::rt {

// C++17 inline variable — exactly one definition across all translation units.
inline uint64_t gSignalProcessNowNs{0u};

/// Called once per RT cycle before readAll().  Updates the shared timestamp.
inline uint64_t signalProcessTickNow() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    gSignalProcessNowNs = static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000u
                        + static_cast<uint64_t>(ts.tv_nsec);
    return gSignalProcessNowNs;
}

/// Returns the cycle timestamp cached by the last signalProcessTickNow() call.
[[nodiscard]] inline uint64_t signalProcessNowNs() noexcept {
    return gSignalProcessNowNs;
}

// ---------------------------------------------------------------------------
// PulseMachine  — composable one-shot pulse state machine (time-based)
// ---------------------------------------------------------------------------
struct PulseMachine {
    void configure(uint32_t ms) noexcept {
        durationNs_ = uint64_t{ms} * 1'000'000u;
    }

    void arm(bool v, uint64_t nowNs) noexcept {
        if (durationNs_ > 0u) {
            if (v && !active_) {
                active_    = true;
                expiresAt_ = nowNs + durationNs_;
            }
        } else {
            latched_ = v;
        }
    }

    bool tick(uint64_t nowNs) noexcept {
        if (durationNs_ > 0u) {
            if (active_) {
                if (nowNs >= expiresAt_) active_ = false;
                else                    return true;
            }
            return false;
        }
        return latched_;
    }

    [[nodiscard]] bool isHighOrLatched() const noexcept {
        return (durationNs_ > 0u) ? active_ : latched_;
    }

    [[nodiscard]] bool isPulseMode() const noexcept { return durationNs_ > 0u; }

private:
    uint64_t durationNs_{0u};
    uint64_t expiresAt_{0u};
    bool     active_{false};
    bool     latched_{false};
};

// ---------------------------------------------------------------------------
// DebounceMachine  — composable input debounce state machine (time-based)
// ---------------------------------------------------------------------------
struct DebounceMachine {
    void configure(uint32_t ms) noexcept {
        settleNs_ = uint64_t{ms} * 1'000'000u;
    }

    /// Call once per cycle.  Returns true only after the input has been
    /// stable (high or low) for settleNs_ nanoseconds.
    bool filter(bool raw, uint64_t nowNs) noexcept {
        if (raw != rawInput_) {
            rawInput_    = raw;
            settleStart_ = nowNs;
            settled_     = false;
        }
        if (!settled_ && (nowNs - settleStart_) >= settleNs_) {
            settled_  = true;
            settledV_ = rawInput_;
        }
        return settledV_;
    }

    [[nodiscard]] bool isSettled() const noexcept { return settled_; }
    [[nodiscard]] bool settledValue() const noexcept { return settledV_; }

private:
    uint64_t settleNs_{0u};
    uint64_t settleStart_{0u};
    bool     rawInput_{false};
    bool     settled_{false};
    bool     settledV_{false};
};

} // namespace common::rt
