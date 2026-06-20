#include "dynamichardware/dhdo/HardwareCatalog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <cstdio>

namespace dynamichardware::dhdo {

// ---------------------------------------------------------------------------
// Identity: .uuid == .key — the key IS the permanent identifier.
// No random UUIDs; structured keys are stable, human-readable, and match
// across discovery → RT backend rebuild (same vendor/product/pos/channel).
// ---------------------------------------------------------------------------

uint64_t HardwareCatalog::hashKey(const std::string_view key)
{
    // FNV-1a 64-bit hash — fast, well-distributed, no collisions on our key space.
    constexpr uint64_t offset_basis = 0xcbf29ce484222325u;
    constexpr uint64_t prime        = 0x00000100000001B3u;

    uint64_t h = offset_basis;
    for (char c : key) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= prime;
    }
    return h;
}

// ---------------------------------------------------------------------------
// Load / Save
// ---------------------------------------------------------------------------
bool HardwareCatalog::load(const std::string& path)
{
    std::ifstream f(path);
    if (!f) {
        // File absent on first run — perfectly normal.
        std::printf("[Catalog] No existing catalog at '%s' — starting fresh\n", path.c_str());
        return true;
    }
    try {
        nlohmann::json j;
        f >> j;
        entries_ = j.value("channels", std::vector<CatalogEntry>{});
        rebuildIndices();
        std::printf("[Catalog] Loaded %zu entries from '%s'\n",
                    entries_.size(), path.c_str());
        return true;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[Catalog] Parse error loading '%s': %s\n", path.c_str(), ex.what());
        return false;
    }
}

bool HardwareCatalog::save(const std::string& path) const
{
    try {
        nlohmann::json j;
        j["channels"] = entries_;

        std::ofstream f(path);
        if (!f) {
            std::fprintf(stderr, "[Catalog] Cannot open '%s' for writing\n", path.c_str());
            return false;
        }
        f << j.dump(2) << '\n';
        std::printf("[Catalog] Saved %zu entries to '%s'\n", entries_.size(), path.c_str());
        return true;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[Catalog] Save error: %s\n", ex.what());
        return false;
    }
}

// ---------------------------------------------------------------------------
// Discovery lifecycle
// ---------------------------------------------------------------------------
void HardwareCatalog::beginDiscovery()
{
    // Snapshot: all currently-loaded keys are potentially stale.
    // During this cycle, anything registered via addEntry or registerEcChannel
    // will be marked "alive"; anything not re-seen gets purged by purgeStaleEntries().
    aliveKeys_.clear();
    discoveryMode_ = true;
}

size_t HardwareCatalog::purgeStaleEntries()
{
    if (!discoveryMode_) {
        std::printf("[Catalog] Not in discovery mode — skipping purge\n");
        return 0;
    }
    discoveryMode_ = false;

    size_t purged = 0;
    auto writeIt = entries_.begin();
    for (auto readIt = entries_.begin(); readIt != entries_.end(); ++readIt) {
        if (aliveKeys_.count(readIt->key)) {
            // This key was re-registered this cycle — keep it.
            *writeIt++ = *readIt;
        } else {
            // Stale entry (device removed, direction changed, etc.) — remove.
            ++purged;
        }
    }
    entries_.erase(writeIt, entries_.end());

    rebuildIndices();
    if (purged > 0) {
        std::printf("[Catalog] Purged %zu stale entries (%zu remaining)\n", purged, entries_.size());
    } else {
        std::printf("[Catalog] No stale entries to purge\n");
    }
    return purged;
}

