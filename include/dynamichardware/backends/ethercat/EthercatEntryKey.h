#pragma once

// ============================================================================
// EthercatEntryKey.h — single source of truth for EtherCAT stable identity keys.
//
// Key format: EC|{vendorId}|{productCode}|{DIRECTION}:{channelType}|POS{pos}|{pdoIdx}:{subIdx}
// Example:    EC|2|157888594|OUT:DigitalOutput|POS02|28672:01
//
// Uses this everywhere (discovery + RT backend) so catalog keys and DHDOEntry::uuid
// are guaranteed to match. If you change the key format, you only change it here.
// ============================================================================

#include <cstdint>
#include <string>

namespace dynamichardware::ethercat {

/// Infer a human-readable channel type from PDO bit length and direction.
inline const char* inferChannelType(uint8_t bitLength, bool isOutput) noexcept {
    if (bitLength == 1U && !isOutput) { return "DigitalInput";  }
    if (bitLength == 1U &&  isOutput) { return "DigitalOutput"; }
    if (bitLength == 32U && !isOutput) { return "Encoder";       }
    if (bitLength == 16U && !isOutput) { return "AnalogInput";   }
    return "Raw";
}

/// Build the canonical stable identity key for an EtherCAT PDO entry.
/// This MUST be used by BOTH discovery scanners AND RT backends.
inline std::string buildEntryKey(
        uint32_t vendorId, uint32_t productCode,
        bool     isOutput, uint8_t bitLength,
        uint16_t slavePos,
        uint16_t pdoIndex, uint8_t pdoSubindex)
{
    const char* ctype  = inferChannelType(bitLength, isOutput);
    const char* dirTag = isOutput ? "OUT" : "IN";

    return std::string("EC|") + std::to_string(vendorId) +
           "|" + std::to_string(productCode) +
           "|" + dirTag + ':' + ctype +
           "|POS" + std::to_string(slavePos) +
           "|" + std::to_string(pdoIndex) + ":" + std::to_string(pdoSubindex);
}

} // namespace dynamichardware::ethercat
