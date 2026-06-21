#pragma once

// ============================================================================
// IBackendScanner — one-shot hardware scanner interface.
//
// scan() returns pure data (vector of HardwareDescriptor) without mutating any
// shared catalog state.  This eliminates the old pattern where discover() directly
// wrote into a shared catalog_ pointer, coupling scanner internals to catalog internals.
// ============================================================================

#include "dynamichardware/dhdo/HardwareDescriptor.h"

namespace dynamichardware::dhdo {

class IBackendScanner {
public:
    virtual ~IBackendScanner() = default;

    /// Scan connected hardware and return structured descriptors.
    /// Does NOT mutate any shared state — returns pure data only.
    /// Default implementation returns empty list for backward compat during transition period.
    [[nodiscard]] virtual std::vector<HardwareDescriptor> scan() { return {}; }

protected:
    IBackendScanner() = default;
};

} // namespace dynamichardware::dhdo
