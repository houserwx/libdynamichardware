#include "dynamichardware/dhdo/DHDO.h"

#include <cstring>

namespace dynamichardware::dhdo {

void DHDOEntry::read() noexcept
{
    if (!image) return;

    switch (type) {
        case EntryType::BoolInput: {
            const uint8_t byte = *image;
            const bool raw = (byte >> bitOffset) & 1u;
            boolVal_ = debounce.filter(raw, dynamichardware::rt::signalProcessNowNs());
            break;
        }
        case EntryType::Int32Input: {
            // 32-bit little-endian (encoder, pulse width, etc.)
            std::memcpy(&int32Val_, image, sizeof(int32Val_));
            break;
        }
        case EntryType::Int16Input: {
            // 16-bit little-endian (ADC value)
            std::memcpy(&int16Val_, image, sizeof(int16Val_));
            break;
        }
        case EntryType::FloatInput: {
            // 32-bit IEEE-754 float — used by any calibrated sensor (IMU, GPS, baro, etc.)
            std::memcpy(&floatVal_, image, sizeof(floatVal_));
            break;
        }
        default:
            break;
    }
}

void DHDOEntry::write() noexcept
{
    if (!image) return;

    switch (type) {
        case EntryType::BoolOutput: {
            const bool pinState = pulse.tick(dynamichardware::rt::signalProcessNowNs());
            if (pinState) {
                *image |=  static_cast<uint8_t>(1U << bitOffset);
            } else {
                *image &= static_cast<uint8_t>(~(1U << bitOffset));
            }
            break;
        }
        case EntryType::Int16Output: {
            std::memcpy(image, &int16Desired_, sizeof(int16Desired_));
            break;
        }
        case EntryType::FloatOutput: {
            std::memcpy(image, &floatDesired_, sizeof(floatDesired_));
            break;
        }
        default:
            break;
    }
}

bool DHDOEntry::getBool() const noexcept
{
    if (type == EntryType::BoolInput) return boolVal_;
    if (type == EntryType::BoolOutput) return pulse.isHighOrLatched();
    return false;
}

void DHDOEntry::setBool(bool v) noexcept
{
    if (type == EntryType::BoolOutput) {
        pulse.arm(v, dynamichardware::rt::signalProcessNowNs());
    }
}

void DHDO::freeze()
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

} // namespace dynamichardware::dhdo
