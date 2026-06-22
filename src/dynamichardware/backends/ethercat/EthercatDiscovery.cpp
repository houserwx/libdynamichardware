#include "dynamichardware/backends/ethercat/EthercatDiscovery.h"
#include "dynamichardware/backends/ethercat/EthercatRTBackend.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"
#include "dynamichardware/dhdo/HardwareDescriptor.h"
#include "dynamichardware/backends/ethercat/SlaveTypeInfo.h"
#include "dynamichardware/backends/ethercat/EthercatEntryKey.h"

#include "dynamichardware/backends/registration.h"

#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace dynamichardware::ethercat {

void EthercatDiscovery::configure(const std::unordered_map<std::string, std::string>& config)
{
    auto it = config.find("cycleNs");
    if (it != config.end()) {
        cycleNs_ = static_cast<uint32_t>(std::stoul(it->second));
    }
}

// ---------------------------------------------------------------------------
// IBackenScanner::scan() — pure data scan: acquire master -> walk slaves/PDOs -> return descriptors.
// Does NOT mutate catalog_. All hardware probing logic lives here.
// Releases all resources before returning (one-shot probe).
// ---------------------------------------------------------------------------
std::vector<dhdo::HardwareDescriptor> EthercatDiscovery::scan()
{
    std::vector<dhdo::HardwareDescriptor> results;

#ifdef ETHERCAT_AVAILABLE
    // 1. Acquire master
    master_ = ecrt_request_master(0);
    if (master_ == nullptr) {
        std::fprintf(stderr, "[EtherCAT] Cannot acquire master - kernel module loaded?\n");
        return results;
    }

    // 2. Create process-data domain (needed to resolve PDO geometry / offsets)
    domain_ = ecrt_master_create_domain(master_);
    if (domain_ == nullptr) {
        std::fprintf(stderr, "[EtherCAT] Failed to create domain\n");
        reset();
        return results;
    }

    // 3. Walk slaves on bus and build descriptors from discovered PDO entries.
    ec_master_state_t ms{};
    ecrt_master_state(master_, &ms);
    const int total = static_cast<int>(ms.slaves_responding);

    std::printf("[EtherCAT] Master state: %u slave(s) responding, link=%s\n",
                ms.slaves_responding, (ms.al_states != 0U) ? "up" : "down");

    if (total == 0) {
        std::fprintf(stderr, "[EtherCAT] No slaves responding on bus\n");
        reset();
        return results;
    }

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

        // Walk EEPROM sync managers and discover all PDO entries -> build descriptors.
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

                    dhdo::HardwareDescriptor desc{};
                    desc.channelType = ctype;
                    desc.name        = chanName;
                    desc.isOutput    = isOutput;
                    desc.backend     = dhdo::BackendType::ETHERCAT;
                    desc.backendData = dhdo::EthercatBackendData{
                        si.vendor_id,       /* vendorId */
                        si.product_code,    /* productCode */
                        si.revision_number, /* revisionNumber */
                        pos,                /* slavePos */
                        entry.index,        /* pdoIndex */
                        entry.subindex      /* pdoSubindex */
                    };

                    // Assign stable UUID using the single source of truth from EthercatEntryKey.h.
                    // This MUST match what EthercatRTBackend::buildRT() generates so catalog lookup succeeds.
                    desc.uuid = buildEntryKey(si.vendor_id, si.product_code, isOutput,
                                              entry.bit_length, pos, entry.index, entry.subindex);

                    results.push_back(std::move(desc));
                    ++entryCountForSlave;
                }
            }
        }

        if (entryCountForSlave > 0) {
            std::printf("[EtherCAT]   -> found %d PDO entries\n", entryCountForSlave);
        } else {
            std::printf("[EtherCAT]   -> no mappable PDO entries found\n");
        }
    }

#else
    std::fprintf(stderr, "[EthercatDiscovery] EtherCAT not available - stub mode\n");
#endif

    // Always release everything — discovery is a one-shot scan.
    reset();
    return results;
}

// ---------------------------------------------------------------------------
// discover() — thin wrapper: calls scan(), feeds results into catalog_.
// Acquires master, scans slaves on bus, populates catalog, releases all resources.
// ---------------------------------------------------------------------------
bool EthercatDiscovery::discover()
{
    auto descriptors = scan();

    if (!catalog_) {
        std::fprintf(stderr, "[EC-Discovery] No catalog attached\n");
        return false;
    }

    for (auto& desc : descriptors) {
        dhdo::CatalogEntry entry{};
        entry.channelType = desc.channelType;
        entry.name        = desc.name;
        entry.slaveName   = desc.name;  // Will be refined in later phases from backendData
        entry.isOutput    = desc.isOutput;
        entry.backend     = desc.backend;
        entry.backendData = std::move(desc.backendData);

        catalog_->addEntry(std::move(entry));
    }

    if (!descriptors.empty()) {
        std::printf("[EtherCAT] Discovery complete: %d slave(s), %zu PDO entries registered\n",
                    nSlaves_, descriptors.size());
    } else {
        std::fprintf(stderr, "[EtherCAT] Discovery found no PDOs\n");
    }

    return !descriptors.empty();
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
// Self-registration with BackendRegistry — zero-boilerplate OCP compliance.
// ---------------------------------------------------------------------------
REGISTER_BACKEND("EtherCAT", []() {
    return std::make_pair(
        std::make_unique<ethercat::EthercatDiscovery>(),
        std::make_unique<ethercat::EthercatRTBackend>()
    );
});

} // namespace dynamichardware::ethercat
