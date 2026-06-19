#pragma once
#include "dynamichardware/pdo/IDiscoveryBackend.h"

#include <string>

// ============================================================================
// SimulatedDiscovery — one-shot simulated channel scanner.
//
// Separation rationale (ISP): Discovery is a transient init-time concern.
// DISCOVERY DISCOVERS WHAT HARDWARE IS AVAILABLE (CHANNELS) AND POPULATES CATALOG. THAT IS ALL.
// After discover() completes, this object can be destroyed entirely.
// No state survives into frozen RT mode.
//
// Lifecycle:
//   1. Context creates SimulatedDiscovery instance (with definitions JSON path)
//   2. Calls setCatalog(&catalog) so discovered channels get registered
//   3. Calls discover() → reads JSON definitions, populates HardwareCatalog with simulated entries
//   4. Discovery object destroyed or discarded after consumer configuration phase
//
// Special behavior per user requirement: if the consumer adds new entries via
// SimulatedDefinitionBuilder which modifies the JSON file, DynamicHardwareContext
// will call discover() again to refresh the catalog with those additions.
// ============================================================================

namespace dynamichardware::simulated {

class SimulatedDiscovery final : public dynamichardware::pdo::IDiscoveryBackend {
public:
    explicit SimulatedDiscovery(std::string definitionsPath);
    ~SimulatedDiscovery() override = default;

    // Non-copyable, non-movable
    SimulatedDiscovery(const SimulatedDiscovery&)            = delete;
    SimulatedDiscovery& operator=(const SimulatedDiscovery&) = delete;
    SimulatedDiscovery(SimulatedDiscovery&&)                 = delete;
    SimulatedDiscovery& operator=(SimulatedDiscovery&&)      = delete;

    /// Read JSON definitions and populate catalog with simulated channel entries.
    /// Returns true if at least one simulated entry was loaded (or zero entries gracefully).
    [[nodiscard]] bool discover() override;

private:
    std::string definitionsPath_;
};

} // namespace dynamichardware::simulated
