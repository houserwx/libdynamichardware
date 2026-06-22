#pragma once
#include "dynamichardware/dhdo/IRuntimeAdapter.h"
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dynamichardware::dhdo {

// ============================================================
// HardwareRegistry — owns backends, orchestrates the RT cycle,
// and provides UUID-keyed init-time channel resolution.
//
// RT DESIGN PATTERN: Init-Time Lookup Map, RT-Time Direct Iteration
//
// This class uses std::unordered_map for UUID resolution at init time,
// but the RT path (readAll/writeAll) NEVER touches the map.
//
// Lifecycle:
//   1. addBackend()     — register hardware backends.
//   2. buildUuidMap()   — build UUID → DHDOEntry* map so that
//                         Queue::loadFromJson() can call lookupByUuid().
//                         addBackend() is still permitted after this
//                         call (GrpcAdapters are added during queue
//                         loading, after the static hardware map
//                         has been built).
//   3. Queue::loadFromJson() — may call addBackend() for per-queue
//                         GrpcAdapters during this phase.
//   4. freezeForRt()    — rebuilds UUID map (includes all backends now),
//                         freezes all PDOs; no structural changes allowed
//                         after this point.
//   5. lookupByUuid()   — init-time only; resolves UUIDs during
//                         Queue::loadFromJson / WrapperPool construction.
//   6. readAll()/writeAll() — one RT cycle (noexcept, no map access).
//
// RT-THREAD INVARIANT: readAll(), writeAll() must be called from the
// same single RT thread.  lookupByUuid() is init-time (single-threaded
// setup phase) and must NOT be called from the RT loop.
// ============================================================
class HardwareRegistry {
public:
    // --- Init phase -------------------------------------------------

    // Transfer ownership of a backend. Must be called before freezeForRt().
    void addBackend(std::unique_ptr<IRuntimeAdapter> adapter);

    // Build the UUID → DHDOEntry* map from all currently registered backends.
    void buildUuidMap();

    // Freeze all backend PDOs and rebuild UUID map.
    void freezeForRt();

    // --- Init-time UUID lookup (NOT RT-safe) ------------------------
    [[nodiscard]] DHDOEntry*       lookupByUuid(std::string_view uuid) noexcept;
    [[nodiscard]] const DHDOEntry* lookupByUuid(std::string_view uuid) const noexcept;

    // --- RT cycle (noexcept) — call in order each cycle -------------
    void readAll() noexcept;
    void writeAll() noexcept;

    // --- Backend health monitoring -----------------------------------
    /// Returns number of backends currently registered.
    [[nodiscard]] std::size_t backendCount() const noexcept { return backends_.size(); }

    /// Returns true if all backends report healthy communication.
    /// Each backend's isFullyCommunicating() is checked (non-RT safe, init-time only).
    [[nodiscard]] bool allBackendsHealthy() const noexcept;

    // --- Cycle period control (runtime-adjustable) ---------------------
    /// Set target cycle period across ALL registered backends.
    /// Each backend's setCyclePeriod() hook is called — timing-aware backends
    /// (EtherCAT DC, Simulated) use the new value immediately for phase math.
    /// No-op backends (GPIO/I²C/SPI) ignore silently via default virtual hook.
    void setGlobalCyclePeriod(uint64_t nanoseconds);

    /// Query current cycle period from first non-zero backend, or return 0 if none set.
    [[nodiscard]] uint64_t getEffectiveCyclePeriod() const noexcept;

    // --- Debug -------------------------------------------------------
    [[nodiscard]] std::size_t entryCount() const noexcept;
    void printState() const;

    [[nodiscard]] bool isFrozen() const noexcept { return frozen_; }

private:
    // Classify EntryType for RT sweep filtering.
    // isInputEntryType  → entry should be read during readAll().
    // isOutputEntryType → entry should be written during writeAll().
    // Message types are handled exclusively by adapter hooks (not swept).
    /// Classify by bitmask — future-proof against new EntryType additions.
    /// Direction lives in bits [1-0] (DIR_INPUT=0x01, DIR_OUTPUT=0x02).
    /// constexpr enables use in template constraints and static_assert contexts.
    [[nodiscard]] static constexpr bool isInputEntryType(EntryType t) noexcept {
        uint8_t dir = static_cast<uint8_t>(t) & 0x03; // Extract direction bits
        return dir == DIR_INPUT &&
               ((static_cast<uint8_t>(t) & BASE_MSG) != BASE_MSG); // Exclude message types
    }

    [[nodiscard]] static constexpr bool isOutputEntryType(EntryType t) noexcept {
        uint8_t dir = static_cast<uint8_t>(t) & 0x03; // Extract direction bits
        return dir == DIR_OUTPUT &&
               ((static_cast<uint8_t>(t) & BASE_MSG) != BASE_MSG); // Exclude message types
    }

    std::vector<std::unique_ptr<IRuntimeAdapter>> backends_;
    std::unordered_map<std::string, DHDOEntry*>     uuidMap_;
    bool                                            frozen_{false};
};

} // namespace dynamichardware::dhdo
