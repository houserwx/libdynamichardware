#pragma once
#include <array>
#include <climits>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

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
//   3. adapter.discover()                   — populates catalog from hardware scan
//   4. catalog.save(path)                   — immediately after discover()
//   5. adapter.buildRT()                    — construct PDOs and activate backend
//   6. registry.addBackend(adapter)         — transfer ownership to registry
//   7. registry.buildUuidMap()              — builds string→DHDOEntry* map
// ==============================================================================

namespace dynamichardware::dhdo {

// ---------------------------------------------------------------------------
// BackendType — which backend populated this catalog entry.
// ---------------------------------------------------------------------------
enum class BackendType : uint8_t {
    UNKNOWN = 0,
    ETHERCAT,
    GPIO,
    I2C,
    SPI,
    SIMULATED,
};

// ---------------------------------------------------------------------------
// Backend-specific data structs — each backend's unique fields, kept separate.
// Higher-level code never touches these directly; use getDetails() instead.
// ---------------------------------------------------------------------------
struct EthercatBackendData {
    uint32_t vendorId       {0};
    uint32_t productCode    {0};
    uint32_t revisionNumber {0};
    uint16_t slavePos       {0};   ///< Physical position on bus
    uint16_t pdoIndex       {0};   ///< EEPROM PDO index (e.g., 0x1A00)
    uint8_t  pdoSubindex    {0};   ///< Sub-index within PDO

    /// Build canonical hashable string from structured fields.
    /// Same params → same string across restarts. Used ONLY for UUID generation.
    [[nodiscard]] std::string toCanonicalString() const;

    /// Human-readable details string: "EL3632 [pos=2] PDO 0x1A00:01"
    std::string toDetailString(const std::string& slaveName) const;
};

struct GpioBackendData {
    uint32_t chipIndex      {0};   ///< Which gpiod chip (usually 0 for SBCs)
    uint32_t lineOffset     {0};   ///< GPIO line number on the chip
    std::string chipModel;         ///< "BCM2712", etc.

    /// Build canonical hashable string from structured fields.
    [[nodiscard]] std::string toCanonicalString() const;

    /// Human-readable details string: "Line 17 (chip BCM2712)"
    std::string toDetailString() const;
};

struct I2cBackendData {
    uint32_t busId          {0};
    uint16_t deviceAddr     {0};
    uint8_t  registerAddr   {0};

    /// Build canonical hashable string from structured fields.
    [[nodiscard]] std::string toCanonicalString() const;

    std::string toDetailString() const;
};

struct SpiBackendData {
    uint32_t busId          {0};
    uint8_t  chipSelect     {0};
    uint8_t  registerAddr   {0};

    /// Build canonical hashable string from structured fields.
    [[nodiscard]] std::string toCanonicalString() const;

    std::string toDetailString() const;
};

struct SimulatedBackendData {
    // No extra fields beyond common — simulated channels are defined by type + name only.

    /// Canonical string for sim is just the channel identifier (handled in caller).
    [[nodiscard]] static std::string toCanonicalString(const std::string& channelId);
};

// Variant that holds exactly one backend's data.
using BackendSpecificData = std::variant<EthercatBackendData, GpioBackendData,
                                          I2cBackendData, SpiBackendData,
                                          SimulatedBackendData>;

// ---------------------------------------------------------------------------
// ChannelDetails — unified view returned by CatalogEntry::getDetails().
// Contains common fields for all backends plus a human-readable details string
// built from the backend-specific data. Higher-level code uses THIS instead of
// reaching into raw backend fields.
// ---------------------------------------------------------------------------
struct ChannelDetails {
    std::string uuid;           ///< Stable identity (catalog UUID)
    std::string name;           ///< Human-readable display name
    std::string channelType;    ///< "DigitalInput", "FloatOutput", etc.
    bool        isOutput{false};
    bool        isSimulated{false};

