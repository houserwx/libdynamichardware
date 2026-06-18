#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <nlohmann/json.hpp>

// ============================================================================
// HardwareCatalog — persisted registry of all discovered PDO channels.
//
// Backend-agnostic: supports EtherCAT, I2C, SPI, GPIO, gRPC, and any future
// backend type.  Each channel gets a stable UUID derived from backend +
// location + model + channel index.
//
// Key format (backend-specific):
//   EtherCAT: EC|{vendor_id:08X}|{product_code:08X}|REV{revision:08X}|POS{pos:04X}|{pdo_idx:04X}:{pdo_sub:02X}
//   I2C:      I2C|{bus:02X}|{addr:02X}|{channel:02X}
//   SPI:      SPI|{bus:02X}|{cs:02X}|{channel:02X}
//   GPIO:     GPIO|{chip:02X}|{line:04X}
//   gRPC:     GRPC|{channel_name}
//
// Rationale: product_code + revision_number identify the exact card model/fw.
// Position anchors it physically.  Replace a bad card with the same model at
// the same slot → identical key → same UUID → mappings survive.
// Firmware revision is included so that a card upgrade (different revision)
// produces a distinct key and forces an intentional remap.
//
// Usage:
//   1. catalog.load(path)                   — at startup (ok if file absent)
//   2. adapter.setCatalog(&catalog)
//   3. adapter.initialize()                 — registers channels during discovery
//   4. catalog.save(path)                   — immediately after initialize()
//   5. registry.buildUuidMap()              — builds string→PDOEntry* map
// ============================================================================

namespace fc::pdo {

// ---------------------------------------------------------------------------
// CatalogEntry — one PDO channel identified solely by its stable UUID.
// ---------------------------------------------------------------------------
struct CatalogEntry {
    std::string key;           ///< Stable identity key (lookup / persistence)
    std::string uuid;          ///< RFC-4122 v4 UUID, generated once + persisted
    std::string channelType;   ///< "DigitalInput" | "IMU_GyroX" | etc.
    std::string name;          ///< Human-readable: "MPU6050[0x68] GyroX"
    std::string slaveName;     ///< Short model name: "MPU6050", "EL3632"
    uint16_t    slavePos{0};   ///< Backend-specific position (EtherCAT slot, I2C bus, etc.)
    uint32_t    productCode{0};
    uint32_t    revisionNumber{0};
    uint16_t    pdoIndex{0};
    uint8_t     pdoSubindex{0};
    bool        isOutput{false};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(CatalogEntry,
        key, uuid, channelType, name, slaveName,
        slavePos, productCode, revisionNumber,
        pdoIndex, pdoSubindex, isOutput)
};

// ---------------------------------------------------------------------------
// HardwareCatalog — backend-agnostic channel registry.
//
// Usage:
//   1. catalog.load(path)                   — at startup (ok if file absent)
//   2. adapter.setCatalog(&catalog)
//   3. adapter.initialize()                 — registers channels during discovery
//   4. catalog.save(path)                   — immediately after initialize()
//   5. registry.buildUuidMap()              — builds string→PDOEntry* map
//
// On subsequent starts the same keys re-map to the same UUIDs.
// ---------------------------------------------------------------------------
class HardwareCatalog {
public:
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    // ---- Registration (called during EtherCAT / I2C / SPI discovery) ----

    /// Register or look up an EtherCAT PDO channel.
    /// If the key already exists the existing entry is returned unchanged,
    /// preserving UUID across restarts.
    const CatalogEntry& registerEcChannel(
        uint32_t    vendorId,
        uint32_t    productCode,
        uint32_t    revisionNumber,
        uint16_t    slavePos,
        uint16_t    pdoIndex,
        uint8_t     pdoSubindex,
        const std::string& channelType,
        const std::string& slaveName,
        bool        isOutput
    );

    /// Register a new channel entry (generic backend). Generates UUID if key is new.
    void addEntry(CatalogEntry entry);

    // ---- Accessors --------------------------------------------------------

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] const std::vector<CatalogEntry>& entries() const noexcept { return entries_; }

    /// Find by UUID — used to resolve config hardwareUuid references.
    [[nodiscard]] const CatalogEntry* findByUuid(const std::string& uuid) const noexcept;

    /// Find by stable key.
    [[nodiscard]] const CatalogEntry* findByKey(const std::string& key) const noexcept;

    /// Find EtherCAT entry by vendorId + productCode (backward compatibility).
    [[nodiscard]] const CatalogEntry* find(uint32_t vendorId, uint32_t productCode) const noexcept;

    /// Get or generate UUID for a given key.
    [[nodiscard]] std::string getOrCreateUuid(const std::string& key) noexcept;

private:
    std::vector<CatalogEntry>               entries_;
    std::unordered_map<std::string, size_t> keyIndex_;   ///< key  → index
    std::unordered_map<std::string, size_t> uuidIndex_;  ///< uuid → index

    void rebuildIndices();

    /// Build the canonical key string from slave identity + PDO address.
    static std::string makeKey(uint32_t vendorId, uint32_t productCode,
                               uint32_t revisionNumber, uint16_t slavePos,
                               uint16_t pdoIndex, uint8_t pdoSubindex);

    /// Build a human-readable name for a new entry.
    static std::string makeName(const std::string& slaveName, uint16_t slavePos,
                                const std::string& channelType,
                                uint16_t pdoIndex, uint8_t pdoSubindex);

    /// Generate a random RFC-4122 v4 UUID string.
    static std::string generateUuid();
};

} // namespace fc::pdo