void HardwareCatalog::markAlive(const std::string& key)
{
    if (discoveryMode_) {
        aliveKeys_.insert(key);
    }
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
const CatalogEntry& HardwareCatalog::registerEcChannel(
    uint32_t    vendorId,
    uint32_t    productCode,
    uint32_t    revisionNumber,
    uint16_t    slavePos,
    uint16_t    pdoIndex,
    uint8_t     pdoSubindex,
    const std::string& channelType,
    const std::string& slaveName,
    bool        isOutput
)
{
    const std::string key = makeKey(vendorId, productCode, revisionNumber,
                                    slavePos, pdoIndex, pdoSubindex);

    markAlive(key);
    auto it = keyIndex_.find(key);
    if (it != keyIndex_.end()) {
        // Existing entry — reuse to preserve UUID across restarts.
        auto& e = entries_[it->second];
        std::printf("[Catalog]   reused  uuid=%.8s...  %s\n",
                    e.uuid.c_str(), e.name.c_str());
        return e;
    }

    // New entry — uuid is the key itself (stable identity).
    CatalogEntry e;
    e.key            = key;
    e.uuid           = key;  // <-- key IS the identifier
    e.channelType    = channelType;
    e.name           = makeName(slaveName, slavePos, channelType, pdoIndex, pdoSubindex);
    e.slaveName      = slaveName;
    e.slavePos       = slavePos;
    e.productCode    = productCode;
    e.revisionNumber = revisionNumber;
    e.pdoIndex       = pdoIndex;
    e.pdoSubindex    = pdoSubindex;
    e.isOutput       = isOutput;

    std::printf("[Catalog]   new     uuid=%.8s...  %s\n",
                e.uuid.c_str(), e.name.c_str());

    const size_t idx = entries_.size();
    entries_.push_back(e);
    keyIndex_[key]        = idx;
    uuidIndex_[e.uuid]    = idx;
    markAlive(key);
    return entries_.back();
}

void HardwareCatalog::addEntry(CatalogEntry entry)
{
    markAlive(entry.key);
    auto it = keyIndex_.find(entry.key);
    if (it != keyIndex_.end()) {
        if (entry.uuid.empty()) {
            entry.uuid = entries_[it->second].uuid;
        }
        // Ensure uuid == key for consistency.
        entry.uuid = entry.key;
        entries_[it->second] = std::move(entry);
    } else {
        // Default: uuid is the key itself.
        if (entry.uuid.empty()) {
            entry.uuid = entry.key;
        } else {
            entry.uuid = entry.key;  // Force uuid == key regardless of what caller passed
        }
        const size_t idx = entries_.size();
        keyIndex_[entry.key]   = idx;
        uuidIndex_[entry.uuid] = idx;
        entries_.push_back(std::move(entry));
    }
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------
const CatalogEntry* HardwareCatalog::findByKey(const std::string& key) const noexcept
{
    auto it = keyIndex_.find(key);
    return (it != keyIndex_.end()) ? &entries_[it->second] : nullptr;
}

const CatalogEntry* HardwareCatalog::findByUuid(const std::string& uuid) const noexcept
{
    // Since .uuid == .key, this is the same as findByKey.
    return findByKey(uuid);
}

const CatalogEntry* HardwareCatalog::find(uint32_t vendorId, uint32_t productCode) const noexcept
{
    // Backward compatibility: search for EtherCAT entries by vendorId + productCode
    for (const auto& e : entries_) {
        if (e.productCode == productCode && e.key.rfind("EC|", 0) == 0) {
            if (e.key.size() > 12) {
                std::string vendorStr = e.key.substr(3, 8);
                uint32_t vid = 0;
                for (char c : vendorStr) {
                    vid = (vid << 4) | (c < '0' ? 0 : ((c < 'A') ? (c - '0') : ((c < 'a') ? (c - 'A' + 10) : (c - 'a' + 10))));
                }
                if (vid == vendorId) {
                    return &e;
                }
            }
        }
    }
    return nullptr;
}

std::string HardwareCatalog::getOrCreateUuid(const std::string& key) noexcept
{
    auto it = keyIndex_.find(key);
    if (it != keyIndex_.end()) {
        return entries_[it->second].key;
    }
    // Key not yet registered — return the key itself as the identity.
    return key;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
void HardwareCatalog::rebuildIndices()
{
    keyIndex_.clear();
    uuidIndex_.clear();
    for (size_t i = 0; i < entries_.size(); ++i) {
        keyIndex_[entries_[i].key]   = i;
        uuidIndex_[entries_[i].uuid] = i;
    }
}

std::string HardwareCatalog::makeKey(
    uint32_t vendorId, uint32_t productCode, uint32_t revisionNumber,
    uint16_t slavePos, uint16_t pdoIndex, uint8_t pdoSubindex)
{
    // EC|{vendor:08X}|{product:08X}|REV{rev:08X}|POS{pos:04X}|{idx:04X}:{sub:02X}
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0')
        << "EC|" << std::setw(8) << vendorId
        << '|'   << std::setw(8) << productCode
        << "|REV" << std::setw(8) << revisionNumber
        << "|POS" << std::setw(4) << slavePos
        << '|'    << std::setw(4) << pdoIndex
        << ':'    << std::setw(2) << static_cast<unsigned>(pdoSubindex);
    return oss.str();
}

std::string HardwareCatalog::makeName(
    const std::string& slaveName, uint16_t slavePos,
    const std::string& channelType,
    uint16_t pdoIndex, uint8_t pdoSubindex)
{
    std::ostringstream oss;
    oss << slaveName << '[' << std::dec << slavePos << "] "
        << channelType
        << " 0x" << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << pdoIndex
        << ':'  << std::setw(2) << static_cast<unsigned>(pdoSubindex);
    return oss.str();
}

} // namespace dynamichardware::dhdo
