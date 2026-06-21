#pragma once

// ============================================================================
// HardwareDescriptor — pure data result from a hardware scan.
//
// Carries structured information about one discovered channel without mutating
// any shared state or reaching into catalog internals.  The orchestrator converts
// these descriptors to CatalogEntry objects during the DISCOVERY phase.
// ============================================================================

#include "dynamichardware/dhdo/HardwareCatalog.h"

namespace dynamichardware::dhdo {

/// Pure data result from a hardware scan — no side effects, no catalog mutation.
struct HardwareDescriptor {
    std::string uuid;                    ///< Deterministic UUID from backend hash
    std::string channelType;             ///< "DigitalInput", "FloatOutput", etc.
    std::string name;                    ///< Human-readable display name
    bool        isOutput{false};         ///< Direction hint (used by orchestrator)

    BackendSpecificData backendData;     ///< Structured per-backend fields (variant)
    BackendType         backend{BackendType::UNKNOWN};  ///< Which backend populated this descriptor
};

} // namespace dynamichardware::dhdo
