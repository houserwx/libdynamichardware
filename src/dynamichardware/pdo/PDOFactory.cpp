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

// ============================================================================
// stringToEntryType — "DigitalInput" → EntryType::DigitalInput
// ============================================================================
EntryType PDOFactory::stringToEntryType(const std::string& channelType)
{
    // Case-insensitive comparison helper
    auto icmp = [](const std::string& a, const char* b) {
        return std::equal(a.begin(), a.end(), b, [](char c1, char c2) {
            return std::tolower(c1) == std::tolower(c2);
        });
    };

    if (icmp(channelType, "DigitalInput"))  return EntryType::DigitalInput;
    if (icmp(channelType, "DigitalOutput")) return EntryType::DigitalOutput;
    if (icmp(channelType, "Encoder"))       return EntryType::Encoder;
    if (icmp(channelType, "AnalogInput"))   return EntryType::AnalogInput;
    if (icmp(channelType, "AnalogOutput"))  return EntryType::AnalogOutput;
    if (icmp(channelType, "MessageOut"))    return EntryType::MessageOut;
    if (icmp(channelType, "MessageIn"))     return EntryType::MessageIn;

    // IMU sensor types
    if (icmp(channelType, "IMU_GyroX"))     return EntryType::IMU_GyroX;
    if (icmp(channelType, "IMU_GyroY"))     return EntryType::IMU_GyroY;
    if (icmp(channelType, "IMU_GyroZ"))     return EntryType::IMU_GyroZ;
    if (icmp(channelType, "IMU_AccelX"))    return EntryType::IMU_AccelX;
    if (icmp(channelType, "IMU_AccelY"))    return EntryType::IMU_AccelY;
    if (icmp(channelType, "IMU_AccelZ"))    return EntryType::IMU_AccelZ;
    if (icmp(channelType, "MagnetometerX")) return EntryType::MagnetometerX;
    if (icmp(channelType, "MagnetometerY")) return EntryType::MagnetometerY;
    if (icmp(channelType, "MagnetometerZ")) return EntryType::MagnetometerZ;
    if (icmp(channelType, "Barometer"))     return EntryType::Barometer;

    // GPS types
    if (icmp(channelType, "GPS_Latitude"))    return EntryType::GPS_Latitude;
    if (icmp(channelType, "GPS_Longitude"))   return EntryType::GPS_Longitude;
    if (icmp(channelType, "GPS_Altitude"))    return EntryType::GPS_Altitude;
    if (icmp(channelType, "GPS_Heading"))     return EntryType::GPS_Heading;
    if (icmp(channelType, "GPS_FixQuality"))  return EntryType::GPS_FixQuality;

    // Unknown — default to DigitalInput
    return EntryType::DigitalInput;
}

// ============================================================================
// entryTypeToString — EntryType::DigitalInput → "DigitalInput"
// ============================================================================
const char* PDOFactory::entryTypeToString(EntryType type)
{
    switch (type) {
        case EntryType::DigitalInput:   return "DigitalInput";
        case EntryType::DigitalOutput:  return "DigitalOutput";
        case EntryType::Encoder:        return "Encoder";
        case EntryType::AnalogInput:    return "AnalogInput";
        case EntryType::AnalogOutput:   return "AnalogOutput";
        case EntryType::MessageOut:     return "MessageOut";
        case EntryType::MessageIn:      return "MessageIn";
        case EntryType::IMU_GyroX:      return "IMU_GyroX";
        case EntryType::IMU_GyroY:      return "IMU_GyroY";
        case EntryType::IMU_GyroZ:      return "IMU_GyroZ";
        case EntryType::IMU_AccelX:     return "IMU_AccelX";
        case EntryType::IMU_AccelY:     return "IMU_AccelY";
        case EntryType::IMU_AccelZ:     return "IMU_AccelZ";
        case EntryType::MagnetometerX:  return "MagnetometerX";
        case EntryType::MagnetometerY:  return "MagnetometerY";
        case EntryType::MagnetometerZ:  return "MagnetometerZ";
        case EntryType::Barometer:      return "Barometer";
        case EntryType::GPS_Latitude:   return "GPS_Latitude";
        case EntryType::GPS_Longitude:  return "GPS_Longitude";
        case EntryType::GPS_Altitude:   return "GPS_Altitude";
        case EntryType::GPS_Heading:    return "GPS_Heading";
        case EntryType::GPS_FixQuality: return "GPS_FixQuality";
    }
    return "Unknown";
}

// ============================================================================
// defaultBitLength — EntryType → bit width in process image
// ============================================================================
uint8_t PDOFactory::defaultBitLength(EntryType type)
{
    switch (type) {
        // 1-bit digital I/O
        case EntryType::DigitalInput:
        case EntryType::DigitalOutput:
            return 1;

        // 16-bit analog
        case EntryType::AnalogInput:
        case EntryType::AnalogOutput:
            return 16;

        // 32-bit encoder / float
        case EntryType::Encoder:
        case EntryType::IMU_GyroX:
        case EntryType::IMU_GyroY:
        case EntryType::IMU_GyroZ:
        case EntryType::IMU_AccelX:
        case EntryType::IMU_AccelY:
        case EntryType::IMU_AccelZ:
        case EntryType::MagnetometerX:
        case EntryType::MagnetometerY:
        case EntryType::MagnetometerZ:
        case EntryType::GPS_Latitude:
        case EntryType::GPS_Longitude:
        case EntryType::GPS_Altitude:
        case EntryType::GPS_Heading:
            return 32;

        // Barometer has two floats (pressure + altitude) = 64 bits
        case EntryType::Barometer:
            return 64;

        // GPS fix quality is 16-bit
        case EntryType::GPS_FixQuality:
            return 16;

        // Message slots use memcpy, not bit extraction — bitLength is 0 (N/A)
        case EntryType::MessageOut:
        case EntryType::MessageIn:
            return 0;

        default:
            return 1;  // safe default
    }
}

} // namespace dynamichardware::pdo
