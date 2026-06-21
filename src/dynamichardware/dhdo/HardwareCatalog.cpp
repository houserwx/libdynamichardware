#include "dynamichardware/dhdo/HardwareCatalog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <cstdio>
#include <iostream>
#include <string>
#include <functional>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <variant>

namespace dynamichardware::dhdo {

// ============================================================================
// Canonical FNV-1a 64-bit hash — single source of truth for ALL hashing in
// this project.  Used by both makeUuidFromKey() and HardwareCatalog::hashKey().
// Any change to the algorithm MUST be applied here only.
// ============================================================================
static uint64_t fnv1aHash(std::string_view input)
{
    constexpr uint64_t offset_basis = 0xcbf29ce484222325u;
    constexpr uint64_t prime        = 0x00000100000001B3u;

    uint64_t h = offset_basis;
    for (char c : input) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= prime;
    }
    return h;
}

// ---------------------------------------------------------------------------
// UUID generation from canonical key strings.
// Uses FNV-1a to produce a stable RFC-4122 v5-like UUID from the structured key.
// Same key → same UUID across restarts (deterministic, no randomness).
// ---------------------------------------------------------------------------
static std::string makeUuidFromKey(const std::string& input)
{
    // Hash twice with different seeds to get enough entropy for a full UUID.
    uint64_t hash1 = fnv1aHash(input);
    uint64_t hash2 = fnv1aHash(input + "\x01salt_for_entropy");

    uint32_t time_low = static_cast<uint32_t>(hash1 >> 32);
    uint16_t time_mid = static_cast<uint16_t>(hash1 >> 16);
    uint16_t time_hi_and_version = static_cast<uint16_t>(hash1);
    time_hi_and_version = (time_hi_and_version & 0x0FFF) | 0x5000;

    uint8_t clk_seq_hi_res = static_cast<uint8_t>(hash2 >> 56);
    clk_seq_hi_res = (clk_seq_hi_res & 0x3F) | 0x80;
    uint8_t clk_seq_low = static_cast<uint8_t>(hash2 >> 48);
    uint64_t node = hash2 & 0xFFFFFFFFFFFFULL;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << time_low << "-"
       << std::setw(4) << time_mid << "-"
       << std::setw(4) << time_hi_and_version << "-"
       << std::setw(2) << static_cast<int>(clk_seq_hi_res)
       << std::setw(2) << static_cast<int>(clk_seq_low) << "-"
       << std::setw(12) << node;

    return ss.str();
}

// ============================================================================
// Backend-specific detail string builders
// ============================================================================
std::string EthercatBackendData::toDetailString(const std::string& slaveName) const
{
    std::ostringstream oss;
    oss << slaveName << " [pos=" << slavePos << "] PDO 0x"
        << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << pdoIndex
        << ':' << std::setw(2) << static_cast<unsigned>(pdoSubindex);
    return oss.str();
}

std::string GpioBackendData::toDetailString() const
{
    std::ostringstream oss;
    oss << "Line " << lineOffset << " (chip" << chipIndex;
    if (!chipModel.empty()) {
        oss << ' ' << chipModel;
    }
    oss << ')';
    return oss.str();
}

std::string I2cBackendData::toDetailString() const
{
    std::ostringstream oss;
    oss << "bus=" << busId << " addr=0x"
        << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << deviceAddr
        << " reg=0x" << std::setw(2) << registerAddr;
    return oss.str();
}

std::string SpiBackendData::toDetailString() const
{
    std::ostringstream oss;
    oss << "bus=" << busId << " cs=" << static_cast<unsigned>(chipSelect)
        << " reg=0x" << std::hex << std::uppercase << std::setfill('0')
        << std::setw(2) << registerAddr;
    return oss.str();
}

