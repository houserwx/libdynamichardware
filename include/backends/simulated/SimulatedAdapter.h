#pragma once
#include "backends/pdo/IHardwareAdapter.h"
#include "backends/ethercat/HardwareCatalog.h"
#include <vector>
#include <string>
#include <cstdint>

namespace fc::simulated {

// ============================================================
// SimulatedAdapter — virtual hardware adapter.
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
// Usage:
//   1. adapter.setCatalog(&catalog)
//   2. adapter.loadDefinitions("SimulatedAdapterDefinitions.json")
//   3. adapter.initialize() — registers channels, builds PDO
//   4. RT: onBeforeReadInputs() writes synthetic values
// ============================================================

class SimulatedAdapter final : public fc::pdo::IHardwareAdapter {
public:
    SimulatedAdapter() = default;
    ~SimulatedAdapter() override = default;

    bool initialize() override;
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

    void setCatalog(fc::pdo::HardwareCatalog* catalog) noexcept { catalog_ = catalog; }

    /// Load simulated channel definitions from a JSON file.
    /// Registers entries into the attached HardwareCatalog.
    bool loadDefinitions(const std::string& path);

    /// Set cycle time directly (alternative to loading from JSON).
    void setCycleTimeUs(int us) noexcept { cycleNs_ = static_cast<uint32_t>(us) * 1000u; }

    [[nodiscard]] std::size_t channelCount() const noexcept { return simStates_.size(); }

private:
    fc::pdo::HardwareCatalog* catalog_{nullptr};
    uint32_t                  cycleNs_{500'000};
    double                    cycleNsD_{500'000.0};

    struct SimState {
        fc::pdo::EntryType type{fc::pdo::EntryType::DigitalInput};
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

} // namespace fc::simulated
