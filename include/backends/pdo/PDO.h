#pragma once

// ============================================================================
// PDO.h — core process-image types (no vtable).
//
// PDOEntry  — one I/O channel.  Maps a bit/byte offset in a process
//             image buffer to a typed value.  All accessor and
//             read/write methods are concrete — no virtual dispatch.
//
// PDO       — owns one uint8_t image[] buffer and the PDOEntries
//             that live inside it.
//
// Lifecycle:
//   Init:    adapter constructs PDOEntry values and pushes them into
//            PDO::entries; adapter sets PDO::image.
//   Freeze:  PDO::freeze() shrinks storage and re-bases entry image
//            pointers.  After freeze nothing may be added or resized.
//   RT:      PDOEntry::read() / write() are the only methods called
//            in the hot path — inlineable, branch-predictable.
// ============================================================================

#include "dynamichardware/rt/SignalProcess.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fc::pdo {

// ------------------------------------------------------------
// EntryType — replaces virtual dispatch for typed accessor logic.
// ------------------------------------------------------------
enum class EntryType : uint8_t {
    DigitalInput  = 0,
    DigitalOutput = 1,
    Encoder       = 2,
    AnalogInput   = 3,
    AnalogOutput  = 4,
    MessageOut    = 5,
    MessageIn     = 6,
    // IMU sensor types (I2C/SPI backends)
    IMU_GyroX     = 7,
    IMU_GyroY     = 8,
    IMU_GyroZ     = 9,
    IMU_AccelX    = 10,
    IMU_AccelY    = 11,
    IMU_AccelZ    = 12,
    MagnetometerX = 13,
    MagnetometerY = 14,
    MagnetometerZ = 15,
    Barometer     = 16,
    // GPS sensor types (UART NMEA, SPI, or I2C backends)
    GPS_Latitude  = 17,
    GPS_Longitude = 18,
    GPS_Altitude  = 19,
    GPS_Heading   = 20,
    GPS_FixQuality = 21,
};

// ------------------------------------------------------------
// PDOEntry — concrete, no vtable, value-type (moveable).
// ------------------------------------------------------------
struct PDOEntry {
    uint8_t* image{nullptr};
    uint32_t byteOffset{0};
    uint8_t  bitOffset{0};
    uint8_t  bitLength{0};
    std::string uuid;
    EntryType type{EntryType::DigitalInput};

    common::rt::DebounceMachine debounce;
    common::rt::PulseMachine    pulse;

    // Message slot (MessageOut / MessageIn entries only)
    struct MessageSlot {
        alignas(8) uint8_t data[64]{};
        uint8_t            size   {0};
        bool               pending{false};
    };
    MessageSlot msgSlot_;

    template<typename T>
    void armOutMessage(const T& msg) noexcept {
        static_assert(sizeof(T) <= sizeof(msgSlot_.data));
        std::memcpy(msgSlot_.data, &msg, sizeof(T));
        msgSlot_.size    = static_cast<uint8_t>(sizeof(T));
        msgSlot_.pending = true;
    }

    template<typename T>
    [[nodiscard]] bool tryConsumeOutMessage(T& out) noexcept {
        if (!msgSlot_.pending) return false;
        std::memcpy(&out, msgSlot_.data, sizeof(T));
        msgSlot_.pending = false;
        return true;
    }

    void setInMessageRaw(const void* data, uint8_t size) noexcept {
        std::memcpy(msgSlot_.data, data, size);
        msgSlot_.size    = size;
        msgSlot_.pending = true;
    }

    template<typename T>
    [[nodiscard]] bool tryGetInMessage(T& out) const noexcept {
        if (!msgSlot_.pending) return false;
        std::memcpy(&out, msgSlot_.data, sizeof(T));
        return true;
    }

    [[nodiscard]] bool hasInMessage()   const noexcept { return msgSlot_.pending; }
    void               consumeInMessage()     noexcept { msgSlot_.pending = false; }

    void configurePulseMs   (uint32_t ms) noexcept { pulse.configure(ms);   }
    void configureDebounceMs(uint32_t ms) noexcept { debounce.configure(ms); }

