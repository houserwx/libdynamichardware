#include "dynamichardware/pdo/HardwareCatalog.h"

#include <fstream>
#include <random>
#include <iomanip>
#include <sstream>
#include <cstdio>

namespace dynamichardware::pdo {

// ---------------------------------------------------------------------------
// UUID generation (RFC-4122 v4 implementation)
// ---------------------------------------------------------------------------
std::string HardwareCatalog::generateUuid()
{
    std::random_device rdev;
    std::mt19937 gen(rdev());
    std::uniform_int_distribution<uint32_t> dist(0U, 0xFFFFFFFFU);

    const uint32_t part1 = dist(gen);
    const auto     part2 = static_cast<uint16_t>(dist(gen) & 0xFFFFU);
    const auto     part3 = static_cast<uint16_t>((dist(gen) & 0x0FFFU) | 0x4000U); // v4
    const auto     part4 = static_cast<uint16_t>((dist(gen) & 0x3FFFU) | 0x8000U); // variant 1
    const uint32_t eHi   = dist(gen);
    const auto     eLo   = static_cast<uint16_t>(dist(gen) & 0xFFFFU);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << part1 << '-'
        << std::setw(4) << part2 << '-'
        << std::setw(4) << part3 << '-'
        << std::setw(4) << part4 << '-'
        << std::setw(8) << eHi
        << std::setw(4) << eLo;
    return oss.str();
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

    auto it = keyIndex_.find(key);
    if (it != keyIndex_.end()) {
        // Existing entry — return as-is to preserve UUID.
        auto& e = entries_[it->second];
        std::printf("[Catalog]   reused  uuid=%.8s...  %s\n",
                    e.uuid.c_str(), e.name.c_str());
        return e;
    }

    // New entry — assign fresh UUID.
    CatalogEntry e;
    e.key            = key;
    e.uuid           = generateUuid();
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
    return entries_.back();
}

void HardwareCatalog::addEntry(CatalogEntry entry)
{
    auto it = keyIndex_.find(entry.key);
    if (it != keyIndex_.end()) {
        if (entry.uuid.empty()) {
            entry.uuid = entries_[it->second].uuid;
        }
        entries_[it->second] = std::move(entry);
    } else {
        if (entry.uuid.empty()) {
            entry.uuid = generateUuid();
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
    auto it = uuidIndex_.find(uuid);
    return (it != uuidIndex_.end()) ? &entries_[it->second] : nullptr;
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
        return entries_[it->second].uuid;
    }
    return generateUuid();
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

} // namespace dynamichardware::pdo