    /// Concatenated backend-specific information: "/" separated key-value pairs.
    /// EtherCAT:  "pos=2|PDO 0x1A00:01|EL3632"
    /// GPIO:      "line=17|chip BCM2712"
    /// I2C:       "bus=1|addr=0x68|reg=0x43"
    /// SPI:       "bus=0|cs=0|reg=0x10"
    /// Simulated: "(simulated)"
    std::string detailString;

    BackendType backend{BackendType::UNKNOWN};
};

// ---------------------------------------------------------------------------
// nlohmann JSON serialization helpers — using concrete nlohmann::json type
// so NLOHMANN_DEFINE_TYPE_INTRUSIVE can find them via ADL.
// ---------------------------------------------------------------------------
inline void to_json(nlohmann::json& j, const EthercatBackendData& d)
{
    j = nlohmann::json{{"vendorId",  d.vendorId}, {"productCode",  d.productCode},
             {"revisionNumber",  d.revisionNumber}, {"slavePos",  d.slavePos},
             {"pdoIndex",  d.pdoIndex}, {"pdoSubindex",  static_cast<uint16_t>(d.pdoSubindex)}};
}
inline void from_json(const nlohmann::json& j, EthercatBackendData& d)
{
    j.at("vendorId").get_to(d.vendorId);
    j.at("productCode").get_to(d.productCode);
    j.at("revisionNumber").get_to(d.revisionNumber);
    j.at("slavePos").get_to(d.slavePos);
    j.at("pdoIndex").get_to(d.pdoIndex);
    j.at("pdoSubindex").get_to(d.pdoSubindex);
}

inline void to_json(nlohmann::json& j, const GpioBackendData& d)
{
    j = nlohmann::json{{"chipIndex",  d.chipIndex}, {"lineOffset",  d.lineOffset},
             {"chipModel",  d.chipModel}};
}
inline void from_json(const nlohmann::json& j, GpioBackendData& d)
{
    j.at("chipIndex").get_to(d.chipIndex);
    j.at("lineOffset").get_to(d.lineOffset);
    if (j.contains("chipModel") && !j["chipModel"].is_null())
        j.at("chipModel").get_to(d.chipModel);
}

inline void to_json(nlohmann::json& j, const I2cBackendData& d)
{
    j = nlohmann::json{{"busId",  d.busId}, {"deviceAddr",  d.deviceAddr},
             {"registerAddr",  static_cast<uint16_t>(d.registerAddr)}};
}
inline void from_json(const nlohmann::json& j, I2cBackendData& d)
{
    j.at("busId").get_to(d.busId);
    j.at("deviceAddr").get_to(d.deviceAddr);
    j.at("registerAddr").get_to(d.registerAddr);
}

inline void to_json(nlohmann::json& j, const SpiBackendData& d)
{
    j = nlohmann::json{{"busId",  d.busId}, {"chipSelect",  static_cast<uint16_t>(d.chipSelect)},
             {"registerAddr",  static_cast<uint16_t>(d.registerAddr)}};
}
inline void from_json(const nlohmann::json& j, SpiBackendData& d)
{
    j.at("busId").get_to(d.busId);
    j.at("chipSelect").get_to(d.chipSelect);
    j.at("registerAddr").get_to(d.registerAddr);
}

// SimulatedBackendData — no fields.
inline void to_json(nlohmann::json&, const SimulatedBackendData&) {}
inline void from_json(const nlohmann::json&, SimulatedBackendData&) {}

// BackendType enum serialization (store as string for human-readable JSON).
inline void to_json(nlohmann::json& j, BackendType bt)
{
    switch (bt) {
        case BackendType::ETHERCAT:   j = "EtherCAT"; break;
        case BackendType::GPIO:       j = "GPIO"; break;
        case BackendType::I2C:        j = "I2C"; break;
        case BackendType::SPI:        j = "SPI"; break;
        case BackendType::SIMULATED:  j = "Simulated"; break;
        default:                      j = "Unknown"; break;
    }
}
inline void from_json(const nlohmann::json& j, BackendType& bt)
{
    std::string s = j.get<std::string>();
    if (s == "EtherCAT")      bt = BackendType::ETHERCAT;
    else if (s == "GPIO")     bt = BackendType::GPIO;
    else if (s == "I2C")      bt = BackendType::I2C;
    else if (s == "SPI")      bt = BackendType::SPI;
    else if (s == "Simulated") bt = BackendType::SIMULATED;
    else                       bt = BackendType::UNKNOWN;
}

// BackendSpecificData variant serialization — use discriminator field.
inline void to_json(nlohmann::json& j, const BackendSpecificData& bd)
{
    std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, EthercatBackendData>)
            j = nlohmann::json{{"_type",  "Ethercat"}, {"data",  data}};
        else if constexpr (std::is_same_v<T, GpioBackendData>)
            j = nlohmann::json{{"_type",  "Gpio"}, {"data",  data}};
        else if constexpr (std::is_same_v<T, I2cBackendData>)
            j = nlohmann::json{{"_type",  "I2c"}, {"data",  data}};
        else if constexpr (std::is_same_v<T, SpiBackendData>)
            j = nlohmann::json{{"_type",  "Spi"}, {"data",  data}};
        else if constexpr (std::is_same_v<T, SimulatedBackendData>)
            j = nlohmann::json{{"_type",  "Simulated"}, {"data",  data}};
    }, bd);
}
inline void from_json(const nlohmann::json& j, BackendSpecificData& bd)
{
    std::string type = j.at("_type").get<std::string>();
    if (type == "Ethercat")       bd = j.at("data").get<EthercatBackendData>();
    else if (type == "Gpio")      bd = j.at("data").get<GpioBackendData>();
    else if (type == "I2c")       bd = j.at("data").get<I2cBackendData>();
    else if (type == "Spi")       bd = j.at("data").get<SpiBackendData>();
    else if (type == "Simulated") bd = SimulatedBackendData{};
}

