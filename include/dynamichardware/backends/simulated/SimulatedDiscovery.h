#pragma once
#include "dynamichardware/dhdo/IBackendScanner.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"

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

class SimulatedDiscovery final
    : public dynamichardware::dhdo::IBackendScanner {
public:
    explicit SimulatedDiscovery(std::string definitionsPath);
    ~SimulatedDiscovery() override = default;

    /// Attach target catalog — discover() will register entries here after scan().
    void setCatalog(dhdo::HardwareCatalog* catalog) noexcept { catalog_ = catalog; }

    // Non-copyable, non-movable
    SimulatedDiscovery(const SimulatedDiscovery&)            = delete;
    SimulatedDiscovery& operator=(const SimulatedDiscovery&) = delete;
    SimulatedDiscovery(SimulatedDiscovery&&)                 = delete;
    SimulatedDiscovery& operator=(SimulatedDiscovery&&)      = delete;

    /// Pure data scan — parse JSON into HardwareDescriptor vector, no catalog mutation.
    [[nodiscard]] std::vector<dhdo::HardwareDescriptor> scan() override;

    /// Legacy wrapper — calls scan(), feeds results into catalog_.
    [[nodiscard]] bool discover();

private:
    std::string            definitionsPath_;
    dhdo::HardwareCatalog* catalog_{nullptr};
};

} // namespace dynamichardware::simulated
