#pragma once
#include "dynamichardware/pdo/IHardwareAdapter.h"
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dynamichardware::pdo {

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
//   2. buildUuidMap()   — build UUID → PDOEntry* map so that
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
    void addBackend(std::unique_ptr<IHardwareAdapter> adapter);

    // Build the UUID → PDOEntry* map from all currently registered backends.
    void buildUuidMap();

    // Freeze all backend PDOs and rebuild UUID map.
    void freezeForRt();

    // --- Init-time UUID lookup (NOT RT-safe) ------------------------
    [[nodiscard]] PDOEntry*       lookupByUuid(std::string_view uuid) noexcept;
    [[nodiscard]] const PDOEntry* lookupByUuid(std::string_view uuid) const noexcept;

    // --- RT cycle (noexcept) — call in order each cycle -------------
    void readAll() noexcept;
    void writeAll() noexcept;

    // --- Backend health monitoring -----------------------------------
    /// Returns number of backends currently registered.
    [[nodiscard]] std::size_t backendCount() const noexcept { return backends_.size(); }

    /// Returns true if all backends report healthy communication.
    /// Each backend's isFullyCommunicating() is checked (non-RT safe, init-time only).
    [[nodiscard]] bool allBackendsHealthy() const noexcept;

    // --- Debug -------------------------------------------------------
    [[nodiscard]] std::size_t entryCount() const noexcept;
    void printState() const;

    [[nodiscard]] bool isFrozen() const noexcept { return frozen_; }

private:
    // Classify EntryType for RT sweep filtering.
    // isInputEntryType  → entry should be read during readAll().
    // isOutputEntryType → entry should be written during writeAll().
    // Message types are handled exclusively by adapter hooks (not swept).
    [[nodiscard]] static bool isInputEntryType(EntryType t) noexcept {
        switch (t) {
            case EntryType::BoolInput:
            case EntryType::Int32Input:
            case EntryType::Int16Input:
            case EntryType::FloatInput:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] static bool isOutputEntryType(EntryType t) noexcept {
        switch (t) {
            case EntryType::BoolOutput:
            case EntryType::Int16Output:
            case EntryType::FloatOutput:
                return true;
            default:
                return false;
        }
    }

    std::vector<std::unique_ptr<IHardwareAdapter>> backends_;
    std::unordered_map<std::string, PDOEntry*>     uuidMap_;
    bool                                            frozen_{false};
};

} // namespace dynamichardware::pdo
