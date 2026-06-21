#pragma once
#include "dynamichardware/dhdo/IRuntimeAdapter.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"

#include <string>
#include <vector>
#include <cstdint>

namespace dynamichardware::simulated {

/// ---- SimulatedRTBackend --------------------------------------------------
/// Real-time simulated I/O backend.
///
/// Fully independent of discovery: parses JSON simdefs directly and builds
/// PDOs + simulation state from scratch in buildRT(). Same source file that
/// SimulatedDiscovery reads for catalog population, but RT backend owns its own data.
/// Generates synthetic waveforms (square-wave toggle, linear increment, sinusoidal)
/// in onBeforeReadInputs() — no real hardware involved.
class SimulatedRTBackend final
    : public dynamichardware::dhdo::IRuntimeAdapter {
public:
    explicit SimulatedRTBackend(std::string definitionsPath);
    ~SimulatedRTBackend() override = default;

    [[nodiscard]] const std::string& definitionsPath() const noexcept { return definitionsPath_; }

    // --- RT lifecycle methods ----------------------------------------------
    [[nodiscard]] bool buildRT();
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

    // --- Builder interface -------------------------------------------------
    void setCatalog(const dynamichardware::dhdo::HardwareCatalog* catalog) noexcept;
    [[nodiscard]] bool build(const std::vector<dynamichardware::dhdo::MappedChannel>& channels) override;

 private:
    const dynamichardware::dhdo::HardwareCatalog* catalog_{nullptr};
    struct SimState {
        dynamichardware::dhdo::EntryType type{dynamichardware::dhdo::EntryType::BoolInput};
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

    struct SimChannelDef {
        std::string                        uuid;
        dynamichardware::dhdo::EntryType   type{dynamichardware::dhdo::EntryType::BoolInput};
        bool                               isOutput{false};
        uint32_t                           togglePeriodMs{100};
        float                              dutyCyclePercent{50.0f};
        int32_t                            incrementPerCycle{1};
        int64_t                            minValue{INT64_MIN};
        int64_t                            maxValue{INT64_MAX};
        float                              amplitude{1.0f};
        double                             frequencyHz{1.0};
        float                              offset{0.0f};
        uint32_t                           debounceMs{0};
        uint32_t                           pulseMs{0};
    };

    std::string                    definitionsPath_;
    uint32_t                       cycleNs_{500'000};
    double                         cycleNsD_{500'000.0};
    std::vector<SimState>          simStates_;

    /// Parse JSON definitions and extract cycle time + channel entries into outChannels.
    bool loadDefinitions(std::vector<SimChannelDef>& outChannels) noexcept;
};

} // namespace dynamichardware::simulated
