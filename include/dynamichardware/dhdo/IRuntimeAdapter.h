#pragma once

// ============================================================================
// IRuntimeAdapter — canonical runtime lifecycle interface for RT backends.
//
// Inherits IDHDOBuilder (configuration flows through build(channels)), adds
// RT cycle hooks (onBeforeReadInputs/onAfterWriteOutputs). Owns the frozen PDO vector.
//
// Canonical replacement for the old IRTBackend interface.
// ============================================================================

#include "dynamichardware/dhdo/IDHDOBuilder.h"

// Forward declaration for catalog pointer — avoids circular include.
namespace dynamichardware::dhdo { class HardwareCatalog; }

#include <string>
#include <unordered_map>
#include <vector>

namespace dynamichardware::dhdo {
class IRuntimeAdapter : public IDHDOBuilder {
public:
    virtual ~IRuntimeAdapter() = default;

    // Non-copyable, non-movable (owned by HardwareRegistry)
    IRuntimeAdapter(const IRuntimeAdapter&)            = delete;
    IRuntimeAdapter& operator=(const IRuntimeAdapter&) = delete;
    IRuntimeAdapter(IRuntimeAdapter&&)                 = delete;
    IRuntimeAdapter& operator=(IRuntimeAdapter&&)      = delete;

    /// Post-creation configuration from orchestrator's enabledBackends config map.
    /// Called after adapter construction but before build(). Backends ignore unknown keys.
    virtual void configure(const std::unordered_map<std::string, std::string>& /*config*/) {}

    /// Inject a const reference to the hardware catalog so backends can resolve UUIDs
    /// during build(channels).  Default implementation is no-op for backends that don't
    /// need catalog lookups (e.g., Simulated uses its own definition source).
    virtual void setCatalog(const HardwareCatalog* /*catalog*/) noexcept {}

    /// Optional one-time initialization after adapter is created but before RT loop.
    virtual void initialize() noexcept {}

    /// Set the target RT cycle period (nanoseconds). Backends that care about timing
    /// (e.g., EtherCAT DC sync, Simulated waveform math) can use this to adjust internal
    /// state. Safe to call during RUNNING phase; changes take effect immediately on next
    /// readAll/writeAll cycle. Default implementation is a no-op for backends that don't
    /// need timing awareness (GPIO, I2C, SPI are purely reactive).
    /// @param nanoseconds Target inter-cycle period in nanoseconds (e.g., 1'000'000 for 1ms / 1kHz)
    virtual void setCyclePeriod(uint64_t /*nanoseconds*/) noexcept {}

    /// Get the current effective cycle period (nanoseconds). Returns 0 if unknown/unset.
    [[nodiscard]] virtual uint64_t getCyclePeriod() const noexcept { return 0; }

    /// Called in the RT cycle before reading input channels.
    virtual void onBeforeReadInputs()  noexcept = 0;
    /// Called in the RT cycle after writing output channels.
    virtual void onAfterWriteOutputs() noexcept = 0;

protected:
    IRuntimeAdapter() = default;

    /// Frozen PDO vector — accessible to derived backends for construction,
    /// and to HardwareRegistry via friendship for mutable RT sweep iteration.
    friend class HardwareRegistry;
    std::vector<DHDO> dhdos_;

public:
    [[nodiscard]] const std::vector<DHDO>& getDHDOS() const noexcept override { return dhdos_; }
};

} // namespace dynamichardware::dhdo
