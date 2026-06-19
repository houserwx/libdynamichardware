#pragma once

// ============================================================================
// DynamicHardwareContext.h — single entry point for libdynamichardware.
//
// Wraps the internal layers (HardwareRegistry, HardwareCatalog, adapters)
// behind one composable, state-machine lifecycle:
//
//   build()  → create adapters, discover hardware, populate catalog
//   freeze() → lock PDOs, build UUID map, ready for RT
//   run()    → readAll() / writeAll() cycle
//
// Usage:
//   auto ctx = DynamicHardwareContext::create(configPath);
//   ctx->build();
//   ctx->freeze();
//
//   while (running) {
//       signalProcessTickNow();
//       ctx->readAll();
//       // ... application logic via ctx->lookupByUuid() ...
//       ctx->writeAll();
//   }
// ============================================================================

#include "dynamichardware/pdo/PDO.h"
#include "dynamichardware/pdo/HardwareRegistry.h"
#include "dynamichardware/pdo/HardwareCatalog.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace dynamichardware {

// Forward declaration (shared_ptr handles incomplete types)
class DynamicHardwareContext;

// ---------------------------------------------------------------------------
// SimulatedDefinitionBuilder — fluent builder for creating simulated adapter
// definitions (JSON files consumed by SimulatedAdapter::loadDefinitions).
//
// Usage:
//   auto defs = SimulatedDefinitionBuilder::create()
//       .cycleTimeUs(500)
//       .encoder("Encoder-A", "virt-enc-a-0001")
//           .rpm(3000.0f).rollerDiamMm(50.0f).resolutionPpr(1024)
//       .digitalInput("LimitSwitch-1", "virt-di-0001")
//           .partsPerMin(120.0f).variancePercent(5.0f)
//       .digitalOutput("Solenoid-1", "virt-do-0001")
//           .pulseMs(100)
//       .analogInput("TempSensor-1", "virt-ai-0001")
//       .save("SimulatedAdapterDefinitions.json");
// ---------------------------------------------------------------------------
class SimulatedDefinitionBuilder {
public:
    /// Create a new builder (static factory).
    static SimulatedDefinitionBuilder create();

    /// Set cycle time in microseconds.
    SimulatedDefinitionBuilder& cycleTimeUs(int us);

    /// Add an encoder channel. Returns *this for chaining sim params.
    SimulatedDefinitionBuilder& encoder(const std::string& name, const std::string& uuid);

    /// Add a digital input channel.
    SimulatedDefinitionBuilder& digitalInput(const std::string& name, const std::string& uuid);

    /// Add a digital output channel.
    SimulatedDefinitionBuilder& digitalOutput(const std::string& name, const std::string& uuid);

    /// Add an analog input channel.
    SimulatedDefinitionBuilder& analogInput(const std::string& name, const std::string& uuid);

    /// Add an analog output channel.
    SimulatedDefinitionBuilder& analogOutput(const std::string& name, const std::string& uuid);

    // ---- Simulation parameters (applied to the last-added channel) ----

    SimulatedDefinitionBuilder& rpm(float v);
    SimulatedDefinitionBuilder& rollerDiamMm(float v);
    SimulatedDefinitionBuilder& resolutionPpr(uint32_t v);
    SimulatedDefinitionBuilder& quadrature(bool v = true);
    SimulatedDefinitionBuilder& partsPerMin(float v);
    SimulatedDefinitionBuilder& partWidthMm(float v);
    SimulatedDefinitionBuilder& variancePercent(float v);
    SimulatedDefinitionBuilder& pulseMs(uint32_t v);
    SimulatedDefinitionBuilder& debounceMs(uint32_t v);

    /// Save definitions to a JSON file. Returns true on success.
    bool save(const std::string& path) const;

    /// Get the JSON string representation.
    std::string toJson() const;

private:
    struct Channel {
        std::string name;
        std::string uuid;
        std::string channelType;
        pdo::CatalogEntry::SimParams sim{};
    };

    std::vector<Channel> channels_;
    uint32_t cycleTimeUs_{1000};

    Channel& lastChannel();
};

// ---------------------------------------------------------------------------
// Builder — fluent API for constructing a context.
// ---------------------------------------------------------------------------
class DynamicHardwareContextBuilder {
public:
    /// Path to the persisted hardware catalog JSON.
    /// Loaded on build(), saved after discovery if channels changed.
    DynamicHardwareContextBuilder& catalogPath(std::string path);

