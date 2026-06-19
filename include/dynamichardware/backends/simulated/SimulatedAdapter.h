#pragma once
#include "dynamichardware/pdo/IDiscoveryBackend.h"
#include "dynamichardware/pdo/IRTBackend.h"
#include "dynamichardware/pdo/HardwareCatalog.h"
#include <vector>
#include <string>
#include <cstdint>

namespace dynamichardware::simulated {

// ============================================================
// SimulatedAdapter — virtual hardware adapter for testing and simulation.
//
// Loads a JSON definitions file describing simulated channels,
// registers them into the HardwareCatalog, builds PDO entries,
// and generates synthetic I/O each cycle.
//
// Definitions JSON format (SimulatedAdapterDefinitions.json):
// {
//   "cycleTimeUs": 500,
//   "channels": [
//     {
//       "name": "Encoder-A",
//       "uuid": "virt-enc-a-0001",
//       "channelType": "Encoder",
//       "sim": { "rpm": 3000.0, "rollerDiamMm": 50.0, "resolutionPpr": 1024 }
//     },
//     {
//       "name": "LimitSwitch-1",
//       "uuid": "virt-di-0001",
//       "channelType": "DigitalInput",
//       "sim": { "partsPerMin": 120.0, "variancePercent": 5.0 }
//     }
//   ]
// }
//
// Two-phase lifecycle (ISP split):
//
// DISCOVERY PHASE (IDiscoveryBackend — transient):
//   1. adapter.setCatalog(&catalog)
//   2. adapter.loadDefinitions(json_path) — populates catalog from JSON defs
//   3. discover() — validates catalog has simulated entries (trivial check)
//
// RT SETUP + CYCLE (IRTBackend — persistent through freeze):
//   4. buildRT() — constructs PDO structure from simulated catalog entries.
//   5. activate() — no-op (simulated backend needs no real activation).
//   6. onBeforeReadInputs()/onAfterWriteOutputs() — generate synthetic values.
// ============================================================

class SimulatedAdapter final
    : public dynamichardware::pdo::IDiscoveryBackend,
      public dynamichardware::pdo::IRTBackend {
public:
    SimulatedAdapter() = default;
    // setCatalog inherited from IDiscoveryBackend.
    ~SimulatedAdapter() override = default;

    // --- IDiscoveryBackend implementation -----------------------------------
    [[nodiscard]] bool discover() override;

    // --- IRTBackend implementation ------------------------------------------
    [[nodiscard]] bool buildRT() override;
    // activate() uses default no-op from IRTBackend.
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

    /// Load simulated channel definitions from a JSON file.
    /// Registers entries into the attached HardwareCatalog.
    bool loadDefinitions(const std::string& path);

    /// Set cycle time directly (alternative to loading from JSON).
    void setCycleTimeUs(int us) noexcept { cycleNs_ = static_cast<uint32_t>(us) * 1000u; }

    [[nodiscard]] std::size_t channelCount() const noexcept { return simStates_.size(); }

private:
    // catalog_ inherited from IDiscoveryBackend for discovery phase.
    uint32_t                  cycleNs_{500'000};
    double                    cycleNsD_{500'000.0};

    struct SimState {
        dynamichardware::pdo::EntryType type{dynamichardware::pdo::EntryType::BoolInput};
        uint32_t  byteOffset{0};

        // Encoder
        int64_t   count{0};
        int64_t   inc{10};
        int64_t   incScaled{0};
        int64_t   accumulator{0};

        // DigitalInput
        int32_t   halfHighTicks{0};
        int32_t   halfLowTicks{0};
        int32_t   nominalLowTicks{0};
        float     varianceFraction{0.0f};
        uint64_t  varianceSeed{1};
        bool      toggle{false};
        int       cycleTick{0};

        // AnalogInput
        int16_t   adc{0};
    };

    std::vector<SimState> simStates_;
};

} // namespace dynamichardware::simulated