// ---------------------------------------------------------------------------
// CatalogEntry — one PDO channel identified solely by its stable UUID.
// ---------------------------------------------------------------------------
struct CatalogEntry {
    // Simulation parameters — populated for simulated (virt-*) entries.
    // Generic rate-of-change model keyed by EntryType:
    //   BoolInput      → periodic square-wave toggle (togglePeriodMs, dutyCyclePercent)
    //   Int*Input      → linear increment per cycle with optional bounds (incrementPerCycle, minValue, maxValue)
    //   FloatInput     → sinusoidal oscillation (amplitude, frequencyHz, offset)
    //   Output types   → pass-through / echo; no sim params needed on write channels
    struct SimParams {
        // --- Boolean simulation: periodic toggle ---
        uint32_t  togglePeriodMs     {0};     ///< Full high+low period in ms
        float     dutyCyclePercent   {50.0f}; ///< Percent of period spent HIGH

        // --- Integer simulation: linear ramp or bounded sawtooth ---
        int32_t   incrementPerCycle  {1};     ///< Value added each RT cycle
        int64_t   minValue           {INT64_MIN}; ///< Optional lower bound (clamps + wraps)
        int64_t   maxValue           {INT64_MAX}; ///< Optional upper bound (clamps + wraps)

        // --- Float simulation: sinusoidal wave ---
        float     amplitude          {1.0f};  ///< Peak deviation from offset
        float     frequencyHz        {1.0f};  ///< Oscillation frequency in Hz
        float     offset             {0.0f};  ///< DC offset added to sine output

        // --- Legacy I/O configuration (applies to any type) ---
        uint32_t  pulseMs            {0};      ///< Pulse machine arming duration (ms)
        uint32_t  debounceMs         {0};      ///< Debounce filter window (ms)

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(SimParams,
            togglePeriodMs, dutyCyclePercent,
            incrementPerCycle, minValue, maxValue,
            amplitude, frequencyHz, offset,
            pulseMs, debounceMs)
    };

