#include "dynamichardware/backends/ethercat/EthercatDiscovery.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"
#include "dynamichardware/backends/ethercat/SlaveTypeInfo.h"
#include "dynamichardware/backends/ethercat/EthercatEntryKey.h"

#include <cstdio>
#include <thread>

namespace dynamichardware::ethercat {

// ---------------------------------------------------------------------------
// discover() — IDiscoveryBackend implementation
// Acquires master, creates domain, scans slaves, populates catalog.
// Releases all resources before returning.
// ---------------------------------------------------------------------------

bool EthercatDiscovery::discover()
{
#ifdef ETHERCAT_AVAILABLE
    // 1. Acquire master
    master_ = ecrt_request_master(0);
    if (master_ == nullptr) {
        std::fprintf(stderr, "[EtherCAT] Cannot acquire master — kernel module loaded?\n");
        return false;
    }

    // 2. Create process-data domain (needed to resolve PDO geometry / offsets)
    domain_ = ecrt_master_create_domain(master_);
    if (domain_ == nullptr) {
        std::fprintf(stderr, "[EtherCAT] Failed to create domain\n");
        reset();
        return false;
    }

    // 3. Discover slaves and register entries in catalog only — no RT setup.
    bool ok = discoverSlaves();

    if (ok) {
        std::printf("[EtherCAT] Discovery complete: %d slave(s)\n", nSlaves_);
    } else {
        std::fprintf(stderr, "[EtherCAT] Discovery failed\n");
    }

    // Always release everything — discovery is a one-shot scan.
    reset();
    return ok;

#else
    std::fprintf(stderr, "[EthercatDiscovery] EtherCAT not available — stub mode\n");
    return false;
#endif
}

void EthercatDiscovery::reset() noexcept
{
#ifdef ETHERCAT_AVAILABLE
    if (master_) {
        ecrt_release_master(master_);
        master_ = nullptr;
        domain_ = nullptr;
    } else {
        domain_ = nullptr;
    }
#endif
    nSlaves_ = 0;
}

EthercatDiscovery::~EthercatDiscovery()
{
    reset();
}

// ---------------------------------------------------------------------------
// discoverSlaves() — walk slaves on bus and populate catalog (no RT state)
// ---------------------------------------------------------------------------

bool EthercatDiscovery::discoverSlaves()
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

    // We need a domain to resolve PDO offsets even in discovery-only mode.
    // Walk all slaves and register each discovered PDO entry into the catalog.
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

        // Walk EEPROM sync managers and discover all PDO entries → register in catalog.
        int entryCountForSlave = 0;
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

                    if (catalog_ != nullptr) {
                        // Single source of truth for key construction — guaranteed to match RT backend.
                        std::string k = buildEntryKey(
                            si.vendor_id, si.product_code,
                            isOutput, entry.bit_length,
                            pos, entry.index, entry.subindex);

                        // Build human-readable names from slave info or raw EEPROM data
                        std::string slaveName;
                        if (sti != nullptr) {
                            slaveName = sti->type_name;
                        } else {
                            slaveName = static_cast<const char*>(si.name);
                        }
                        std::string chanName = slaveName + "[pos" + std::to_string(static_cast<int>(pos)) +
                                               "] ch" + std::to_string(static_cast<int>(entry.subindex));

                       const char* ctype = inferChannelType(entry.bit_length, isOutput);
                        dynamichardware::dhdo::CatalogEntry catEntry{};
                        // UUID will be auto-generated from backendData in addEntry().
                        catEntry.channelType = ctype;
                        catEntry.name        = chanName;
                        catEntry.slaveName   = slaveName;
                        catEntry.isOutput    = isOutput;
                        catEntry.backend     = dynamichardware::dhdo::BackendType::ETHERCAT;
                        catEntry.backendData = dynamichardware::dhdo::EthercatBackendData{
                            si.vendor_id,       /* vendorId */
                            si.product_code,    /* productCode */
                            si.revision_number, /* revisionNumber */
                            pos,                /* slavePos */
                            entry.index,        /* pdoIndex */
                            entry.subindex      /* pdoSubindex */
                        };
                        catalog_->addEntry(std::move(catEntry));
                        ++entryCountForSlave;
                    }
                }
            }
        }

        if (entryCountForSlave > 0) {
            std::printf("[EtherCAT]   → registered %d PDO entries in catalog\n", entryCountForSlave);
        } else {
            std::printf("[EtherCAT]   → no mappable PDO entries found\n");
        }
    }

    return nSlaves_ > 0;

#else
    (void)0;
    return false;
#endif
}

} // namespace dynamichardware::ethercat
