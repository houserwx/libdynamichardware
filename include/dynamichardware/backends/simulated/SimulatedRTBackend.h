#pragma once
#include "dynamichardware/pdo/IRTBackend.h"

#include <string>
#include <vector>
#include <cstdint>

namespace dynamichardware::simulated {

/// ---- SimulatedRTBackend --------------------------------------------------
/// Real-time simulated I/O backend. Implements IRTBackend.
///
/// Fully independent of discovery: reads catalog entries discovered by
/// SimulatedDiscovery, builds PDOs + simulation state from scratch in buildRT().
/// Generates synthetic waveforms (square-wave toggle, linear increment, sinusoidal)
/// in onBeforeReadInputs() — no real hardware involved.
class SimulatedRTBackend final : public dynamichardware::pdo::IRTBackend {
public:
    explicit SimulatedRTBackend(std::string definitionsPath);
    ~SimulatedRTBackend() override = default;

    // --- IRTBackend implementation ------------------------------------------
    [[nodiscard]] bool buildRT() override;
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

private:
    struct SimState {
        dynamichardware::pdo::EntryType type{dynamichardware::pdo::EntryType::BoolInput};
        uint32_t  byteOffset{0};

        // Bool: periodic square-wave toggle state machine
        uint32_t  togglePeriodCycles{0};     ///< Full period in RT cycles
        uint32_t  highCycles{0};             ///< HIGH duration in cycles
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
        double    phaseIncrement{0.0};       ///< Phase delta per RT cycle
        float     floatAmplitude{1.0f};      ///< Peak deviation from offset
        float     floatOffset{0.0f};         ///< DC offset added to sine output
    };

    std::string definitionsPath_;
    uint32_t                  cycleNs_{500'000};
    double                    cycleNsD_{500'000.0};
    std::vector<SimState> simStates_;

    /// Read JSON definitions and extract cycle time + simulated entries from catalog_.
    bool loadDefinitions() noexcept;
};

} // namespace dynamichardware::simulated
