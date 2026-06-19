#include "dynamichardware/backends/ethercat/EthercatAdapter.h"
#include "dynamichardware/pdo/HardwareCatalog.h"
#include "dynamichardware/backends/ethercat/SlaveTypeInfo.h"

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

// Map PDO geometry to a channel-type string for the hardware catalog.
static const char* inferChannelType(uint8_t bitLength, bool isOutput) noexcept {
    if (bitLength == 1U && !isOutput) { return "DigitalInput";  }
    if (bitLength == 1U &&  isOutput) { return "DigitalOutput"; }
    if (bitLength == 32U && !isOutput) { return "Encoder";       }
    if (bitLength == 16U && !isOutput) { return "AnalogInput";   }
    return "Raw";
}

// ---------------------------------------------------------------------------
// discover() — IDiscoveryBackend implementation
// Acquires master, creates domain, scans slaves, populates catalog.
// Master stays open for buildRT() phase but is NOT activated yet.
// ---------------------------------------------------------------------------
bool EthercatAdapter::discover()
{
#ifdef ETHERCAT_AVAILABLE
    // 1. Acquire master
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

    // 3. Discover slaves, register PDOs in catalog, configure DC sync
    // The master stays open so buildRT() can activate it later.
    if (!discoverSlaves()) {
        ecrt_release_master(master_);
        master_ = nullptr;
        return false;
    }

    std::printf("[EtherCAT] Discovery complete: %d slave(s), %zu PDO entries registered in catalog\n",
                nSlaves_, regs_.size());
    return true;
#else
    std::fprintf(stderr, "[EthercatAdapter] EtherCAT not available — stub mode\n");
    return false;
#endif
}

