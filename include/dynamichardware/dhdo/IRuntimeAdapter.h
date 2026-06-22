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

    /// Optional one-time initialization after adapter is created but before RT loop.
    virtual void initialize() noexcept {}

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
