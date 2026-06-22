#pragma once

// ============================================================================
// PhaseManager — lifecycle state machine for discovery→mapping→build flow.
//
// Enforces strict ordering: DISCOVERY → MAPPING → BUILD_RT → RUNNING → SHUTDOWN.
// Prevents invalid transitions (e.g., calling buildRT() before discover()).
// Header-only with inline implementation for zero overhead.
// ============================================================================

#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace dynamichardware::config {

enum class HardwarePhase : uint8_t {
    DISCOVERY,   ///< Scanning hardware; catalog is writable by scanners only
    MAPPING,     ///< Consumer defines channels via builder.mapChannel() calls
    BUILD_RT,    ///< Adapter.build(channels) constructs RT process image objects
    RUNNING,     ///< RT loop active; freeze() called; no more modifications allowed
    SHUTDOWN     ///< Context destroyed; backends released
};

class PhaseManager {
public:
    explicit PhaseManager(HardwarePhase initial = HardwarePhase::DISCOVERY) noexcept
        : current_(initial) {}

    /// Advance to the next valid phase in the sequence.
    /// Returns true on success; throws std::invalid_argument if transition is illegal.
    [[nodiscard]] bool advance(HardwarePhase target) {
        if (!isValidTransition(current_, target)) {
            throw std::invalid_argument(
                std::string("Invalid phase transition from ") + std::string(phaseName(current_)) + " to " + std::string(phaseName(target)));
        }
        current_ = target;
        return true;
    }

    /// Query current lifecycle phase (no mutation).
    [[nodiscard]] constexpr HardwarePhase get() const noexcept { return current_; }

    /// Check if a specific phase is active.
    [[nodiscard]] constexpr bool isAt(HardwarePhase target) const noexcept {
        return current_ == target;
    }

    /// Explicit opt-in reset to DISCOVERY phase for intentional re-scanning scenarios
    /// (e.g., hardware hot-plug, configuration reload).
    /// Returns true if successful; false if already at DISCOVERY or past RUNNING.
    [[nodiscard]] bool resetToDiscovery() noexcept {
        // Allow reset from any phase up to BUILD_RT; once RUNNING the context is frozen.
        if (current_ > HardwarePhase::BUILD_RT) {
            return false;
        }
        current_ = HardwarePhase::DISCOVERY;
        return true;
    }

private:
    HardwarePhase current_;

    static constexpr bool isValidTransition(HardwarePhase from, HardwarePhase to) noexcept {
        using U = std::underlying_type_t<HardwarePhase>;
        
        // Allow forward transitions only (strict ordering enforcement)
        if (static_cast<U>(to) <= static_cast<U>(from)) {
            return false;  // Can't go backward or stay at same phase during advance()
        }

        // Allow skip-ahead by exactly one step OR jumping to SHUTDOWN from any state
        auto diff = static_cast<U>(to) - static_cast<U>(from);
        return diff == 1 || to == HardwarePhase::SHUTDOWN;
    }

    std::string_view phaseName(HardwarePhase p) const noexcept {
        switch (p) {
            case HardwarePhase::DISCOVERY: return "DISCOVERY";
            case HardwarePhase::MAPPING:   return "MAPPING";
            case HardwarePhase::BUILD_RT:  return "BUILD_RT";
            case HardwarePhase::RUNNING:   return "RUNNING";
            case HardwarePhase::SHUTDOWN:  return "SHUTDOWN";
            default:                       return "<unknown>";
        }
    }
};

} // namespace dynamichardware::config
