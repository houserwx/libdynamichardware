#pragma once

// ============================================================================
// DynamicHardwareContextFactory — one-shot hardware scan and catalog update.
//
// Fluent API: enable backends → discover() scans physical hardware → user inspects/augments catalog
// → buildRT() creates DynamicHardwareContextObject with all RT backends built.
// Factory is destroyed after buildRT(); it holds no persistent resources.
//
// Usage:
//   auto ctx = DynamicHardwareContextFactory{}
//       .catalogPath("hardware.json")
//       .withEthercat(1'000'000u)
//       .discover();            // scan hardware, populate/purge catalog, save JSON
//                               // ... user can inspect factory.catalog() here if needed ...
//   ctx->buildRT();             // create RT backends, return context object
// ============================================================================

#include "dynamichardware/dhdo/HardwareCatalog.h"

#include <memory>
#include <optional>
#include <string>

namespace dynamichardware {

class DynamicHardwareContextObject;

class DynamicHardwareContextFactory {
public:
    // ---- Fluent configuration ----

    /// Set path for persisted hardware catalog JSON (default: "hardware.json").
    DynamicHardwareContextFactory& catalogPath(std::string path);

    /// Enable EtherCAT backend with optional cycle time in nanoseconds.
    DynamicHardwareContextFactory& withEthercat(uint32_t cycleNs = 1'000'000u);

    /// Enable GPIO backend (auto-detects Raspberry Pi variant).
    DynamicHardwareContextFactory& withGPIO();

    /// Enable I2C backend on the given bus path.
    DynamicHardwareContextFactory& withI2C(std::string busPath = "/dev/i2c-1");

    /// Enable SPI backend on the given device path.
    DynamicHardwareContextFactory& withSPI(std::string busPath = "/dev/spidev0.0");

    /// Enable simulated backend (no real hardware needed).
    DynamicHardwareContextFactory& withSimulation(
        std::optional<std::string> definitionsPath = std::nullopt);

    // ---- Discovery phase ----

    /// Run one-shot discovery across all enabled backends.
    /// Loads existing catalog → scans hardware → purges stale entries → saves JSON.
    bool discover();

    // ---- Access to populated catalog (for user inspection / augmentation) ----

    [[nodiscard]] const dhdo::HardwareCatalog& catalog() const noexcept;
    [[nodiscard]]       dhdo::HardwareCatalog& catalog()       noexcept;

    // ---- Build RT context from discovered data ----

    /// Create and return a runtime context object with all RT backends built.
    /// Caller takes ownership via unique_ptr; factory is no longer usable after this call.
    std::unique_ptr<DynamicHardwareContextObject> buildRT();

private:
    struct State {
        std::string catalogPath{"hardware.json"};

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

    State state_;
    dhdo::HardwareCatalog catalog_;
};

} // namespace dynamichardware
