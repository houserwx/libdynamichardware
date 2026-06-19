#pragma once
#include "dynamichardware/pdo/IDiscoveryBackend.h"
#include <cstdint>

// Guard IgH EtherCAT headers — only available when libethercat is installed.
#ifdef ETHERCAT_AVAILABLE
extern "C" {
#include <ecrt.h>
}
#endif

namespace dynamichardware::ethercat {

/// ---- EthercatDiscovery ---------------------------------------------------
/// One-shot hardware scanner for EtherCAT buses. Implements IDiscoveryBackend.
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
class EthercatDiscovery final : public dynamichardware::pdo::IDiscoveryBackend {
public:
    /// @param cycleNs  Cycle period in nanoseconds used for DC sync configuration hints.
    explicit EthercatDiscovery(uint32_t cycleNs = 1'000'000u) noexcept
        : cycleNs_(cycleNs) {}

    ~EthercatDiscovery() override;

    // setCatalog inherited from IDiscoveryBackend.

    /// Acquire master, scan slaves on bus, populate catalog, then release master+domain.
    [[nodiscard]] bool discover() override;

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

    int               nSlaves_{0};

    bool discoverSlaves();
};

} // namespace dynamichardware::ethercat
