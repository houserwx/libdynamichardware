#pragma once

// ============================================================================
// IDHDOBuilder — constructs DHDO objects from mapped channels.
//
// Replaces the old registerLine/registerDevice pattern where factories pushed
// backend-specific data INTO concrete backends via public setup methods.  
// Instead, all configuration flows through build(channels) as a parameter.
// ============================================================================

#include "dynamichardware/dhdo/DHDO.h"

namespace dynamichardware::dhdo {

/// Single channel mapping definition passed to RT adapter builders.
struct MappedChannel {
    std::string uuid;       ///< Catalog entry UUID (sole identity for lookup)
    EntryType   type;       ///< Consumer-specified direction + value format
    std::string name;       ///< Human-readable display name (optional override if empty)
};

/// Type alias for convenience when working with channel lists.
using MappedChannels = std::vector<MappedChannel>;

class IDHDOBuilder {
public:
    virtual ~IDHDOBuilder() = default;

    /// Build internal DHDO state from the provided channel list.
    /// Each backend looks up its own catalog entries using UUIDs and constructs
    /// internal state without external callers knowing HOW.
    /// Returns true on success; default implementation returns false (stub).
    [[nodiscard]] virtual bool build(const std::vector<MappedChannel>& /*channels*/) { return false; }

    /// Access built DHDOs after successful build().
    /// Concrete backends override this to return their built PDO list.
    [[nodiscard]] virtual const std::vector<DHDO>& getDHDOS() const noexcept 
    { static std::vector<DHDO> empty; return empty; }

protected:
    IDHDOBuilder() = default;
};

} // namespace dynamichardware::dhdo
