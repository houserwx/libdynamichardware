#pragma once

// ============================================================================
// HardwareOrchestrator — internal phase coordination + backend iteration logic.
// 
// Separates the orchestration concerns (phase ordering, catalog lifecycle,
// backend dispatch) from the public fluent API (DynamicHardwareBuilder).
// Fixes Issue E (SRP violation — god class had too many responsibilities).
// 
// Uses BackendRegistry for OCP-compliant dispatch — zero hardcoded branches per
// transport. Backends self-register at static init time via REGISTER_BACKEND macro.
// Exclusively operates through interfaces: scan() for discovery, buildRT()/build()
// for RT construction.
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
namespace dhdo { class IRuntimeAdapter; }

/// Internal state held by orchestrator during all phases.
/// OCP-compliant: adding new backends requires zero changes to this struct.
struct OrchestratorState {
    std::string catalogPath{"hardware.json"};
    std::string mappingPath;  ///< Optional path for persisted channel mappings JSON

    /// Enabled backends as name -> {config_key -> config_value} map.
    /// Builder.enableBackend(name, config) populates this directly.
    /// Examples:
    ///   "EtherCAT" -> {"cycleNs": "500000"}
    ///   "I2C"      -> {"busPath": "/dev/i2c-1"}
    ///   "Simulated" -> {"definitionsPath": "/path/to/defs.json"}
    std::unordered_map<std::string,
                       std::unordered_map<std::string, std::string>> enabledBackends;
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

    // Discovery phase — iterate over enabled backends via BackendRegistry, call scan(), feed into catalog.
    bool runDiscoveryScan();

    /// Adapters created during discovery, stored for buildRT() to configure+build.
    /// Key = backend name from enabledBackends map (e.g., "EtherCAT", "GPIO").
    std::unordered_map<std::string, std::unique_ptr<dhdo::IRuntimeAdapter>> pendingAdapters_;
};

} // namespace dynamichardware