    // ---- Common fields (all backends) ----
    std::string uuid;          ///< RFC-4122 v5 UUID — sole identity, deterministically hashed from backend data
    std::string channelType;   ///< "DigitalInput" | "FloatOutput" | "IMU_GyroX" | etc.
    std::string name;          ///< Human-readable display name: "EL3632 GPIO Output"
    std::string slaveName;     ///< Short model/device name: "EL3632", "BCM2712", "MPU6050"
    bool        isOutput{false};
    bool        isSimulated{false};  ///< true for simulated channels
    SimParams   sim{};              ///< Simulation parameters (if isSimulated)

    // ---- Backend type + structured backend-specific data ----
    BackendType         backend{BackendType::UNKNOWN};
    BackendSpecificData backendData;

    /// Get unified ChannelDetails view — common fields + human-readable detail string.
    /// This is the accessor higher-level code should use instead of touching raw
    /// backend fields directly.
    [[nodiscard]] ChannelDetails getDetails() const;

    /// Hash the backend-specific data into a deterministic canonical string,
    /// then convert that to UUID via fnv1a. Each backend struct has its own
    /// toCanonicalString() so this dispatches via visitor pattern.
    [[nodiscard]] std::string makeUuidFromBackend(BackendType bt, const BackendSpecificData& bd) const;

    // Manual JSON serialization — NLOHMANN_DEFINE_TYPE_INTRUSIVE can't handle variant<backendData>.
    friend void to_json(nlohmann::json& j, const CatalogEntry& e)
    {
        j = nlohmann::json{
            {"uuid", e.uuid},
            {"channelType", e.channelType},
            {"name", e.name},
            {"slaveName", e.slaveName},
            {"isOutput", e.isOutput},
            {"isSimulated", e.isSimulated},
            {"sim", e.sim},
            {"backend", e.backend},
            {"backendData", e.backendData}
        };
    }

    friend void from_json(const nlohmann::json& j, CatalogEntry& e)
    {
        // Backward compatibility: old format had "key" field — skip it.
        if (j.contains("uuid") && !j["uuid"].is_null())          j.at("uuid").get_to(e.uuid);
        else if (j.contains("key") && !j["key"].is_null())       { /* Legacy format: UUID was == key; regenerate below */ }
        j.at("channelType").get_to(e.channelType);
        if (j.contains("name") && !j["name"].is_null())           j.at("name").get_to(e.name);
        if (j.contains("slaveName") && !j["slaveName"].is_null()) j.at("slaveName").get_to(e.slaveName);
        if (j.contains("isOutput"))                                j.at("isOutput").get_to(e.isOutput);
        if (j.contains("isSimulated"))                             j.at("isSimulated").get_to(e.isSimulated);
        if (j.contains("sim"))                                     j.at("sim").get_to(e.sim);
        if (j.contains("backend"))                                 j.at("backend").get_to(e.backend);
        if (j.contains("backendData"))                             j.at("backendData").get_to(e.backendData);

        // Backward compatibility: load old flat EtherCAT fields into backendData.
        if (!j.contains("backendData") || j["backendData"].empty()) {
            if (j.contains("productCode") && j.contains("pdoIndex")) {
                e.backend = BackendType::ETHERCAT;
                EthercatBackendData bd{};
                if (j.contains("vendorId"))      j.at("vendorId").get_to(bd.vendorId);
                if (j.contains("productCode"))   j.at("productCode").get_to(bd.productCode);
                if (j.contains("revisionNumber")) j.at("revisionNumber").get_to(bd.revisionNumber);
                if (j.contains("slavePos"))      j.at("slavePos").get_to(bd.slavePos);
                if (j.contains("pdoIndex"))      j.at("pdoIndex").get_to(bd.pdoIndex);
                if (j.contains("pdoSubindex"))   j.at("pdoSubindex").get_to(bd.pdoSubindex);
                e.backendData = std::move(bd);
            }
        }
    }

}; // struct CatalogEntry

