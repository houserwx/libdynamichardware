#pragma once
#include "dynamichardware/pdo/IDiscoveryBackend.h"
#include "dynamichardware/pdo/IRTBackend.h"
#include "dynamichardware/pdo/HardwareCatalog.h"
#include <vector>
#include <string>
#include <cstdint>

namespace dynamichardware::simulated {

// ============================================================\n// SimulatedAdapter — virtual hardware adapter for testing and simulation.\n//\n// Loads a JSON definitions file describing simulated channels,\n// registers them into the HardwareCatalog, builds PDO entries,\n// and generates synthetic I/O each cycle.\n//\n// Generic rate-of-change model (mirrors EntryType system):\n//   BoolInput    → periodic square-wave toggle (togglePeriodMs, dutyCyclePercent)\n//   Int*Input    → linear increment per cycle with optional bounds\n//                  (incrementPerCycle, minValue, maxValue)\n//   FloatInput   → sinusoidal oscillation (amplitude, frequencyHz, offset)\n//   Output types → pass-through / echo (no active simulation)\n//\n// Definitions JSON format:\n// {\n//   \"cycleTimeUs\": 500,\n//   \"channels\": [\n//     {\n//       \"name\": \"LimitSwitch-1\",\n//       \"uuid\": \"virt-di-0001\",\n//       \"channelType\": \"BoolInput\",\n//       \"sim\": { \"togglePeriodMs\": 100, \"dutyCyclePercent\": 30.0 }\n//     },\n//     {\n//       \"name\": \"Encoder-A\",\n//       \"uuid\": \"virt-enc-a-0001\",\n//       \"channelType\": \"Int32Input\",\n//       \"sim\": { \"incrementPerCycle\": 10 }\n//     },\n//     {\n//       \"name\": \"TempSensor-1\",\n//       \"uuid\": \"virt-fi-0001\",\n//       \"channelType\": \"FloatInput\",\n//       \"sim\": { \"amplitude\": 5.0, \"frequencyHz\": 0.5, \"offset\": 25.0 }\n//     }\n//   ]\n// }
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

        // Bool: periodic square-wave toggle state machine
        uint32_t  togglePeriodCycles{0};     ///< Full period in RT cycles (derived from ms)
        uint32_t  highCycles{0};             ///< HIGH duration in cycles (from dutyCyclePercent)
        uint32_t  lowCycles{0};              ///< LOW duration in cycles
        uint32_t  tickCount{0};              ///< Current position within period
        bool      isHigh{false};

        // Integer: linear increment with optional bounded sawtooth
        int64_t   intValue{0};               ///< Current accumulated value
        int32_t   incrementPerCycle{1};      ///< Delta per RT cycle
        int64_t   minValue{INT64_MIN};       ///< Lower bound (wraps if exceeded below)
        int64_t   maxValue{INT64_MAX};       ///< Upper bound (wraps if exceeded above)

        // Float: sinusoidal oscillation
        double    floatPhase{0.0};           ///< Current phase angle (radians)
        double    phaseIncrement{0.0};       ///< Phase delta per RT cycle (from frequencyHz + cycle time)
        float     floatAmplitude{1.0f};      ///< Peak deviation from offset
        float     floatOffset{0.0f};         ///< DC offset added to sine output
    };

    std::vector<SimState> simStates_;
};

} // namespace dynamichardware::simulated
