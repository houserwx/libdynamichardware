#pragma once
#include "dynamichardware/pdo/IRTBackend.h"
#include <vector>
#include <atomic>
#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

// Guard IgH EtherCAT headers — only available when libethercat is installed.
#ifdef ETHERCAT_AVAILABLE
extern "C" {
#include <ecrt.h>
}
#endif

namespace dynamichardware::ethercat {

/// ============================================================================
/// Config — EtherCAT-specific configuration loaded from hardware.json.
/// Provides per-slave pulse/debounce overrides applied during buildRT().
/// ============================================================================
struct Config {
    // Master settings
    int             masterIndex{0};
    uint32_t        cycleTimeUs{1000};  // Default 1ms cycle
    bool            useDcSync{true};     // Enable distributed clock synchronization
    bool            useDcFsmAuto{true}; // Auto-start DC state machine

    // Domain settings
    std::string     domainName{"default"};

    // Slave expectations (optional, for validation)
    int             expectedSlaveCount{0}; // 0 = any count accepted

    // Per-slave configuration overrides
    struct SlaveConfig {
        uint16_t                position{0};
        uint32_t                vendorId{0};
        uint32_t                productCode{0};
        std::string             alias;     // Human-readable: "EL2124-DigitalOut-Slot1"
        std::vector<std::string> uuidMap; // UUIDs mapped to this slave's channels

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(SlaveConfig,
            position, vendorId, productCode, alias, uuidMap)
    };
    std::vector<SlaveConfig> slaves;

    /// Load from a JSON file path.
    static Config loadFromJson(const std::string& path);

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Config,
        masterIndex, cycleTimeUs, useDcSync, useDcFsmAuto,
        domainName, expectedSlaveCount, slaves)
};

/// ---- Internal PDO registration record ------------------------------------
/// Holds the EtherCAT PDO geometry discovered during buildRT().
/// Used to map IgH-managed offsets/bits into concrete PDOEntry structs.
struct EcEntryReg {
    uint16_t     slavePos;
    uint32_t     vendorId;
    uint32_t     productCode;
    uint16_t     pdoIndex;
    uint8_t      pdoSubindex;
    uint8_t      bitLength;
    bool         isOutput;
    std::string  uuid;          ///< catalog UUID — matched against config PdoEntryDef
    unsigned int offset{0};
    unsigned int bitPos{0};
};

/// ---- EthercatRTBackend ---------------------------------------------------
/// Real-time EtherCAT process-data backend for I/O cycles. Implements IRTBackend.
///
/// Fully independent of discovery: acquires its own master/domain in buildRT(),
/// re-scans the bus, builds PDO structures from scratch (using catalog UUIDs),
/// and runs the RT loop via onBeforeReadInputs/onAfterWriteOutputs hooks.
///
/// Lifecycle:
///   1. setConfig(cfg)               — optional pulse/debounce overrides
///   2. buildRT()                    — acquire master, scan slaves, activate, wait WC_COMPLETE
///   3. onBeforeReadInputs()         — receive + process domain data into buffer
///   4. onAfterWriteOutputs()        — queue domain + send frames from buffer
///   5. Destructor                   — release master+domain
class EthercatRTBackend final : public dynamichardware::pdo::IRTBackend {
public:
    /// @param cycleNs  Cycle period in nanoseconds (must match DC sync configuration).
    explicit EthercatRTBackend(uint32_t cycleNs = 1'000'000u) noexcept
        : cycleNs_(cycleNs) {}

    ~EthercatRTBackend() override;

    // setCatalog inherited protected from IDiscoveryBackend is NOT needed here;
    // IRTBackend has no catalog_ member. We use catalog indirectly through PDO entries.

    /// Optionally attach application Config before calling buildRT().
    void setConfig(const Config* config) noexcept { config_ = config; }

    // --- IRTBackend implementation ------------------------------------------
    /// Acquire master, scan slaves, create domain, activate, build PDOs, wait WC_COMPLETE.
    [[nodiscard]] bool buildRT() override;

    /// Receive EtherCAT frames and process domain data into the IgH-managed buffer.
    void onBeforeReadInputs()  noexcept override;

    /// Sync clocks, queue domain, and send EtherCAT frames from the buffer.
    void onAfterWriteOutputs() noexcept override;

    // --- Status accessors ---------------------------------------------------
    [[nodiscard]] bool     isAvailable()          const noexcept { return master_ != nullptr; }
    [[nodiscard]] bool     isFullyCommunicating() const noexcept {
#ifdef ETHERCAT_AVAILABLE
        return lastDomainState_.wc_state == EC_WC_COMPLETE;
#else
        return lastDomainState_.wc_state == 0;
#endif
    }
    [[nodiscard]] int      slaveCount()           const noexcept { return nSlaves_; }
    [[nodiscard]] uint16_t workingCounter()       const noexcept {
        return lastDomainState_.working_counter;
    }
    [[nodiscard]] uint64_t cycleCount()           const noexcept {
        return cycleCount_.load(std::memory_order_relaxed);
    }

private:
#ifdef ETHERCAT_AVAILABLE
    ec_master_t*      master_{nullptr};
    ec_domain_t*      domain_{nullptr};
    uint8_t*          domainData_{nullptr};  // IgH-managed; PDOEntry::image pointers point here
    ec_domain_state_t lastDomainState_{};
#else
    void*             master_{nullptr};
    void*             domain_{nullptr};
    uint8_t*          domainData_{nullptr};
    struct { uint16_t wc_state{0}; uint16_t working_counter{0}; } lastDomainState_{};
#endif
    uint32_t          cycleNs_;
    const Config*     config_{nullptr};

    int               nSlaves_{0};
    std::atomic<uint64_t> cycleCount_{0u};

    /// Init-phase storage — stable storage for offset/bitPos pointers during buildRT().
    std::vector<EcEntryReg> regs_;

    // Private helpers (all run within buildRT())
    bool discoverAndRegister();   ///< Scan slaves, create domain registrations → populate regs_
    void buildEntries();          ///< Populate pdos_[0].entries from regs_ + domainData_
    void applyConfig();           ///< Apply pulse/debounce from Config to entries
    bool waitForCommunication(uint32_t timeoutMs = 5000u);
};

} // namespace dynamichardware::ethercat
