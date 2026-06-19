#include "dynamichardware/pdo/PDOFactory.h"

#include <algorithm>
#include <cstddef>

namespace dynamichardware::pdo {

// ============================================================================
// fromCatalogEntry — discovery-driven PDOEntry creation
// ============================================================================
PDOEntry PDOFactory::fromCatalogEntry(const CatalogEntry& ce)
{
    PDOEntry entry{};
    entry.type       = stringToEntryType(ce.channelType);
    entry.uuid       = ce.uuid;
    entry.bitLength  = static_cast<uint8_t>(defaultBitLength(entry.type));

    // Apply simulation parameters as pulse/debounce config
    if (ce.isSimulated) {
        if (ce.sim.pulseMs > 0) {
            entry.configurePulseMs(ce.sim.pulseMs);
        }
        if (ce.sim.debounceMs > 0) {
            entry.configureDebounceMs(ce.sim.debounceMs);
        }
    }

    return entry;
}

// ============================================================================
// create — explicit config-driven PDOEntry creation
// ============================================================================
PDOEntry PDOFactory::create(
    EntryType  type,
    std::string uuid,
    uint32_t   pulseMs,
    uint32_t   debounceMs,
    uint8_t    bitLength)
{
    PDOEntry entry{};
    entry.type       = type;
    entry.uuid       = std::move(uuid);
    entry.bitLength  = bitLength ? bitLength : static_cast<uint8_t>(defaultBitLength(type));

    if (pulseMs > 0)    entry.configurePulseMs(pulseMs);
    if (debounceMs > 0) entry.configureDebounceMs(debounceMs);

    return entry;
}

// stringToEntryType — catalog channelType string → EntryType (value format)
//
// The catalog may use semantic names ("IMU_GyroX", "GPS_Latitude") for
// human readability. This function maps them to the transport-agnostic
// value type based on the data format in the process image.
// ============================================================================
EntryType PDOFactory::stringToEntryType(const std::string& channelType)
{
    // Case-insensitive comparison helper
    auto icmp = [](const std::string& a, const char* b) {
        return std::equal(a.begin(), a.end(), b, [](char c1, char c2) {
            return std::tolower(c1) == std::tolower(c2);
        });
    };

    // Direct value-type mappings (preferred catalog format)
    if (icmp(channelType, "BoolInput"))    return EntryType::BoolInput;
    if (icmp(channelType, "BoolOutput"))   return EntryType::BoolOutput;
    if (icmp(channelType, "Int32Input"))   return EntryType::Int32Input;
    if (icmp(channelType, "Int16Input"))   return EntryType::Int16Input;
    if (icmp(channelType, "Int16Output"))  return EntryType::Int16Output;
    if (icmp(channelType, "FloatInput"))   return EntryType::FloatInput;
    if (icmp(channelType, "FloatOutput"))  return EntryType::FloatOutput;
    if (icmp(channelType, "MessageOut"))   return EntryType::MessageOut;
    if (icmp(channelType, "MessageIn"))    return EntryType::MessageIn;

    // Legacy aliases (for backward compatibility with existing catalogs)
    if (icmp(channelType, "DigitalInput"))  return EntryType::BoolInput;
    if (icmp(channelType, "DigitalOutput")) return EntryType::BoolOutput;
    if (icmp(channelType, "Encoder"))       return EntryType::Int32Input;
    if (icmp(channelType, "AnalogInput"))   return EntryType::Int16Input;
    if (icmp(channelType, "AnalogOutput"))  return EntryType::Int16Output;

    // Semantic sensor types — map to value format
    // IMU sensors (float)
    if (icmp(channelType, "IMU_GyroX"))     return EntryType::FloatInput;
    if (icmp(channelType, "IMU_GyroY"))     return EntryType::FloatInput;
    if (icmp(channelType, "IMU_GyroZ"))     return EntryType::FloatInput;
    if (icmp(channelType, "IMU_AccelX"))    return EntryType::FloatInput;
    if (icmp(channelType, "IMU_AccelY"))    return EntryType::FloatInput;
    if (icmp(channelType, "IMU_AccelZ"))    return EntryType::FloatInput;
    if (icmp(channelType, "MagnetometerX")) return EntryType::FloatInput;
    if (icmp(channelType, "MagnetometerY")) return EntryType::FloatInput;
    if (icmp(channelType, "MagnetometerZ")) return EntryType::FloatInput;
    if (icmp(channelType, "Barometer"))     return EntryType::FloatInput;

    // GPS types
    if (icmp(channelType, "GPS_Latitude"))   return EntryType::FloatInput;
    if (icmp(channelType, "GPS_Longitude"))  return EntryType::FloatInput;
    if (icmp(channelType, "GPS_Altitude"))   return EntryType::FloatInput;
    if (icmp(channelType, "GPS_Heading"))    return EntryType::FloatInput;
    if (icmp(channelType, "GPS_FixQuality")) return EntryType::Int16Input;

    // Unknown — default to BoolInput (safest conservative default)
    return EntryType::BoolInput;
}

// ============================================================================
// entryTypeToString — EntryType → string (composited from bitmask fields)
// ============================================================================
const char* PDOFactory::entryTypeToString(EntryType type)
{
    // Static buffer for dynamic composition (not RT-safe, but this is init-time only)
    static char buf[32];

    // Well-known convenience constants get their names directly
    if (type == EntryType::BoolInput)    return "BoolInput";
    if (type == EntryType::BoolOutput)   return "BoolOutput";
    if (type == EntryType::Int8Input)    return "Int8Input";
    if (type == EntryType::Int16Input)   return "Int16Input";
    if (type == EntryType::Int32Input)   return "Int32Input";
    if (type == EntryType::Int8Output)   return "Int8Output";
    if (type == EntryType::Int16Output)  return "Int16Output";
    if (type == EntryType::Int32Output)  return "Int32Output";
    if (type == EntryType::FloatInput)   return "FloatInput";
    if (type == EntryType::FloatOutput)  return "FloatOutput";
    if (type == EntryType::MessageIn)    return "MessageIn";
    if (type == EntryType::MessageOut)   return "MessageOut";

    // Any other bitmask composition gets a dynamic name
    const char* dir = entryIsInput(type) ? "In" : entryIsOutput(type) ? "Out" : "";
    const char* base;
    switch (type & 0x18) {
        case BASE_BOOL:  base = "Bool";  break;
        case BASE_INT:   base = "Int";   break;
        case BASE_FLOAT: base = "Float"; break;
        case BASE_MSG:   base = "Msg";   break;
        default:         base = "?";     break;
    }
    const char* size;
    switch (type & 0x60) {
        case SZ_1:  size = "1";  break;
        case SZ_8:  size = "8";  break;
        case SZ_16: size = "16"; break;
        case SZ_32: size = "32"; break;
        default:    size = "?";  break;
    }
    std::snprintf(buf, sizeof(buf), "%s%s%s%s", entryIsSigned(type) ? "" : "U", base, size, dir);
    return buf;
}

// ============================================================================
// defaultBitLength — EntryType → bit width in process image
// Derived from the bitmask size field — no switch needed.
// ============================================================================
uint8_t PDOFactory::defaultBitLength(EntryType type)
{
    if (entryIsMessage(type)) return 0;

    switch (entryBitSize(type)) {
        case SZ_1:  return 1;
        case SZ_8:  return 8;
        case SZ_16: return 16;
        case SZ_32: return 32;
        default:    return 1;  // safe default
    }
}

} // namespace dynamichardware::pdo
