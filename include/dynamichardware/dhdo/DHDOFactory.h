#pragma once

// ============================================================================
// PDOFactory.h — Creates and configures PDO / DHDOEntry objects from
// CatalogEntry records or explicit definitions.
//
// This is the HAL's factory: given a catalog entry (from hardware discovery
// or a persisted catalog file), it produces a DHDOEntry with the correct
// EntryType, pulse/debounce parameters, and UUID binding.
//
// Usage:
//   auto entry = PDOFactory::fromCatalogEntry(catalogEntry);
//   // or with explicit overrides:
//   auto entry = PDOFactory::create(entryType, uuid, pulseMs, debounceMs);
// ============================================================================

#include "dynamichardware/dhdo/DHDO.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"

namespace dynamichardware::dhdo {

class DHDOFactory {
public:
    // ---- Construction from CatalogEntry (discovery-driven) ----

    /// Create a DHDOEntry from a HardwareCatalog::CatalogEntry.
    /// Maps channelType strings to EntryType enum values.
    /// Applies pulse/debounce from the entry's SimParams if present.
    /// @param ce   Catalog entry (from HardwareCatalog)
    /// @return     Configured DHDOEntry
    [[nodiscard]] static DHDOEntry fromCatalogEntry(const CatalogEntry& ce);

    // ---- Explicit construction (config-driven) ----

    /// Create a DHDOEntry with explicit parameters.
    /// @param type       Entry type (digital, encoder, analog, etc.)
    /// @param uuid       Stable UUID for this channel
    /// @param pulseMs    Pulse duration in ms for output types (0 = latched)
    /// @param debounceMs Debounce duration in ms for input types (0 = disabled)
    /// @param bitLength  Bit width in process image (1, 16, 32, etc.)
    /// @return           Configured DHDOEntry
    [[nodiscard]] static DHDOEntry create(
        EntryType  type,
        std::string uuid,
        uint32_t   pulseMs   = 0,
        uint32_t   debounceMs = 0,
        uint8_t    bitLength  = 0
    );

    // ---- Channel type string → EntryType mapping ----

    /// Convert a catalog channelType string ("DigitalInput", "Encoder", etc.)
    /// to the corresponding EntryType enum value.
    /// Returns DigitalInput for unknown types.
    [[nodiscard]] static EntryType stringToEntryType(const std::string& channelType);

    /// Reverse of stringToEntryType — returns "DigitalInput", "Encoder", etc.
    [[nodiscard]] static const char* entryTypeToString(EntryType type);

    // ---- Bit length inference ----

    /// Infer the bit length from EntryType.
    /// Digital I/O → 1, Analog → 16, Encoder → 32, Float types → 32.
    [[nodiscard]] static uint8_t defaultBitLength(EntryType type);
};

} // namespace dynamichardware::dhdo
