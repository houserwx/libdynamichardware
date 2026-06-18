#include "backends/pdo/PDO.h"

#include <cstring>

namespace fc::pdo {

void PDOEntry::read() noexcept
{
    if (!image) return;

    switch (type) {
        case EntryType::DigitalInput: {
            const uint8_t byte = image[byteOffset];
            const bool raw = (byte >> bitOffset) & 1u;
            boolVal_ = debounce.filter(raw, common::rt::signalProcessNowNs());
            break;
        }
        case EntryType::Encoder: {
            // 32-bit little-endian
            std::memcpy(&countVal_, image + byteOffset, sizeof(countVal_));
            break;
        }
        case EntryType::AnalogInput: {
            // 16-bit little-endian
            int16_t raw;
            std::memcpy(&raw, image + byteOffset, sizeof(raw));
            adcVal_ = raw;
            break;
        }
        // IMU sensor types — read float values from image buffer
        // I2C/SPI adapters write calibrated float values directly into the PDO image
        case EntryType::IMU_GyroX:  { float v; std::memcpy(&v, image + byteOffset, sizeof(v)); gyroXVal_ = v; break; }
        case EntryType::IMU_GyroY:  { float v; std::memcpy(&v, image + byteOffset, sizeof(v)); gyroYVal_ = v; break; }
        case EntryType::IMU_GyroZ:  { float v; std::memcpy(&v, image + byteOffset, sizeof(v)); gyroZVal_ = v; break; }
        case EntryType::IMU_AccelX: { float v; std::memcpy(&v, image + byteOffset, sizeof(v)); accelXVal_ = v; break; }
        case EntryType::IMU_AccelY: { float v; std::memcpy(&v, image + byteOffset, sizeof(v)); accelYVal_ = v; break; }
        case EntryType::IMU_AccelZ: { float v; std::memcpy(&v, image + byteOffset, sizeof(v)); accelZVal_ = v; break; }
        case EntryType::MagnetometerX: { float v; std::memcpy(&v, image + byteOffset, sizeof(v)); magXVal_ = v; break; }
        case EntryType::MagnetometerY: { float v; std::memcpy(&v, image + byteOffset, sizeof(v)); magYVal_ = v; break; }
        case EntryType::MagnetometerZ: { float v; std::memcpy(&v, image + byteOffset, sizeof(v)); magZVal_ = v; break; }
        case EntryType::Barometer: {
            // Barometer has two floats (pressure + altitude) packed sequentially
            float pressure, altitude;
            std::memcpy(&pressure, image + byteOffset, sizeof(float));
            std::memcpy(&altitude, image + byteOffset + sizeof(float), sizeof(float));
            baroPressureVal_ = pressure;
            baroAltitudeVal_ = altitude;
            break;
        }
        // GPS sensor types — read float values from image buffer
        case EntryType::GPS_Latitude:   { float v; std::memcpy(&v, image + byteOffset, sizeof(v)); floatVal_ = v; break; }
        case EntryType::GPS_Longitude:  { float v; std::memcpy(&v, image + byteOffset, sizeof(v)); floatVal_ = v; break; }
        case EntryType::GPS_Altitude:   { float v; std::memcpy(&v, image + byteOffset, sizeof(v)); floatVal_ = v; break; }
        case EntryType::GPS_Heading:    { float v; std::memcpy(&v, image + byteOffset, sizeof(v)); floatVal_ = v; break; }
        case EntryType::GPS_FixQuality: {
            int16_t raw;
            std::memcpy(&raw, image + byteOffset, sizeof(raw));
            adcVal_ = raw;
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
        case EntryType::DigitalOutput: {
            const bool pinState = pulse.tick(common::rt::signalProcessNowNs());
            uint8_t* bytePtr = image + byteOffset;
            if (pinState) {
                *bytePtr |= (1u << bitOffset);
            } else {
                *bytePtr &= ~(1u << bitOffset);
            }
            break;
        }
        case EntryType::AnalogOutput: {
            std::memcpy(image + byteOffset, &adcDesired_, sizeof(adcDesired_));
            break;
        }
        default:
            break;
    }
}

bool PDOEntry::getBool() const noexcept
{
    if (type == EntryType::DigitalInput) return boolVal_;
    if (type == EntryType::DigitalOutput) return pulse.isHighOrLatched();
    return false;
}

void PDOEntry::setBool(bool v) noexcept
{
    if (type == EntryType::DigitalOutput) {
        pulse.arm(v, common::rt::signalProcessNowNs());
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

} // namespace fc::pdo
