#pragma once

// ============================================================================
// HardwareOrchestrator — internal phase coordination + backend iteration logic.
// 
// Separates the orchestration concerns (phase ordering, catalog lifecycle,
// backend dispatch) from the public fluent API (DynamicHardwareBuilder).
// Fixes Issue E (SRP violation — god class had too many responsibilities).
// 
// Still uses explicit backend types internally (OCP fix deferred to future),
// but exclusively through new interfaces: scan() for discovery, build(channels)
// for RT construction. Clean separation enables focused unit tests.
// ============================================================================

#include "dynamichardware/dhdo/HardwareCatalog.h"
#include "dynamichardware/dhdo/DHDO.h"
#include "dynamichardware/config/PhaseManager.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dynamichardware {

class DynamicHardwareContextObject;

/// Internal state held by orchestrator during all phases.
struct OrchestratorState {
    std::string catalogPath{"hardware.json"};
    std::string mappingPath;  ///< Optional path for persisted channel mappings JSON
    
    // Backend enable flags + configuration strings (parsed from builder's config map)
    bool enableEthercat{false};
    uint32_t ethercatCycleNs{1'000'000u};
    
    bool enableGPIO{false};
    
    bool enableI2C{false};
    std::string i2cBusPath{"/dev/i2c-1"};
    
    bool enableSPI{false};
    std::string spiBusPath{"/dev/spidev0.0"};
    
    bool enableSimulation{false};
    std::optional<std::string> simDefinitionsPath;
};

struct ChannelDefinition {
    std::string keyOrUuid;     ///< Catalog entry key or UUID to map
    dhdo::EntryType type;      ///< Direction + value format for DHDOEntry
    std::string friendlyName;  ///< User-assigned display name (optional)
};

class DynamicHardwareBuilder; // Forward declaration for friend
class HardwareOrchestrator {
public:
    friend class DynamicHardwareBuilder;
    
    explicit HardwareOrchestrator(OrchestratorState state);

    // ---- Phase transitions (enforced by PhaseManager internally) ----

    /// Run one-shot discovery across all enabled backends using scan() → catalog pipeline.
    bool discover();

    /// Build RT context with all backends constructed via build(channels).
    std::unique_ptr<DynamicHardwareContextObject> buildRT();

    // ---- Mapping persistence helpers ----

    size_t loadMappings();
    void saveMappings();

    // ---- Fluent-style setters (called by Builder before phase transitions) ----

    void addChannelDefinition(const std::string& keyOrUuid, 
                               dhdo::EntryType type, 
                               const std::string& friendlyName = "");

    // ---- Read-only accessors for Builder delegation ----

    [[nodiscard]] const dhdo::HardwareCatalog& catalog() const noexcept;
    [[nodiscard]]       dhdo::HardwareCatalog& catalog()       noexcept;

private:
    OrchestratorState state_;
    dhdo::HardwareCatalog catalog_;
    config::PhaseManager phaseManager_;
    
    std::vector<ChannelDefinition> channelDefs_;  ///< Consumer-defined channels
    
    // Discovery phase — iterate over enabled backends, call scan(), feed into catalog.
    bool runDiscoveryScan();
};

} // namespace dynamichardware
