#pragma once

// ============================================================================
// DynamicHardwareBuilder — high-level fluent API for hardware initialization.
//
// Replaces DynamicHardwareContextFactory with a cleaner interface that depends
// on abstractions (IBackendScanner / IRuntimeAdapter) rather than concrete types.
// Fixes Issue E (SRP violation) by delegating coordination to HardwareOrchestrator.
//
// Usage:
//   auto ctx = DynamicHardwareBuilder{}
//       .catalogPath("hardware.json")
//       .enableBackend("GPIO")
//       .enableBackend("EtherCAT", nlohmann::json{{"cycleNs", 1000000}})
//       .discover();            // scan all enabled backends via scan() → catalog
//                               // ... user inspects builder.catalog() if needed ...
//       .mapChannel("uuid-abc123", dhdo::EntryType::BOOL_OUTPUT, "Valve 1")
//       .buildRT();             // orchestrator filters channels per-backend, calls build(channels)
// ============================================================================

#include "dynamichardware/dhdo/HardwareCatalog.h"
#include "dynamichardware/dhdo/DHDO.h"
#include "dynamichardware/config/PhaseManager.h"

// Full definitions needed for unique_ptr destructors
#include "dynamichardware/DynamicHardwareContextObject.h"
#include "dynamichardware/HardwareOrchestrator.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dynamichardware {

class DynamicHardwareBuilder {
public:
    DynamicHardwareBuilder();
    ~DynamicHardwareBuilder() = default;

    // ---- Fluent configuration ----

    /// Set path for persisted hardware catalog JSON (default: "hardware.json").
    DynamicHardwareBuilder& catalogPath(std::string path);

    /// Enable a backend by name with optional key-value configuration.
    /// Backend names: "EtherCAT", "GPIO", "I2C", "SPI", "Simulated".
    /// Config values are passed as strings (e.g., {"cycleNs": "1000000"}).
    DynamicHardwareBuilder& enableBackend(
        std::string name,
        const std::unordered_map<std::string, std::string>& config = {});

    // ---- Channel mapping (consumer explicitly maps channels after discovery) ----

    /// Define a channel by catalog entry UUID, specifying its EntryType.
    /// Call this between discover() and buildRT().
    DynamicHardwareBuilder& mapChannel(
        const std::string&  keyOrUuid,
        dhdo::EntryType     type,
        const std::string&  friendlyName = "");

    // ---- Mapping persistence ----

    /// Set path for persisted channel mappings JSON file (default: no persistence).
    DynamicHardwareBuilder& mappingPath(std::string path);

    /// Load previously persisted mappings into the internal definition list.
    size_t loadMappings();

    // ---- Discovery phase ----

    /// Run one-shot discovery across all enabled backends using scan() pipeline.
    bool discover();

    // ---- Access to populated catalog (for user inspection / augmentation) ----

    [[nodiscard]] const dhdo::HardwareCatalog& catalog() const noexcept;
    [[nodiscard]]       dhdo::HardwareCatalog& catalog()       noexcept;

    // ---- Build RT context from discovered data ----

    /// Create and return a runtime context object with all RT backends built.
    std::unique_ptr<DynamicHardwareContextObject> buildRT();

private:
    std::unique_ptr<HardwareOrchestrator> orchestrator_;
};

} // namespace dynamichardware
