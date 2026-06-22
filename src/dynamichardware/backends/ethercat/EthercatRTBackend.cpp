#include "dynamichardware/backends/ethercat/EthercatRTBackend.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"
#include "dynamichardware/backends/ethercat/SlaveTypeInfo.h"
#include "dynamichardware/backends/ethercat/EthercatEntryKey.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>
#include <unordered_map>

namespace dynamichardware::ethercat {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint64_t clockNowNs() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL) +
           static_cast<uint64_t>(ts.tv_nsec);
}

// ---------------------------------------------------------------------------
// configure() — extract recognized keys from orchestrator config map.
// Recognized keys: "cycleNs" (default 1000000ns = 1ms).
// ---------------------------------------------------------------------------
void EthercatRTBackend::configure(const std::unordered_map<std::string, std::string>& config)
{
    auto it = config.find("cycleNs");
    if (it != config.end()) {
        try { std::atomic_store(&cycleNs_, static_cast<uint32_t>(std::stoul(it->second))); }
        catch (...) {
            std::fprintf(stderr, "[EtherCAT] Invalid cycleNs value '%s' — using default\n",
                         it->second.c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// Destructor — release all RT resources.
// ---------------------------------------------------------------------------

EthercatRTBackend::~EthercatRTBackend()
{
#ifdef ETHERCAT_AVAILABLE
    if (master_) ecrt_release_master(master_);
#endif
}

// ---------------------------------------------------------------------------
// buildRT() — RT lifecycle implementation
/// Fully self-contained: acquires master, scans slaves, creates domain,
// activates, builds PDO structure, waits for WC_COMPLETE.
// ---------------------------------------------------------------------------

bool EthercatRTBackend::buildRT()
{
#ifdef ETHERCAT_AVAILABLE
    // 1. Acquire master (fresh — independent of any prior discovery scan)
    master_ = ecrt_request_master(0);
    if (master_ == nullptr) {
        std::fprintf(stderr, "[EtherCAT] Cannot acquire master — kernel module loaded?\n");
        return false;
    }

    // 2. Create process-data domain
    domain_ = ecrt_master_create_domain(master_);
    if (domain_ == nullptr) {
        std::fprintf(stderr, "[EtherCAT] Failed to create domain\n");
        ecrt_release_master(master_);
        master_ = nullptr;
        return false;
    }

    // 3. Scan slaves on bus and register PDOs in domain (populates regs_)
    if (!discoverAndRegister()) {
        ecrt_release_master(master_);
        master_ = nullptr;
        return false;
    }

    // 4. Activate master
    std::printf("[EtherCAT] Activating master...\n");
    if (ecrt_master_activate(master_) != 0) {
        std::fprintf(stderr, "[EtherCAT] Master activation failed\n");
        ecrt_release_master(master_);
        master_ = nullptr;
        return false;
    }

    // 5. Get domain data pointer (only valid after activation)
    domainData_ = ecrt_domain_data(domain_);
    if (domainData_ == nullptr) {
        std::fprintf(stderr, "[EtherCAT] ecrt_domain_data() returned nullptr\n");
        ecrt_release_master(master_);
        master_ = nullptr;
        return false;
    }

    // 6. Build PDOEntry structs in dhdos_[0] from discovered entries + apply config
    buildEntries();
    applyConfig();

    std::printf("[EtherCAT] Ready: %d slave(s), %zu PDO entries, cycleNs=%u\n",
                nSlaves_, dhdos_.empty() ? 0u : dhdos_[0].entries.size(),
                std::atomic_load(&cycleNs_));

    // 7. Wait for all slaves to reach WC_COMPLETE
    std::printf("[EtherCAT] Waiting for slaves to reach OP state...\n");
    if (!waitForCommunication(5000)) {
        std::fprintf(stderr,
            "[EtherCAT] WARNING: slaves did not reach WC_COMPLETE within 5 s "
            "(wc_state=%u, wc=%u) — continuing anyway\n",
            lastDomainState_.wc_state,
            lastDomainState_.working_counter);
    } else {
        std::printf("[EtherCAT] All slaves communicating (wc=%u)\n",
                    lastDomainState_.working_counter);
    }
    return true;

#else
    std::fprintf(stderr, "[EthercatRTBackend] EtherCAT not available — stub mode\n");
    return false;
#endif
}

// ---------------------------------------------------------------------------
// discoverAndRegister() — scan bus + register PDOs in domain → populate regs_
// ---------------------------------------------------------------------------

bool EthercatRTBackend::discoverAndRegister()
{
#ifdef ETHERCAT_AVAILABLE
    ec_master_state_t ms{};
    ecrt_master_state(master_, &ms);
    const int total = static_cast<int>(ms.slaves_responding);

    std::printf("[EtherCAT] Master state: %u slave(s) responding, link=%s\n",
                ms.slaves_responding, (ms.al_states != 0U) ? "up" : "down");

    if (total == 0) {
        std::fprintf(stderr, "[EtherCAT] No slaves responding on bus\n");
        return false;
    }

    // Reserve so push_back never reallocates — offset pointers below must stay stable.
    regs_.reserve(256);

    for (uint16_t pos = 0; pos < static_cast<uint16_t>(total); ++pos) {
        ec_slave_info_t si{};
        if (ecrt_master_get_slave(master_, pos, &si) != 0) {
            std::printf("[EtherCAT] Warning: ecrt_master_get_slave(%u) failed\n", pos);
            continue;
        }
        ++nSlaves_;

        const SlaveTypeInfo* sti = lookupSlaveType(si.vendor_id, si.product_code);
        std::printf("[EtherCAT] Slave %u: 0x%08X:0x%08X '%s' [%s] syncs=%u\n",
                    pos, si.vendor_id, si.product_code,
                    static_cast<const char*>(si.name),
                    (sti != nullptr) ? sti->type_name : "unknown",
                    si.sync_count);

        // Bind slave config to master (uses default EEPROM PDO mapping)
        ec_slave_config_t* sc = ecrt_master_slave_config(
            master_, 0, pos, si.vendor_id, si.product_code);
        if (sc == nullptr) {
            std::fprintf(stderr, "[EtherCAT]   Failed to get slave config for pos %u\n", pos);
            continue;
        }

        // Configure DC synchronisation from kSlaveTypes table
        if (sti != nullptr) {
            const DcOpMode* dcMode = lookupDcMode(sti);
            uint32_t curCycleNs = std::atomic_load(&cycleNs_);
            if ((dcMode != nullptr) && dcMode->assign_activate != 0U) {
                ecrt_slave_config_dc(sc, dcMode->assign_activate,
                                     curCycleNs, 0, 0, 0);
                std::printf("[EtherCAT]   DC enabled: assign_activate=0x%04X "
                            "(%s) cycleNs=%u\n",
                            dcMode->assign_activate, dcMode->name, curCycleNs);
            } else {
                ecrt_slave_config_dc(sc, 0x0000, 0, 0, 0, 0);
            }
        }

        // Walk EEPROM sync managers and auto-discover all PDO entries.
        for (uint8_t smIdx = 0; smIdx < si.sync_count; ++smIdx) {
            ec_sync_info_t smInfo{};
            if (ecrt_master_get_sync_manager(master_, pos, smIdx, &smInfo) != 0) continue;
            if (smInfo.dir == EC_DIR_INVALID) continue;

            const bool isOutput = (smInfo.dir == EC_DIR_OUTPUT);

            for (unsigned int pIdx = 0; pIdx < smInfo.n_pdos; ++pIdx) {
                ec_pdo_info_t pdoInfo{};
                if (ecrt_master_get_pdo(master_, pos, smIdx, pIdx, &pdoInfo) != 0) continue;

                for (unsigned int eIdx = 0; eIdx < pdoInfo.n_entries; ++eIdx) {
                    ec_pdo_entry_info_t entry{};
                    if (ecrt_master_get_pdo_entry(master_, pos, smIdx, pIdx, eIdx, &entry) != 0)
                        continue;
                    if (entry.index == 0) continue; // padding / gap entry

                    EcEntryReg r{};
                    r.slavePos    = pos;
                    r.vendorId    = si.vendor_id;
                    r.productCode = si.product_code;
                    r.pdoIndex    = entry.index;
                    r.pdoSubindex = entry.subindex;
                    r.bitLength   = entry.bit_length;
                    r.isOutput    = isOutput;
                    // Single source of truth — guaranteed to match discovery keys.
                    r.uuid        = buildEntryKey(
                        si.vendor_id, si.product_code,
                        isOutput, entry.bit_length,
                        pos, entry.index, entry.subindex);

                    regs_.push_back(r);
                }
            }
        }
    }

    if (regs_.empty()) {
        std::fprintf(stderr, "[EtherCAT] No PDO entries discovered\n");
        return false;
    }

    // Build domain PDO registration table (pointers into regs_ — must stay stable)
    std::vector<ec_pdo_entry_reg_t> regList;
    regList.reserve(regs_.size() + 1);
    for (auto& r : regs_) {
        ec_pdo_entry_reg_t er{};
        er.alias        = 0;
        er.position     = r.slavePos;
        er.vendor_id    = r.vendorId;
        er.product_code = r.productCode;
        er.index        = r.pdoIndex;
        er.subindex     = r.pdoSubindex;
        er.offset       = &r.offset;
        er.bit_position = &r.bitPos;
        regList.push_back(er);
    }
    regList.push_back({}); // null terminator

    if (ecrt_domain_reg_pdo_entry_list(domain_, regList.data()) != 0) {
        std::fprintf(stderr, "[EtherCAT] PDO domain registration failed\n");
        return false;
    }

    std::printf("[EtherCAT] Registered %zu PDO entries across %d slave(s)\n",
                regs_.size(), nSlaves_);
    return true;

#else
    (void)0;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// buildEntries() — populate dhdos_[0].entries from regs_ + domainData_ buffer.
// Entry image pointers point directly into the IgH-managed buffer (zero-copy).
// ---------------------------------------------------------------------------

void EthercatRTBackend::buildEntries()
{
    dhdos_.resize(1);
    dhdos_[0].entries.reserve(regs_.size());

    for (const auto& r : regs_) {
        dynamichardware::dhdo::EntryType type;
        if      (r.bitLength == 1U  && !r.isOutput) { type = dynamichardware::dhdo::EntryType::BoolInput;     }
        else if (r.bitLength == 1U  &&  r.isOutput) { type = dynamichardware::dhdo::EntryType::BoolOutput;    }
        else if (r.bitLength == 32U && !r.isOutput) { type = dynamichardware::dhdo::EntryType::Int32Input;    }
        else if (r.bitLength == 16U && !r.isOutput) { type = dynamichardware::dhdo::EntryType::Int16Input;    }
        else {
            continue; // Unsupported width — registered in domain, not yet wrapped as typed entry
        }

        std::printf("[EtherCAT]   uuid=%-40s %s bits=%-2u byte=%-4u bit=%u idx=0x%04X:%02X\n",
                    r.uuid.c_str(),
                    r.isOutput ? "OUT" : "IN ",
                    r.bitLength, r.offset, r.bitPos,
                    r.pdoIndex, r.pdoSubindex);

        dynamichardware::dhdo::DHDOEntry e{};
        e.image      = domainData_ + r.offset;          // direct into IgH buffer
        e.byteOffset = r.offset;                        // retained for reference
        e.bitOffset  = static_cast<uint8_t>(r.bitPos);
        e.bitLength  = r.bitLength;
        e.uuid       = r.uuid;
        e.type       = type;
        dhdos_[0].entries.push_back(e);
    }

    // PDO::image is empty — domainData_ is IgH-managed.
    // freeze() sees image.empty() and leaves entry image pointers untouched.
    dhdos_[0].freeze();
}

// ---------------------------------------------------------------------------
// waitForCommunication() — spin loop until WC_COMPLETE or timeout.
// ---------------------------------------------------------------------------

bool EthercatRTBackend::waitForCommunication(uint32_t timeoutMs)
{
#ifdef ETHERCAT_AVAILABLE
    const uint32_t intervalUs = (std::atomic_load(&cycleNs_) / 1000U);
    const int maxCycles = static_cast<int>((timeoutMs * 1000ULL) / intervalUs);
    const int logEvery = static_cast<int>(500000U / intervalUs); // ~0.5 s

    for (int i = 0; i < maxCycles; ++i) {
        onBeforeReadInputs();
        onAfterWriteOutputs();

        if (lastDomainState_.wc_state == EC_WC_COMPLETE) {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(intervalUs));

        if (logEvery > 0 && i > 0 && (i % logEvery) == 0) {
            std::printf("[EtherCAT]   wc_state=%u wc=%u  (%.0f ms elapsed)\n",
                        lastDomainState_.wc_state, lastDomainState_.working_counter,
                        static_cast<double>(i) * intervalUs / 1000.0);
        }
    }
    return lastDomainState_.wc_state == EC_WC_COMPLETE;

#else
    (void)timeoutMs;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// setCyclePeriod() — runtime-safe cycle update via atomic store.
// NOTE: DC sync reconfiguration requires master deactivation which is NOT done here.
// The new value takes effect for application time stamping immediately;
// actual slave DC sync would need a full rebuild to propagate.
// ---------------------------------------------------------------------------
void EthercatRTBackend::setCyclePeriod(uint64_t nanoseconds) noexcept
{
    if (nanoseconds == 0) return;  // Guard against invalid zero period
    std::atomic_store(&cycleNs_, static_cast<uint32_t>(nanoseconds));
    std::printf("[EtherCAT] Cycle period updated to %llu ns (%.1f kHz)\n",
                (unsigned long long)nanoseconds,
                1'000'000'000.0 / static_cast<double>(nanoseconds));
}

uint64_t EthercatRTBackend::getCyclePeriod() const noexcept
{
    return static_cast<uint64_t>(std::atomic_load(&cycleNs_));
}

// ---------------------------------------------------------------------------
// onBeforeReadInputs() — called every cycle before reading PDO data.
// ---------------------------------------------------------------------------

void EthercatRTBackend::onBeforeReadInputs() noexcept
{
    if (!master_) return;
#ifdef ETHERCAT_AVAILABLE
    const uint64_t appTimeNs = clockNowNs();
    ecrt_master_application_time(master_, appTimeNs);

    ecrt_master_receive(master_);
    ecrt_domain_process(domain_);
    ecrt_domain_state(domain_, &lastDomainState_);

    cycleCount_.fetch_add(1U, std::memory_order_relaxed);
#endif
}

// ---------------------------------------------------------------------------
// onAfterWriteOutputs() — called every cycle after writing PDO outputs.
// ---------------------------------------------------------------------------

void EthercatRTBackend::onAfterWriteOutputs() noexcept
{
    if (!master_) return;
#ifdef ETHERCAT_AVAILABLE
    const uint64_t nowNs = clockNowNs();
    ecrt_master_application_time(master_, nowNs);
    ecrt_master_sync_reference_clock(master_);

    ecrt_master_sync_slave_clocks(master_);

    ecrt_domain_queue(domain_);
    ecrt_master_send(master_);
#endif
}

// ---------------------------------------------------------------------------
// applyConfig() — applies pulse/debounce from Config to dhdos_[0] entries.
// Called once during buildRT(); never in the RT loop.
// ---------------------------------------------------------------------------

void EthercatRTBackend::applyConfig()
{
    if ((config_ == nullptr) || config_->slaves.empty()) {
        return;
    }

    // Build UUID → SlaveConfig lookup for pulse/debounce overrides.
    std::unordered_map<std::string, const Config::SlaveConfig*> uuidMap;
    for (const auto& sc : config_->slaves) {
        for (const auto& uuid : sc.uuidMap) {
            uuidMap[uuid] = &sc;
        }
    }

    for (const auto& reg : regs_) {
        if (reg.uuid.empty()) continue;

        auto it = uuidMap.find(reg.uuid);
        if (it == uuidMap.end()) continue;

        for (auto& e : dhdos_[0].entries) {
            if (e.uuid.empty() || e.uuid != reg.uuid) continue;

            if (reg.isOutput) {
                e.configurePulseMs(100); // Default pulse — can be overridden by alias logic
                std::printf("[EtherCAT]   uuid=%-40s pulse configured  '%s'\n",
                            reg.uuid.c_str(), it->second->alias.c_str());
            } else {
                e.configureDebounceMs(5); // Default debounce
                std::printf("[EtherCAT]   uuid=%-40s debounce configured  '%s'\n",
                            reg.uuid.c_str(), it->second->alias.c_str());
            }
            break;
        }
    }
}

void EthercatRTBackend::setCatalog(const dhdo::HardwareCatalog* catalog) noexcept {
    catalog_ = catalog;
}

bool EthercatRTBackend::build(const std::vector<dhdo::MappedChannel>& channels) {
    // Store UUID filter list if non-empty (for Issue G - LSP consistency)
    if (!channels.empty()) {
        channelUuidFilter_.clear();
        for (const auto& ch : channels) {
            channelUuidFilter_.push_back(ch.uuid);
        }
        std::printf("[EtherCAT] Filtering to %zu specified channels\n", channelUuidFilter_.size());
    }

    // Call existing buildRT() which handles master init, slave discovery, domain creation, etc.
    bool ok = buildRT();
    if (!ok) return false;

    // If we have a UUID filter and entries were built, remove entries not in the filter list.
    if (!channelUuidFilter_.empty() && !dhdos_[0].entries.empty()) {
        auto beforeCount = dhdos_[0].entries.size();
        std::vector<dhdo::DHDOEntry> filtered;
        for (auto& entry : dhdos_[0].entries) {
            bool found = false;
            for (const auto& uuidStr : channelUuidFilter_) {
                if (entry.uuid == uuidStr) { found = true; break; }
            }
            if (found) filtered.push_back(std::move(entry));
        }
        auto removedCount = beforeCount - filtered.size();
        if (removedCount > 0) {
            std::printf("[EtherCAT] Applied channel filter: kept %zu entries, removed %zu\n",
                        filtered.size(), removedCount);
            dhdos_[0].entries = std::move(filtered);
        }
    }

    return true;
}

} // namespace dynamichardware::ethercat
