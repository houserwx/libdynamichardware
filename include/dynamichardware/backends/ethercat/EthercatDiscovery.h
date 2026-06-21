#pragma once
#include "dynamichardware/dhdo/IBackendScanner.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"
#include <cstdint>

// Guard IgH EtherCAT headers — only available when libethercat is installed.
#ifdef ETHERCAT_AVAILABLE
extern "C" {
#include <ecrt.h>
}
#endif

namespace dynamichardware::ethercat {

/// ---- EthercatDiscovery ---------------------------------------------------
/// One-shot hardware scanner for EtherCAT buses.
///
/// Pure discovery: scans bus → populates catalog → releases everything.
/// No shared state with the RT backend; acts like a standalone scan tool.
/// Only purpose: build a list of discovered hardware in the HardwareCatalog.
///
/// Lifecycle:
///   1. setCatalog(&catalog)          — attach target catalog
///   2. discover()                    — acquire master, create domain, walk slaves,
///                                      register entries in catalog, release all resources.
///   3. Object destroyed or reset()   — cleanup (idempotent).
class EthercatDiscovery final
    : public dynamichardware::dhdo::IBackendScanner {
public:
    /// @param cycleNs  Cycle period in nanoseconds used for DC sync configuration hints.
    explicit EthercatDiscovery(uint32_t cycleNs = 1'000'000u) noexcept
        : cycleNs_(cycleNs) {}

    ~EthercatDiscovery() override;

    /// Attach target catalog — discover() will register entries here after scan().
    void setCatalog(dhdo::HardwareCatalog* catalog) noexcept { catalog_ = catalog; }

    /// Set catalog pointer so discover() can feed results into it.

    /// Pure data scan — acquire master, walk slaves, return descriptors without mutating catalog.
    [[nodiscard]] std::vector<dhdo::HardwareDescriptor> scan() override;

    /// Legacy wrapper — calls scan(), feeds results into catalog_.
    [[nodiscard]] bool discover();

    /// Release all resources early if desired (also called by destructor).
    void reset() noexcept;

private:
#ifdef ETHERCAT_AVAILABLE
    ec_master_t*      master_{nullptr};
    ec_domain_t*      domain_{nullptr};
#else
    void*             master_{nullptr};
    void*             domain_{nullptr};
#endif
    uint32_t          cycleNs_;

    int                    nSlaves_{0};
    dhdo::HardwareCatalog* catalog_{nullptr};

    bool discoverSlaves();
};

} // namespace dynamichardware::ethercat
