#include "backends/pdo/HardwareRegistry.h"
#include <cstdio>
#include <stdexcept>

namespace fc::pdo {

void HardwareRegistry::addBackend(std::unique_ptr<IHardwareAdapter> adapter)
{
    if (frozen_) {
        throw std::logic_error("addBackend() after freezeForRt()");
    }
    backends_.push_back(std::move(adapter));
}

void HardwareRegistry::buildUuidMap()
{
    uuidMap_.clear();
    for (auto& backend : backends_) {
        for (auto& pdo : backend->getPDOs()) {
            for (auto& e : pdo.entries) {
                if (!e.uuid.empty()) {
                    uuidMap_.emplace(e.uuid, &e);
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
        for (auto& pdo : backend->getPDOs()) {
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

void HardwareRegistry::readAll() noexcept
{
    for (auto& backend : backends_) {
        // Phase 1: backend pre-read hook fills the process image
        // (EtherCAT: receive + domain_process; Simulated: write synthetic values)
        backend->onBeforeReadInputs();

        // Phase 2: concrete read sweep — latch value from image into entry cache
        // No virtual calls — PDOEntry::read() is a concrete struct method.
        for (auto& pdo : backend->getPDOs()) {
            for (auto& e : pdo.entries) {
                e.read();
            }
        }
    }
}

void HardwareRegistry::writeAll() noexcept
{
    for (auto& backend : backends_) {
        // Phase 3: concrete write sweep — flush pulse/desired state into image
        // No virtual calls — PDOEntry::write() is a concrete struct method.
        for (auto& pdo : backend->getPDOs()) {
            for (auto& e : pdo.entries) {
                e.write();
            }
        }

        // Phase 4: backend post-write hook flushes the image to hardware
        // (EtherCAT: domain_queue + send; Simulated: no-op)
        backend->onAfterWriteOutputs();
    }
}

// ---- Init-time UUID lookup ------------------------------------------

PDOEntry* HardwareRegistry::lookupByUuid(std::string_view uuid) noexcept
{
    if (uuid.empty()) return nullptr;
    auto it = uuidMap_.find(std::string{uuid});
    return (it != uuidMap_.end()) ? it->second : nullptr;
}

const PDOEntry* HardwareRegistry::lookupByUuid(std::string_view uuid) const noexcept
{
    if (uuid.empty()) return nullptr;
    auto it = uuidMap_.find(std::string{uuid});
    return (it != uuidMap_.end()) ? it->second : nullptr;
}

// ---- Debug ----------------------------------------------------------

std::size_t HardwareRegistry::entryCount() const noexcept
{
    std::size_t n = 0;
    for (const auto& backend : backends_) {
        for (const auto& pdo : backend->getPDOs()) {
            n += pdo.entries.size();
        }
    }
    return n;
}

void HardwareRegistry::printState() const
{
    for (const auto& backend : backends_) {
        for (const auto& pdo : backend->getPDOs()) {
            for (const auto& e : pdo.entries) {
                switch (e.type) {
                case EntryType::Encoder:
                    std::printf("  Encoder  uuid=%-40s  count=%-8ld\n",
                                e.uuid.c_str(),
                                static_cast<long>(e.getCount()));
                    break;
                case EntryType::DigitalInput:
                    std::printf("  DI       uuid=%-40s  state=%s\n",
                                e.uuid.c_str(), e.getBool() ? "HIGH" : "low ");
                    break;
                case EntryType::DigitalOutput:
                    std::printf("  DO       uuid=%-40s  state=%s\n",
                                e.uuid.c_str(), e.getBool() ? "ON " : "off");
                    break;
                case EntryType::AnalogInput:
                    std::printf("  AI       uuid=%-40s  raw=%-6d\n",
                                e.uuid.c_str(),
                                static_cast<int>(e.getRawAdc()));
                    break;
                default:
                    break;
                }
            }
        }
    }
}

} // namespace fc::pdo