// ---------------------------------------------------------------------------
// HardwareCatalog — backend-agnostic channel registry.
//
// Usage:
//   1. catalog.load(path)                   — at startup (ok if file absent)
//   2. adapter.setCatalog(&catalog)
//   3. adapter.discover()                   — populates catalog from hardware scan
//   4. catalog.save(path)                   — immediately after discover()
//   5. adapter.buildRT()                    — construct PDOs and activate backend
//   6. registry.addBackend(adapter)         — transfer ownership to registry
//   7. registry.buildUuidMap()              — builds string→DHDOEntry* map
//
// On subsequent starts the same keys re-map to the same UUIDs.
// ---------------------------------------------------------------------------
class HardwareCatalog {
public:
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    // ---- Discovery lifecycle ----
    // Call beginDiscovery() before running any adapters, then purgeStaleEntries()
    // after all adapters complete.  Entries added via addEntry/registerEcChannel
    // are auto-marked as "alive" so they survive; anything not re-seen gets removed.

    /// Begin a fresh discovery cycle — marks current catalog as potentially stale.
    void beginDiscovery();

    /// End discovery — remove entries that were NOT registered during this cycle.
    /// Returns the number of purged entries.
    size_t purgeStaleEntries();

    /// End discovery AND lock the catalog against further writes.
    /// After calling this, addEntry/registerEcChannel will silently reject new entries.
    /// Call purgeStaleEntries() internally for convenience.
    void endDiscovery();

    /// Query whether the catalog accepts write operations (addEntry/registerEcChannel).
    /// Returns false after endDiscovery() is called or when writeLocked_ is true.
    [[nodiscard]] bool isWritable() const noexcept { return !writeLocked_; }

    // ---- Registration (called during EtherCAT / I2C / SPI discovery) ----

   /// Register or look up an EtherCAT PDO channel.
    /// If the UUID (derived from backend data) already exists the existing
    /// entry is returned unchanged — hardware hasn't changed.
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

    /// Register a new channel entry (generic backend). Generates deterministic UUID
    /// from structured backendData if not already set. Auto-marks UUID as alive during discovery.
    void addEntry(CatalogEntry entry);

    // ---- Accessors --------------------------------------------------------

   [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] const std::vector<CatalogEntry>& entries() const noexcept { return entries_; }

    /// Find by UUID — the sole identity lookup. No "key" field exists.
    [[nodiscard]] const CatalogEntry* findByUuid(const std::string& uuid) const noexcept;

    /// Find EtherCAT entry by vendorId + productCode (backward compatibility).
    [[nodiscard]] const CatalogEntry* find(uint32_t vendorId, uint32_t productCode) const noexcept;

    /// Legacy stub — kept only for backward compat with old callers.
    [[deprecated("Use findByUuid instead")]]
    [[nodiscard]] std::string getOrCreateUuid(const std::string&) noexcept;

private:
    std::vector<CatalogEntry>               entries_;
    std::unordered_map<std::string, size_t> uuidIndex_;  ///< uuid → index (sole lookup)

   // Discovery lifecycle: tracks which UUIDs were registered during current cycle.
    bool                            discoveryMode_{false};
    std::unordered_set<std::string> aliveUuids_;  ///< UUIDs marked alive since beginDiscovery()

    /// Write lockdown — after endDiscovery() is called, the catalog becomes read-only.
    /// Prevents post-discovery mutation (Issue B fix).
    bool                            writeLocked_{false};

    void markAlive(const std::string& uuid);
    void rebuildIndices();
    /// Canonical FNV-1a 64-bit hash (single source of truth — delegates to fnv1aHash()).
    [[nodiscard]] static uint64_t hashKey(std::string_view key);

    /// Build a human-readable name for a new entry.
    static std::string makeName(const std::string& slaveName, uint16_t slavePos,
                                const std::string& channelType,
                                uint16_t pdoIndex, uint8_t pdoSubindex);

    /// Generate a random RFC-4122 v4 UUID string.
    static std::string generateUuid();
};

} // namespace dynamichardware::dhdo