    /// Path to optional backend-specific configuration JSON.
    DynamicHardwareContextBuilder& configPath(std::string path);

    /// Enable EtherCAT backend. Optionally set cycle time (ns).
    DynamicHardwareContextBuilder& withEthercat(uint32_t cycleNs = 1'000'000u);

    /// Enable GPIO backend.
    DynamicHardwareContextBuilder& withGPIO();

    /// Enable I2C backend on the given bus path (default: /dev/i2c-1).
    DynamicHardwareContextBuilder& withI2C(std::string busPath = "/dev/i2c-1");

    /// Enable SPI backend on the given bus path (default: /dev/spidev0.0).
    DynamicHardwareContextBuilder& withSPI(std::string busPath = "/dev/spidev0.0");

    /// Enable simulated backend (no real hardware).
    /// Optionally load definitions from a JSON file.
    DynamicHardwareContextBuilder& withSimulation(std::optional<std::string> definitionsPath = std::nullopt);

    /// Build the context. Returns nullptr if required resources are unavailable.
    std::shared_ptr<DynamicHardwareContext> build();

private:
    friend class DynamicHardwareContext;

    struct State {
        std::string catalogPath{"hardware.json"};
        std::string configPath;

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
};

// ---------------------------------------------------------------------------
// DynamicHardwareContext — main library entry point.
// ---------------------------------------------------------------------------
class DynamicHardwareContext {
public:
    using SharedPtr = std::shared_ptr<DynamicHardwareContext>;

    // ---- Factory ----

    /// Create a builder for fluent context construction.
    static DynamicHardwareContextBuilder builder();

    // ---- Lifecycle (state machine) ----

    enum class State { PRE_BUILD, BUILT, FROZEN, SHUTDOWN };

    [[nodiscard]] State state() const noexcept { return state_; }

    /// Discover hardware, create adapters, populate catalog.
    /// Transitions from PRE_BUILD → BUILT.
    bool build();

    /// Lock PDOs, build UUID map, prepare for RT.
    /// Transitions from BUILT → FROZEN.
    bool freeze();

    /// Shut down backends, release resources.
    /// Transitions to SHUTDOWN.
    void shutdown();

    // ---- RT cycle (call from RT thread after freeze) ----

    /// Read all inputs from all backends.
    void readAll() noexcept;

    /// Write all outputs to all backends.
    void writeAll() noexcept;

    // ---- Channel access (init-time: lookup, RT-time: use cached pointers) ----

    /// Resolve a PDOEntry by UUID (init-time safe, RT-time safe if cached).
    [[nodiscard]] pdo::PDOEntry* lookupByUuid(std::string_view uuid) noexcept;

    /// Resolve a PDOEntry by name (init-time only, slower string search).
    [[nodiscard]] pdo::PDOEntry* lookupByName(std::string_view name) noexcept;

    /// List all discovered channels (init-time, debug/config).
    /// Use this to discover UUIDs, types, backend info, and physical pin mappings.
    [[nodiscard]] const std::vector<pdo::CatalogEntry>& catalogEntries() const noexcept;

    // ---- Health monitoring ----

    /// Returns number of registered backends.
    [[nodiscard]] std::size_t backendCount() const noexcept;

    /// Returns true if all backends report healthy communication.
    [[nodiscard]] bool allBackendsHealthy() const noexcept;

    /// Returns total PDOEntry count across all backends.
    [[nodiscard]] std::size_t entryCount() const noexcept;

    // ---- Debug ----

    /// Print full state (backends, PDOs, entries) to stdout.
    void printState() const;

private:
    friend DynamicHardwareContextBuilder;

    // Internal layer access (private — users interact via facade methods only)
    [[nodiscard]] pdo::HardwareRegistry& registry() noexcept;
    [[nodiscard]] pdo::HardwareCatalog& catalog() noexcept;

    DynamicHardwareContext(DynamicHardwareContextBuilder::State&& state);
    ~DynamicHardwareContext();

    DynamicHardwareContext(const DynamicHardwareContext&) = delete;
    DynamicHardwareContext& operator=(const DynamicHardwareContext&) = delete;

    struct Impl;
    Impl* impl_;

    State state_;
    DynamicHardwareContextBuilder::State builderState_;
};

} // namespace dynamichardware
