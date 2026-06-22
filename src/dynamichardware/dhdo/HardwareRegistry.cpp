#include "dynamichardware/dhdo/HardwareRegistry.h"
#include <cstdio>
#include <stdexcept>

namespace dynamichardware::dhdo {

// ---- Init phase -----------------------------------------------------
// Registry is friend of IRuntimeAdapter so it can iterate mutable dhdos_
// during freeze and RT sweeps.

void HardwareRegistry::addBackend(std::unique_ptr<IRuntimeAdapter> adapter)
{
    if (frozen_) {
        throw std::logic_error("addBackend() after freezeForRt()");
    }
    backends_.push_back(std::move(adapter));
}

void HardwareRegistry::buildUuidMap()
{
    uuidMap_.clear();
    for (const auto& backend : backends_) {
        for (const auto& pdo : backend->getDHDOS()) {
            for (const auto& e : pdo.entries) {
                if (!e.uuid.empty()) {
                    uuidMap_.emplace(e.uuid, const_cast<DHDOEntry*>(&e));
                }
            }
        }
    }
    std::printf("[Registry] UUID map built: %zu backends, %zu UUID-mapped entries\n",
                backends_.size(), uuidMap_.size());
}

void HardwareRegistry::freezeForRt()
{
    // Rebuild UUID map so any backends added after the last buildUuidMap()
    // call (e.g. GrpcAdapters added during queue loading) are included.
    buildUuidMap();

    std::size_t totalEntries = 0;
    for (auto& backend : backends_) {
        for (auto& pdo : backend->dhdos_) {
            // Freeze this PDO: shrink storage and re-base image pointers.
            pdo.freeze();
            totalEntries += pdo.entries.size();
        }
    }

    frozen_ = true;
    std::printf("[Registry] Frozen: %zu backends, %zu total entries, %zu with UUIDs\n",
                backends_.size(), totalEntries, uuidMap_.size());
}

// ---- RT cycle -------------------------------------------------------
// Registry is friend of IRuntimeAdapter so it can iterate mutable dhdos_
// during freeze and RT sweeps.

void HardwareRegistry::readAll() noexcept
{
    for (auto& backend : backends_) {
        // Phase 1: backend pre-read hook fills the process image
        // (EtherCAT: receive + domain_process; Simulated: write synthetic values)
        backend->onBeforeReadInputs();

        // Phase 2: concrete read sweep — latch value from image into entry cache
        // No virtual calls — DHDOEntry::read() is a concrete struct method.
        // Reads all input entry types: DI, Encoder, AI, IMU, GPS, Magnetometer, Barometer.
        for (auto& pdo : backend->dhdos_) {
            for (auto& e : pdo.entries) {
                if (isInputEntryType(e.type)) {
                    e.read();
                }
            }
        }
    }
}

void HardwareRegistry::writeAll() noexcept
{
    for (auto& backend : backends_) {
        // Phase 3: concrete write sweep — flush pulse/desired state into image
        // No virtual calls — DHDOEntry::write() is a concrete struct method.
        // Writes all output entry types: DO, AO.
        for (auto& pdo : backend->dhdos_) {
            for (auto& e : pdo.entries) {
                if (isOutputEntryType(e.type)) {
                    e.write();
                }
            }
        }

        // Phase 4: backend post-write hook flushes the image to hardware
        // (EtherCAT: domain_queue + send; Simulated: no-op)
        backend->onAfterWriteOutputs();
    }
}

// ---- Init-time UUID lookup ------------------------------------------

DHDOEntry* HardwareRegistry::lookupByUuid(std::string_view uuid) noexcept
{
    if (uuid.empty()) return nullptr;
    auto it = uuidMap_.find(std::string{uuid});
    return (it != uuidMap_.end()) ? it->second : nullptr;
}

const DHDOEntry* HardwareRegistry::lookupByUuid(std::string_view uuid) const noexcept
{
    if (uuid.empty()) return nullptr;
    auto it = uuidMap_.find(std::string{uuid});
    return (it != uuidMap_.end()) ? it->second : nullptr;
}

// ---- Health monitoring ----------------------------------------------

bool HardwareRegistry::allBackendsHealthy() const noexcept
{
    if (backends_.empty()) return true;
    // For now: if backends are registered, consider healthy.
    // Future: check each backend's isFullyCommunicating() or equivalent.
    return true;
}

// ---- Cycle period control -------------------------------------------

void HardwareRegistry::setGlobalCyclePeriod(uint64_t nanoseconds)
{
    if (!nanoseconds || nanoseconds > 1'000'000'000ULL) { // Sanity: 1ns–1s range
        std::printf("[warn] HardwareRegistry::setGlobalCyclePeriod(%llu) out of sane range — ignoring\n",
                    static_cast<unsigned long long>(nanoseconds));
        return;
    }
    for (auto& backend : backends_) {
        uint64_t oldNs = backend->getCyclePeriod();
        backend->setCyclePeriod(nanoseconds);
        uint64_t newNs = backend->getCyclePeriod();
        if (newNs != oldNs) {
            std::printf("[info] Backend cycle period updated: %llu ns → %llu ns\n",
                        static_cast<unsigned long long>(oldNs),
                        static_cast<unsigned long long>(newNs));
        }
    }
}

uint64_t HardwareRegistry::getEffectiveCyclePeriod() const noexcept
{
    for (const auto& backend : backends_) {
        uint64_t ns = backend->getCyclePeriod();
        if (ns > 0) return ns;   // First non-zero wins
    }
    return 0;
}

// ---- Debug ----------------------------------------------------------

std::size_t HardwareRegistry::entryCount() const noexcept
{
    std::size_t n = 0;
    for (const auto& backend : backends_) {
        for (const auto& pdo : backend->getDHDOS()) {
            n += pdo.entries.size();
        }
    }
    return n;
}

void HardwareRegistry::printState() const
{
    for (const auto& backend : backends_) {
        for (const auto& pdo : backend->getDHDOS()) {
            for (const auto& e : pdo.entries) {
                switch (e.type) {
                case EntryType::BoolInput:
                    std::printf("  BoolIn   uuid=%-40s  state=%s\n",
                                e.uuid.c_str(), e.getBool() ? "HIGH" : "low ");
                    break;
                case EntryType::BoolOutput:
                    std::printf("  BoolOut  uuid=%-40s  state=%s\n",
                                e.uuid.c_str(), e.getBool() ? "ON " : "off");
                    break;
                case EntryType::Int32Input:
                    std::printf("  Int32In  uuid=%-40s  value=%-12d\n",
                                e.uuid.c_str(), static_cast<int>(e.getInt32()));
                    break;
                case EntryType::Int16Input:
                    std::printf("  Int16In  uuid=%-40s  value=%-6d\n",
                                e.uuid.c_str(), static_cast<int>(e.getInt16()));
                    break;
                case EntryType::FloatInput:
                    std::printf("  FloatIn  uuid=%-40s  value=%-12.4f\n",
                                e.uuid.c_str(), e.getFloat());
                    break;
                default:
                    break;
                }
            }
        }
    }
}

} // namespace dynamichardware::dhdo
