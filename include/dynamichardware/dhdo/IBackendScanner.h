#pragma once

// ============================================================================
// IBackendScanner — one-shot hardware scanner interface.
//
// scan() returns pure data (vector of HardwareDescriptor) without mutating any
// shared catalog state.  This eliminates the old pattern where discover() directly
// wrote into a shared catalog_ pointer, coupling scanner internals to catalog internals.
// ============================================================================

#include "dynamichardware/dhdo/HardwareDescriptor.h"
#include <unordered_map>
#include <string>

namespace dynamichardware::dhdo {

class IBackendScanner {
public:
    virtual ~IBackendScanner() = default;

    /// Post-creation configuration from orchestrator's enabledBackends config map.
    /// Called after scanner construction but before scan(). Backends ignore unknown keys.
    virtual void configure(const std::unordered_map<std::string, std::string>& /*config*/) {}

    /// Scan connected hardware and return structured descriptors.
    /// Does NOT mutate any shared state — returns pure data only.
    /// Default implementation returns empty vector (no-op) for backends without discovery.
    [[nodiscard]] virtual std::vector<HardwareDescriptor> scan() { return {}; }

protected:
    IBackendScanner() = default;
};

} // namespace dynamichardware::dhdo