// ---------------------------------------------------------------------------
// buildRT() — IRTBackend implementation
// Activates master, builds PDO structure from discovered entries,
// waits for WC_COMPLETE. Called after consumer configuration phase.
// ---------------------------------------------------------------------------
bool EthercatAdapter::buildRT()
{
#ifdef ETHERCAT_AVAILABLE
    if (master_ == nullptr) {
        std::fprintf(stderr, "[EtherCAT] Cannot build RT — no master acquired (call discover first)\n");
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

    // 6. Build PDOEntry structs in pdos_[0] and apply config
    buildEntries();
    applyConfig();

    std::printf("[EtherCAT] Ready: %d slave(s), %zu PDO entries, cycleNs=%u\n",
                nSlaves_, pdos_.empty() ? 0u : pdos_[0].entries.size(), cycleNs_);

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
    std::fprintf(stderr, "[EthercatAdapter] EtherCAT not available — stub mode\n");
    return false;
#endif
}

// ---------------------------------------------------------------------------
// discoverSlaves()
// ---------------------------------------------------------------------------

bool EthercatAdapter::discoverSlaves()
{
#ifdef ETHERCAT_AVAILABLE
    // Use master state to get the actual slave count (avoids "Slave N does not
    // exist" kernel log spam that a fixed 0..63 loop causes).
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

        // Look up slave in ESI-derived table
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
            if ((dcMode != nullptr) && dcMode->assign_activate != 0U) {
                ecrt_slave_config_dc(sc, dcMode->assign_activate, cycleNs_, 0, 0, 0);
                std::printf("[EtherCAT]   DC enabled: assign_activate=0x%04X "
                            "(%s) cycleNs=%u\n",
                            dcMode->assign_activate, dcMode->name, cycleNs_);
            } else {
                // Slave has no DC mode — explicitly disable to suppress kernel warnings
                ecrt_slave_config_dc(sc, 0x0000, 0, 0, 0, 0);
            }
        }

        // Walk EEPROM sync managers and auto-discover all PDO entries
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

                    if (catalog_ != nullptr) {
                        const char* ctype = inferChannelType(entry.bit_length, isOutput);
                        // Register channel in catalog — gets or reuses UUID
                        catalog_->addEntry(dynamichardware::pdo::CatalogEntry{
                            catalog_->getOrCreateUuid(
                                "EC|" + std::to_string(si.vendor_id) +
                                "|" + std::to_string(si.product_code) +
                                "|POS" + std::to_string(pos) +
                                "|" + std::to_string(entry.index) +
                                ":" + std::to_string(entry.subindex)),
                            "", ctype, "",
                            (sti != nullptr) ? sti->type_name : "unknown",
                            pos, si.product_code, si.revision_number,
                            entry.index, entry.subindex, isOutput
                        });
                        r.uuid = catalog_->getOrCreateUuid(
                            "EC|" + std::to_string(si.vendor_id) +
                            "|" + std::to_string(si.product_code) +
                            "|POS" + std::to_string(pos) +
                            "|" + std::to_string(entry.index) +
                            ":" + std::to_string(entry.subindex));
                    }

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
// buildEntries()
// Populates pdos_[0].entries with concrete PDOEntry structs.
// Entry image pointers are set to domainData_ + offset (pointing directly
// into the IgH-managed buffer — no copy needed).  PDO::image stays empty;
// PDO::freeze() detects this and skips re-basing.
// ---------------------------------------------------------------------------

void EthercatAdapter::buildEntries()
{
    pdos_.resize(1);
    pdos_[0].entries.reserve(regs_.size());

    for (const auto& r : regs_) {
        dynamichardware::pdo::EntryType type;
        if      (r.bitLength == 1U  && !r.isOutput) { type = dynamichardware::pdo::EntryType::BoolInput;     }
        else if (r.bitLength == 1U  &&  r.isOutput) { type = dynamichardware::pdo::EntryType::BoolOutput;    }
        else if (r.bitLength == 32U && !r.isOutput) { type = dynamichardware::pdo::EntryType::Int32Input;    }
        else if (r.bitLength == 16U && !r.isOutput) { type = dynamichardware::pdo::EntryType::Int16Input;    }
        else {
            // Unsupported width — registered in domain for offset resolution,
            // but not yet wrapped as a typed entry.
            continue;
        }

        std::printf("[EtherCAT]   uuid=%-40s %s bits=%-2u byte=%-4u bit=%u idx=0x%04X:%02X\n",
                    r.uuid.c_str(),
                    r.isOutput ? "OUT" : "IN ",
                    r.bitLength, r.offset, r.bitPos,
                    r.pdoIndex, r.pdoSubindex);

        dynamichardware::pdo::PDOEntry e{};
        e.image      = domainData_ + r.offset;          // direct into IgH buffer
        e.byteOffset = r.offset;                        // retained for reference
        e.bitOffset  = static_cast<uint8_t>(r.bitPos);
        e.bitLength  = r.bitLength;
        e.uuid       = r.uuid;
        e.type       = type;
        pdos_[0].entries.push_back(e);
    }

    // PDO::image is empty — domainData_ is IgH-managed.
    // freeze() sees image.empty() and leaves entry image pointers untouched.
    pdos_[0].freeze();
}

// ---------------------------------------------------------------------------
// waitForCommunication()
// ---------------------------------------------------------------------------

bool EthercatAdapter::waitForCommunication(uint32_t timeoutMs)
{
#ifdef ETHERCAT_AVAILABLE
    const uint32_t intervalUs = (cycleNs_ / 1000U);
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
// onBeforeReadInputs() — called every cycle, before reading PDO data
// ---------------------------------------------------------------------------

void EthercatAdapter::onBeforeReadInputs() noexcept
{
    if (!master_) return;
#ifdef ETHERCAT_AVAILABLE
    // 1. Timestamp application time to master for DC reference clock alignment
    const uint64_t appTimeNs = clockNowNs();
    ecrt_master_application_time(master_, appTimeNs);

    // 2. Receive all queued EtherCAT frames
    ecrt_master_receive(master_);

    // 3. Decode received data into domain buffer
    ecrt_domain_process(domain_);

    // 4. Read domain state (working counter, wc_state)
    ecrt_domain_state(domain_, &lastDomainState_);

    cycleCount_.fetch_add(1U, std::memory_order_relaxed);
#endif
}

// ---------------------------------------------------------------------------
// onAfterWriteOutputs() — called every cycle, after writing PDO outputs
// ---------------------------------------------------------------------------

void EthercatAdapter::onAfterWriteOutputs() noexcept
{
    if (!master_) return;
#ifdef ETHERCAT_AVAILABLE
    // 1. Synchronise reference clock to application time
    const uint64_t nowNs = clockNowNs();
    ecrt_master_application_time(master_, nowNs);
    ecrt_master_sync_reference_clock(master_);

    // 2. Propagate time to all slave clocks in the DC topology
    ecrt_master_sync_slave_clocks(master_);

    // 3. Stage domain data for the next frame
    ecrt_domain_queue(domain_);

    // 4. Transmit queued EtherCAT frames
    ecrt_master_send(master_);
#endif
}

// ---------------------------------------------------------------------------
// applyConfig() — called once after buildEntries(); never in the RT loop
// ---------------------------------------------------------------------------

void EthercatAdapter::applyConfig()
{
    if ((config_ == nullptr) || config_->slaves.empty()) {
        return;
    }

    // Build UUID → SlaveConfig lookup for pulse/debounce overrides
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

        for (auto& e : pdos_[0].entries) {
            if (e.uuid.empty() || e.uuid != reg.uuid) continue;

            // Apply pulse/debounce from the common config if available
            if (reg.isOutput) {
                e.configurePulseMs(100); // Default pulse — can be overridden
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

} // namespace dynamichardware::ethercat