// ============================================================================
// Backend-specific canonical string builders
// Each backend struct produces a deterministic string from its structured fields.
// This string is hashed into UUID — no intermediate "key" stored anywhere.
// Same hardware params → same canonical string → same UUID across restarts.
// ============================================================================
std::string EthercatBackendData::toCanonicalString() const
{
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

std::string GpioBackendData::toCanonicalString() const
{
    std::ostringstream oss;
    oss << "GPIO|" << std::setw(2) << chipIndex
        << '|' << lineOffset;
    return oss.str();
}

std::string I2cBackendData::toCanonicalString() const
{
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0')
        << "I2C|"  << std::setw(2) << busId
        << '|'     << std::setw(2) << deviceAddr
        << '|'     << std::setw(2) << static_cast<unsigned>(registerAddr);
    return oss.str();
}

std::string SpiBackendData::toCanonicalString() const
{
   std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0')
        << "SPI|"  << std::setw(2) << busId
        << '|'     << std::setw(2) << static_cast<unsigned>(chipSelect)
        << '|'     << std::setw(2) << static_cast<unsigned>(registerAddr);
    return oss.str();
}

std::string SimulatedBackendData::toCanonicalString(const std::string& channelId)
{
    // For simulated channels, the channel ID (from JSON "uuid" field or fallback name)
    // IS the canonical identity.
    if (!channelId.empty()) return "SIM|" + channelId;
    return "SIM|default";
}

// ============================================================================
// CatalogEntry::getDetails() — unified view for higher-level code
// ============================================================================
ChannelDetails CatalogEntry::getDetails() const
{
    ChannelDetails details{};
    details.uuid = uuid;
    details.name = name;
    details.channelType = channelType;
    details.isOutput = isOutput;
    details.isSimulated = isSimulated;
    details.backend = backend;

    switch (backend) {
        case BackendType::ETHERCAT: {
            if (auto* d = std::get_if<EthercatBackendData>(&backendData)) {
                details.detailString = d->toDetailString(slaveName);
            }
            break;
        }
        case BackendType::GPIO: {
            if (auto* d = std::get_if<GpioBackendData>(&backendData)) {
                details.detailString = d->toDetailString();
            }
            break;
        }
        case BackendType::I2C: {
            if (auto* d = std::get_if<I2cBackendData>(&backendData)) {
                details.detailString = d->toDetailString();
            }
            break;
        }
        case BackendType::SPI: {
            if (auto* d = std::get_if<SpiBackendData>(&backendData)) {
                details.detailString = d->toDetailString();
            }
            break;
        }
        case BackendType::SIMULATED:
        default:
            details.detailString = "(simulated)";
            break;
    }

    return details;
}

// ============================================================================
// CatalogEntry::makeUuidFromBackend() — hash backend data into deterministic UUID
// Each backend struct has toCanonicalString(); we hash that through fnv1a.
// ============================================================================
std::string CatalogEntry::makeUuidFromBackend(BackendType bt, const BackendSpecificData& bd) const
{
   std::string canonical;
    switch (bt) {
        case BackendType::ETHERCAT: {
            auto* d = std::get_if<EthercatBackendData>(&bd);
            canonical = d ? d->toCanonicalString() : "UNKNOWN|EC";
            break;
        }
        case BackendType::GPIO: {
            auto* d = std::get_if<GpioBackendData>(&bd);
            canonical = d ? d->toCanonicalString() : "UNKNOWN|GPIO";
            break;
        }
        case BackendType::I2C: {
            auto* d = std::get_if<I2cBackendData>(&bd);
            canonical = d ? d->toCanonicalString() : "UNKNOWN|I2C";
            break;
        }
        case BackendType::SPI: {
            auto* d = std::get_if<SpiBackendData>(&bd);
            canonical = d ? d->toCanonicalString() : "UNKNOWN|SPI";
            break;
        }
        default:
            canonical = SimulatedBackendData::toCanonicalString("");
            break;
    }
    return makeUuidFromKey(canonical);
}

// ============================================================================
// HardwareCatalog — private helpers
// ============================================================================
uint64_t HardwareCatalog::hashKey(const std::string_view key)
{
    // Delegate to the single canonical FNV-1a implementation at top of file.
    return fnv1aHash(std::string(key));
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
    // Clear write lock at start of discovery — catalog becomes writable again.
    writeLocked_ = false;

    // Snapshot: all currently-loaded UUIDs are potentially stale.
    // During this cycle, anything registered via addEntry or registerEcChannel
    // will be marked "alive"; anything not re-seen gets purged by purgeStaleEntries().
    aliveUuids_.clear();
    discoveryMode_ = true;
}

size_t HardwareCatalog::purgeStaleEntries()
{
    if (!discoveryMode_) {
        std::printf("[Catalog] Not in discovery mode — skipping purge\n");
        return 0;
    }

    size_t purged = 0;
    auto writeIt = entries_.begin();
    for (auto readIt = entries_.begin(); readIt != entries_.end(); ++readIt) {
        if (aliveUuids_.count(readIt->uuid)) {
            // This UUID was re-registered this cycle — keep it.
            *writeIt++ = *readIt;
        } else {
            // Stale entry (device removed, direction changed, etc.) — remove.
            ++purged;
        }
    }
    entries_.erase(writeIt, entries_.end());

    rebuildIndices();
    discoveryMode_ = false;

    if (purged > 0) {
        std::printf("[Catalog] Purged %zu stale entries (%zu remaining)\n", purged, entries_.size());
    } else {
        std::printf("[Catalog] No stale entries to purge\n");
    }
    return purged;
}

void HardwareCatalog::endDiscovery()
{
    // Purge stale entries first (exits discovery mode internally).
    purgeStaleEntries();

    // Lock catalog against further writes after discovery completes.
    writeLocked_ = true;
    std::printf("[Catalog] Write lock engaged — catalog is now read-only\n");
}

void HardwareCatalog::markAlive(const std::string& uuid)
{
    if (discoveryMode_) {
        aliveUuids_.insert(uuid);
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
     // Write lock guard — reject mutations after discovery ends.
    if (writeLocked_) {
        static int warnCount = 0;
        if (warnCount++ < 3) {
            std::fprintf(stderr, "[Catalog] REJECTED registerEcChannel: catalog is write-locked\n");
        }
        // Return existing entry or static dummy to avoid nullptr deref.
        static CatalogEntry dummy{};
        return entries_.empty() ? dummy : entries_[0];
    }
   // Build backend data struct first — UUID is derived from this.
    EthercatBackendData bd{
        vendorId,         /* vendorId */
        productCode,      /* productCode */
        revisionNumber,   /* revisionNumber */
        slavePos,         /* slavePos */
        pdoIndex,         /* pdoIndex */
        pdoSubindex       /* pdoSubindex */
    };

    // Generate deterministic UUID from structured backend data.
    CatalogEntry temp{};
    temp.backend = BackendType::ETHERCAT;
    temp.backendData = bd;
    const std::string uuid = temp.makeUuidFromBackend(BackendType::ETHERCAT, bd);

    markAlive(uuid);
    auto it = uuidIndex_.find(uuid);
    if (it != uuidIndex_.end()) {
        // Existing entry — reuse to preserve UUID across restarts.
        auto& e = entries_[it->second];
        std::printf("[Catalog]   reused  uuid=%.8s...  %s\n",
                    e.uuid.c_str(), e.name.c_str());
        return e;
    }

    // New entry — stable UUID hashed directly from backend struct fields.
    CatalogEntry e;
    e.uuid           = uuid;
    e.channelType    = channelType;
    e.name           = makeName(slaveName, slavePos, channelType, pdoIndex, pdoSubindex);
    e.slaveName      = slaveName;
    e.backend        = BackendType::ETHERCAT;
    e.backendData    = bd;
    e.isOutput       = isOutput;

    std::printf("[Catalog]   new     uuid=%.8s...  %s\n",
                e.uuid.c_str(), e.name.c_str());

    const size_t idx = entries_.size();
    entries_.push_back(e);
    uuidIndex_[e.uuid]    = idx;
    markAlive(uuid);
    return entries_.back();
}

void HardwareCatalog::addEntry(CatalogEntry entry)
{
    // Write lock guard — reject mutations after discovery ends.
    if (writeLocked_) {
        static int warnCount = 0;
        if (warnCount++ < 3) {
            std::fprintf(stderr, "[Catalog] REJECTED addEntry: catalog is write-locked\n");
        }
        return;
    }

    // Generate deterministic UUID from backend data if not already set.
    if (entry.uuid.empty() || entry.uuid == "00000000-0000-0000-0000-000000000000") {
        entry.uuid = entry.makeUuidFromBackend(entry.backend, entry.backendData);
    }

    markAlive(entry.uuid);
    auto it = uuidIndex_.find(entry.uuid);
    if (it != uuidIndex_.end()) {
        // Existing entry with same UUID — preserve and update.
        entries_[it->second] = std::move(entry);
    } else {
        // New entry — add to catalog.
        const size_t idx = entries_.size();
        uuidIndex_[entry.uuid] = idx;
        entries_.push_back(std::move(entry));
    }
}

// ---------------------------------------------------------------------------
// Lookup — UUID is the sole identity. No "key" field exists.
// ---------------------------------------------------------------------------
const CatalogEntry* HardwareCatalog::findByUuid(const std::string& uuid) const noexcept
{
    auto it = uuidIndex_.find(uuid);
    return (it != uuidIndex_.end()) ? &entries_[it->second] : nullptr;
}

const CatalogEntry* HardwareCatalog::find(uint32_t vendorId, uint32_t productCode) const noexcept
{
   // Backward compatibility: search for EtherCAT entries by vendorId + productCode
    for (const auto& e : entries_) {
        if (e.backend != BackendType::ETHERCAT) continue;
        auto* bd = std::get_if<EthercatBackendData>(&e.backendData);
        if (!bd || bd->productCode != productCode) continue;
        if (bd->vendorId == vendorId) {
            return &e;
        }
    }
    return nullptr;
}

// Legacy stub — kept for backward compat but no longer used internally.
// Consumers should use catalog entry UUIDs directly.
std::string HardwareCatalog::getOrCreateUuid(const std::string& /*uuid*/) noexcept
{
    return "";
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
void HardwareCatalog::rebuildIndices()
{
    uuidIndex_.clear();
    for (size_t i = 0; i < entries_.size(); ++i) {
        uuidIndex_[entries_[i].uuid] = i;
    }
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
