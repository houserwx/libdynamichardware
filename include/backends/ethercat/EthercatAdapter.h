#pragma once
#include "backends/pdo/IHardwareAdapter.h"
#include "backends/ethercat/HardwareCatalog.h"
#include "backends/ethercat/SlaveTypeInfo.h"
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

namespace fc::ethercat {

// ============================================================================
// Config — EtherCAT-specific configuration loaded from hardware.json.
// Provides per-UUID pulse/debounce overrides via the common::config::Config.
// ============================================================================
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

    // Load from JSON
    static Config loadFromJson(const std::string& path);

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Config,
        masterIndex, cycleTimeUs, useDcSync, useDcFsmAuto,
        domainName, expectedSlaveCount, slaves)
};

// ---- Internal PDO registration record (init phase only) ----------------------
// Holds the EtherCAT PDO geometry discovered during slave scan.
// After buildEntries() this is only accessed by applyConfig(); it is not
// used in the RT loop.
struct EcEntryReg {
    uint16_t     slavePos;
    uint32_t     vendorId;
    uint32_t     productCode;
    uint16_t     pdoIndex;
    uint8_t      pdoSubindex;
    uint8_t      bitLength;
    bool         isOutput;
    std::string  uuid;          ///< catalog UUID — used to match config PdoEntryDef
    unsigned int offset{0};
    unsigned int bitPos{0};
};

// ---- EthercatAdapter ---------------------------------------------------------
// Owns one EtherCAT domain (one PDO in pdos_[0]).
// onBeforeReadInputs()  — receive + domain_process  → fills domainData_ buffer.
// onAfterWriteOutputs() — domain_queue + send       → flushes domainData_ buffer.
// Both methods operate on the IgH-managed domainData_ buffer directly.
// PDOEntry::image pointers inside pdos_[0].entries point into this buffer,
// so no copy is needed between the IgH buffer and PDO::image.

class EthercatAdapter final : public fc::pdo::IHardwareAdapter {
public:
    /// @param cycleNs  EtherCAT cycle period in nanoseconds (must match DC sync config).
    explicit EthercatAdapter(uint32_t cycleNs = 1'000'000u) noexcept
        : cycleNs_(cycleNs) {}

    ~EthercatAdapter() override {
#ifdef ETHERCAT_AVAILABLE
        if (master_) ecrt_release_master(master_);
#endif
    }

    /// Optionally attach a HardwareCatalog before calling initialize().
    void setCatalog(fc::pdo::HardwareCatalog* catalog) noexcept { catalog_ = catalog; }

    /// Optionally attach the application Config before calling initialize().
    void setConfig(const Config* config) noexcept { config_ = config; }

    /// Discover slaves, register PDOs, configure DC sync, activate master,
    /// block until all slaves reach WC_COMPLETE (or timeout).
    bool initialize() override;

    /// Receive EtherCAT frames and process domain data into domainData_.
    void onBeforeReadInputs()  noexcept override;

    /// Sync clocks, queue domain, and send EtherCAT frames from domainData_.
    void onAfterWriteOutputs() noexcept override;

    // --- Status accessors ---
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

    /// Spin the EtherCAT loop until domain reaches WC_COMPLETE, up to timeoutMs.
    bool waitForCommunication(uint32_t timeoutMs = 5000u);

private:
#ifdef ETHERCAT_AVAILABLE
    ec_master_t*      master_{nullptr};
    ec_domain_t*      domain_{nullptr};
    uint8_t*          domainData_{nullptr};  // IgH-managed; PDOEntry::image pointers point into this
    ec_domain_state_t lastDomainState_{};
#else
    void*             master_{nullptr};
    void*             domain_{nullptr};
    uint8_t*          domainData_{nullptr};
    struct { uint16_t wc_state{0}; uint16_t working_counter{0}; } lastDomainState_{};
#endif
    uint32_t          cycleNs_;
    fc::pdo::HardwareCatalog*  catalog_{nullptr};
    const Config*     config_{nullptr};

    int               nSlaves_{0};
    std::atomic<uint64_t> cycleCount_{0u};

    // Init-phase storage (not used in RT loop) — stable storage for offset/bitPos pointers
    std::vector<EcEntryReg> regs_;

    bool discoverSlaves();
    void buildEntries();    ///< Populates pdos_[0].entries from regs_ + domainData_
    void applyConfig();     ///< Applies pulse/debounce from Config to pdos_[0] entries
};

} // namespace fc::ethercat
