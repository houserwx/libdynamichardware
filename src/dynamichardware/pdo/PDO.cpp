#include "dynamichardware/pdo/PDO.h"

#include <cstring>

namespace dynamichardware::pdo {

void PDOEntry::read() noexcept
{
    if (!image) return;

    switch (type) {
        case EntryType::BoolInput: {
            const uint8_t byte = image[byteOffset];
            const bool raw = (byte >> bitOffset) & 1u;
            boolVal_ = debounce.filter(raw, dynamichardware::rt::signalProcessNowNs());
            break;
        }
        case EntryType::Int32Input: {
            // 32-bit little-endian (encoder, pulse width, etc.)
            std::memcpy(&int32Val_, image + byteOffset, sizeof(int32Val_));
            break;
        }
        case EntryType::Int16Input: {
            // 16-bit little-endian (ADC value)
            std::memcpy(&int16Val_, image + byteOffset, sizeof(int16Val_));
            break;
        }
        case EntryType::FloatInput: {
            // 32-bit IEEE-754 float — used by any calibrated sensor (IMU, GPS, baro, etc.)
            std::memcpy(&floatVal_, image + byteOffset, sizeof(floatVal_));
            break;
        }
        default:
            break;
    }
}

void PDOEntry::write() noexcept
{
    if (!image) return;

    switch (type) {
        case EntryType::BoolOutput: {
            const bool pinState = pulse.tick(dynamichardware::rt::signalProcessNowNs());
            uint8_t* bytePtr = image + byteOffset;
            if (pinState) {
                *bytePtr |= (1u << bitOffset);
            } else {
                *bytePtr &= ~(1u << bitOffset);
            }
            break;
        }
        case EntryType::Int16Output: {
            std::memcpy(image + byteOffset, &int16Desired_, sizeof(int16Desired_));
            break;
        }
        case EntryType::FloatOutput: {
            std::memcpy(image + byteOffset, &floatDesired_, sizeof(floatDesired_));
            break;
        }
        default:
            break;
    }
}

bool PDOEntry::getBool() const noexcept
{
    if (type == EntryType::BoolInput) return boolVal_;
    if (type == EntryType::BoolOutput) return pulse.isHighOrLatched();
    return false;
}

void PDOEntry::setBool(bool v) noexcept
{
    if (type == EntryType::BoolOutput) {
        pulse.arm(v, dynamichardware::rt::signalProcessNowNs());
    }
}

void PDO::freeze()
{
    entries.shrink_to_fit();
    image.shrink_to_fit();

    // Re-base image pointers for adapters that own their buffer.
    // If image.empty(), entry image pointers are left untouched — they already
    // point into backend-owned memory (e.g., EtherCAT domainData_).
    if (!image.empty()) {
        for (auto& entry : entries) {
            entry.image = image.data() + entry.byteOffset;
        }
    }
}

} // namespace dynamichardware::pdo
