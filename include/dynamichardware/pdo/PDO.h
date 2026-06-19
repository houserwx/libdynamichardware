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

namespace dynamichardware::pdo {

// ------------------------------------------------------------
// EntryFlag — bitmask fields for composing EntryType.
//
// EntryType is a composable bitmask: direction | signedness | base | size
// Any combination is valid, so we don't need a new enum value per combo.
//
//   Bit  [1-0]   Direction:    INPUT=0x01, OUTPUT=0x02
//   Bit  [2]     Signedness:   SIGNED=0x04 (for INT base; ignored for BOOL/FLOAT)
//   Bits [4-3]   Base type:    BOOL=0x00, INT=0x08, FLOAT=0x10, MSG=0x18
//   Bits [6-5]   Bit size:     SZ_1=0x00, SZ_8=0x20, SZ_16=0x40, SZ_32=0x60
//   Bit  [7]     Reserved
//
// Domain semantics (IMU_GyroX, Encoder_A, etc.) live in the catalog
// channelType string and in application-layer composites.
// ------------------------------------------------------------
// Direction (bits 0-1)
constexpr uint8_t DIR_INPUT   = 0x01;
constexpr uint8_t DIR_OUTPUT  = 0x02;
// Signedness (bit 2)
constexpr uint8_t SIGNED      = 0x04;
// Base type (bits 3-4)
constexpr uint8_t BASE_BOOL   = 0x00;
constexpr uint8_t BASE_INT    = 0x08;
constexpr uint8_t BASE_FLOAT  = 0x10;
constexpr uint8_t BASE_MSG    = 0x18;
// Bit size (bits 5-6)
constexpr uint8_t SZ_1        = 0x00;
constexpr uint8_t SZ_8        = 0x20;
constexpr uint8_t SZ_16       = 0x40;
constexpr uint8_t SZ_32       = 0x60;

// Pre-composed convenience constants (the values users actually reference)
enum EntryType : uint8_t {
    // Bool I/O
    BoolInput    = DIR_INPUT  | BASE_BOOL  | SZ_1,   // 0x01
    BoolOutput   = DIR_OUTPUT | BASE_BOOL  | SZ_1,   // 0x02

    // Integer inputs (signed is the common hardware default)
    Int8Input    = DIR_INPUT  | SIGNED | BASE_INT | SZ_8,
    Int16Input   = DIR_INPUT  | SIGNED | BASE_INT | SZ_16,
    Int32Input   = DIR_INPUT  | SIGNED | BASE_INT | SZ_32,

    // Integer outputs
    Int8Output   = DIR_OUTPUT | SIGNED | BASE_INT | SZ_8,
    Int16Output  = DIR_OUTPUT | SIGNED | BASE_INT | SZ_16,
    Int32Output  = DIR_OUTPUT | SIGNED | BASE_INT | SZ_32,

    // Float I/O (always signed, 32-bit)
    FloatInput   = DIR_INPUT  | BASE_FLOAT | SZ_32,   // 0x71
    FloatOutput  = DIR_OUTPUT | BASE_FLOAT | SZ_32,   // 0x72

    // Message channels (no value format in process image)
    MessageIn    = DIR_INPUT  | BASE_MSG,   // 0x19
    MessageOut   = DIR_OUTPUT | BASE_MSG,   // 0x1A
};

// Bitmask extractors — constexpr, inlineable, zero-cost
constexpr bool    entryIsInput(uint8_t t)     noexcept { return t & DIR_INPUT;  }
constexpr bool    entryIsOutput(uint8_t t)    noexcept { return t & DIR_OUTPUT; }
constexpr bool    entryIsMessage(uint8_t t)   noexcept { return (t & 0x18) == BASE_MSG; }
constexpr uint8_t entryValueFormat(uint8_t t) noexcept { return t & 0x78; }
constexpr uint8_t entryBitSize(uint8_t t)     noexcept { return t & 0x60; }
constexpr bool    entryIsSigned(uint8_t t)    noexcept { return t & SIGNED; }

// ------------------------------------------------------------
// PDOEntry — concrete, no vtable, value-type (moveable).
// ------------------------------------------------------------
struct PDOEntry {
    uint8_t* image{nullptr};
    uint32_t byteOffset{0};
    uint8_t  bitOffset{0};
    uint8_t  bitLength{0};
    std::string uuid;
    EntryType type{EntryType::BoolInput};

    dynamichardware::rt::DebounceMachine debounce;
    dynamichardware::rt::PulseMachine    pulse;

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

    // ---- Application accessors (typed by value format) ----
    // The catalog's channelType string provides domain semantics.

    [[nodiscard]] bool    getBool()    const noexcept;  // BoolInput / BoolOutput
    void                  setBool(bool v)       noexcept;  // BoolOutput
    [[nodiscard]] int32_t getInt32()   const noexcept { return int32Val_; }  // Int32Input
    [[nodiscard]] int16_t getInt16()   const noexcept { return int16Val_; }  // Int16Input
    void                  setInt16(int16_t v)    noexcept { int16Desired_ = v; }  // Int16Output
    [[nodiscard]] float   getFloat()   const noexcept { return floatVal_; }  // FloatInput
    void                  setFloat(float v)       noexcept { floatDesired_ = v; }  // FloatOutput

private:
    // Read-side cache — updated by read()
    bool    boolVal_{false};
    int32_t int32Val_{0};
    int16_t int16Val_{0};
    float   floatVal_{0.0f};

    // Write-side desired state
    int16_t int16Desired_{0};
    float   floatDesired_{0.0f};
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

} // namespace dynamichardware::pdo