    // RT hot path
    void read()  noexcept;
    void write() noexcept;

    // Application accessors
    [[nodiscard]] bool    getBool()            const noexcept;
    void                  setBool(bool v)            noexcept;
    [[nodiscard]] int64_t getCount()           const noexcept { return countVal_; }
    [[nodiscard]] int16_t getRawAdc()          const noexcept { return adcVal_;   }
    void                  setRawAdc(int16_t v)       noexcept { adcDesired_ = v;  }

    // IMU sensor accessors (I2C/SPI backends)
    [[nodiscard]] float getGyroX()  const noexcept { return gyroXVal_; }
    [[nodiscard]] float getGyroY()  const noexcept { return gyroYVal_; }
    [[nodiscard]] float getGyroZ()  const noexcept { return gyroZVal_; }
    [[nodiscard]] float getAccelX() const noexcept { return accelXVal_; }
    [[nodiscard]] float getAccelY() const noexcept { return accelYVal_; }
    [[nodiscard]] float getAccelZ() const noexcept { return accelZVal_; }
    [[nodiscard]] float getMagX()   const noexcept { return magXVal_; }
    [[nodiscard]] float getMagY()   const noexcept { return magYVal_; }
    [[nodiscard]] float getMagZ()   const noexcept { return magZVal_; }
    [[nodiscard]] float getBaroPressure() const noexcept { return baroPressureVal_; }
    [[nodiscard]] float getBaroAltitude() const noexcept { return baroAltitudeVal_; }

    // Generic float accessor (used by GPS and any future float-type entries)
    [[nodiscard]] float getFloat() const noexcept { return floatVal_; }
    void setFloat(float v) noexcept { floatVal_ = v; }

    void setGyroX(float v) noexcept { gyroXVal_ = v; }
    void setGyroY(float v) noexcept { gyroYVal_ = v; }
    void setGyroZ(float v) noexcept { gyroZVal_ = v; }
    void setAccelX(float v) noexcept { accelXVal_ = v; }
    void setAccelY(float v) noexcept { accelYVal_ = v; }
    void setAccelZ(float v) noexcept { accelZVal_ = v; }
    void setMagX(float v) noexcept { magXVal_ = v; }
    void setMagY(float v) noexcept { magYVal_ = v; }
    void setMagZ(float v) noexcept { magZVal_ = v; }
    void setBaroPressure(float v) noexcept { baroPressureVal_ = v; }
    void setBaroAltitude(float v) noexcept { baroAltitudeVal_ = v; }

private:
    bool    boolVal_{false};
    int64_t countVal_{0};
    int16_t adcVal_{0};
    int16_t adcDesired_{0};

    // IMU sensor cached values (written by I2C/SPI adapters during readAll())
    float gyroXVal_{0.0f};
    float gyroYVal_{0.0f};
    float gyroZVal_{0.0f};
    float accelXVal_{0.0f};
    float accelYVal_{0.0f};
    float accelZVal_{0.0f};
    float magXVal_{0.0f};
    float magYVal_{0.0f};
    float magZVal_{0.0f};
    float baroPressureVal_{0.0f};
    float baroAltitudeVal_{0.0f};

    // Generic float value (used by GPS and any future float-type entries)
    float floatVal_{0.0f};
};

// ------------------------------------------------------------
// PDO — owns an image buffer and the entries that live in it.
//
// Lifecycle:
//   Init:    adapter constructs PDOEntry values and pushes them into
//            PDO::entries; adapter sets PDO::image (or leaves it empty
//            if image is owned by the backend, e.g. EtherCAT domainData).
//   Freeze:  PDO::freeze() shrinks storage and re-bases entry image
//            pointers.  If image.empty(), entry image pointers are left
//            untouched (they already point into backend-owned memory).
//   RT:      PDOEntry::read() / write() are the only methods called
//            in the hot path — inlineable, branch-predictable.
// ------------------------------------------------------------
struct PDO {
    std::vector<uint8_t>  image;
    std::vector<PDOEntry> entries;

    void freeze();
};

} // namespace fc::pdo
