#pragma once
#include "backends/pdo/IHardwareAdapter.h"
#include "dynamichardware/config/Config.h"
#include <vector>
#include <cstdint>

namespace fc::simulated {

// ============================================================
// SimulatedAdapter — generates synthetic I/O values each cycle.
//
// onBeforeReadInputs() writes synthetic encoder counts and digital
// input toggle bits into pdos_[0].image before the registry's read
// sweep.  Concrete PDOEntry::read() then extracts typed values.
// ============================================================

class SimulatedAdapter final : public fc::pdo::IHardwareAdapter {
public:
    explicit SimulatedAdapter(const common::config::Config& cfg) : cfg_(cfg) {}

    bool initialize() override;
    void onBeforeReadInputs() noexcept override;

private:
    const common::config::Config& cfg_;

    struct SimState {
        fc::pdo::EntryType type{fc::pdo::EntryType::DigitalInput};
        uint32_t  byteOffset{0};

        // Encoder
        int64_t   count{0};
        int64_t   inc{10};
        int64_t   incScaled{0};
        int64_t   accumulator{0};

        // DigitalInput
        int       toggleEvery{20};
        int       cycleTick{0};
        bool      rawBit{false};
        int32_t   halfHighTicks{0};
        int32_t   nominalLowTicks{0};
        int32_t   halfLowTicks{0};
        int32_t   phaseTick{0};
        bool      inHighPhase{false};
        float     varianceFraction{0.0f};
        uint64_t  varianceSeed{1};

        // AnalogInput
        int16_t   staticAdc{0};
    };

    std::vector<SimState> simStates_;
    double cycleNs_{500'000.0};
};

} // namespace fc::simulated
