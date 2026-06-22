#pragma once
#include "dynamichardware/dhdo/IBackendScanner.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"

#include <string>
#include <unordered_map>

// ============================================================================
// SimulatedDiscovery — one-shot simulated channel scanner.
//
// Separation rationale (ISP): Discovery is a transient init-time concern.
// After discover() completes, this object can be destroyed entirely.
// No state survives into frozen RT mode.
// ============================================================================

namespace dynamichardware::simulated {

class SimulatedDiscovery final
    : public dynamichardware::dhdo::IBackendScanner {
public:
    /// Default constructor for self-registration via BackendRegistry.
    SimulatedDiscovery();

    /// Legacy parameterized constructor (retained for direct instantiation).
    explicit SimulatedDiscovery(std::string definitionsPath);
    ~SimulatedDiscovery() override = default;

    /// Inject per-backend configuration from orchestrator's enabledBackends map.
    void configure(const std::unordered_map<std::string, std::string>& config) override;

    /// Attach target catalog — discover() will register entries here after scan().
    void setCatalog(dhdo::HardwareCatalog* catalog) noexcept { catalog_ = catalog; }

    // Non-copyable, non-movable
    SimulatedDiscovery(const SimulatedDiscovery&)            = delete;
    SimulatedDiscovery& operator=(const SimulatedDiscovery&) = delete;
    SimulatedDiscovery(SimulatedDiscovery&&)                 = delete;
    SimulatedDiscovery& operator=(SimulatedDiscovery&&)      = delete;

    /// Pure data scan — parse JSON into HardwareDescriptor vector, no catalog mutation.
    [[nodiscard]] std::vector<dhdo::HardwareDescriptor> scan() override;

    /// Convenience wrapper — calls scan() through IBackendScanner, feeds results into catalog_.
    [[nodiscard]] bool discover();

private:
    std::string            definitionsPath_;
    dhdo::HardwareCatalog* catalog_{nullptr};
};

} // namespace dynamichardware::simulated
