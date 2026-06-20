#pragma once

#include "HardwareRegistry.h"
#include <cstdint>

// ============================================================================
// HardwareDemoRoutine — hardware smoke-test patterns for development.
//
// Purpose:
//   Exercises PulseMachine, debounce, digital outputs, and sequential
//   channel activation without requiring any product/conveyor logic.
//   Run during development to verify that the hardware layer, EtherCAT
//   adapter, and simulated adapter are behaving correctly.
//
// This class has NO place in a production deployment — it runs in place
// of real application logic only while FunctionEvaluator is not yet wired in.
//
// Patterns:
//   1. Walk: 7-step sequential digital output activation (2 s per step).
//      Activates channels kWalkAFirst+step and kWalkBFirst+step.
//   2. Flip: toggles two outputs every kFlipPeriod RT cycles.
//   3. Pulse re-arm: re-arms two PulseMachine outputs every kPulseRepeat
//      cycles (~20 s) to verify timed auto-reset.
//
// RT safety:
//   tick() is noexcept, allocation-free, and makes only registry setter
//   calls (single lower_bound per call, cached write, zero virtual dispatch).
// ============================================================================

namespace civ_control {

class HardwareDemoRoutine final {
public:
    /// @param registry   Registry frozen and ready for RT use.
    explicit HardwareDemoRoutine(pdomodel::HardwareRegistry& registry) noexcept;

    /// Called once per RT cycle, between readAll() and writeAll().
    void tick(uint64_t cycleCount, uint64_t nowNs) noexcept;

    /// Total position-gated triggers fired by the ExampleFunction pattern.
    [[nodiscard]] int64_t triggerCount() const noexcept { return exTriggerCount_; }

private:
    // Channel IDs — match SimulatedAdapter defaults and EL2409 slave layout.
    static constexpr int64_t  kIdWalkAFirst =  18;   // EL2409 ch1-7
    static constexpr int64_t  kIdFlipA      =  25;   // EL2409 ch8
    static constexpr int64_t  kIdWalkBFirst =  26;   // EL2409 ch9-15
    static constexpr int64_t  kIdFlipB      =  33;   // EL2409 ch16
    static constexpr int64_t  kIdSimDoA     =  50;   // Sim DO A (pulse 750 ms)
    static constexpr int64_t  kIdSimDoB     =  51;   // Sim DO B (pulse 2000 ms)

    // ExampleFunction pattern IDs
    static constexpr int64_t  kIdSensor     =  40;   // Sim DI A — rising-edge input
    static constexpr int64_t  kIdEncoder    =   1;   // Simulated encoder
    static constexpr int64_t  kIdAction     =  50;   // Sim DO A — position-gated output
    static constexpr int64_t  kExFnThreshold = 500;  // min encoder counts between triggers

    static constexpr int      kWalkSteps    =   7;
    static constexpr uint64_t kWalkStepNs   = 2'000'000'000ULL; // 2 s per step
    static constexpr uint64_t kFlipPeriod   =    10'000ULL;     // cycles
    static constexpr uint64_t kPulseRepeat  =    40'000ULL;     // cycles (~20 s @ 500 µs)

    pdomodel::HardwareRegistry& registry_;

    // Walk state
    int      walkStep_   {0};
    uint64_t walkStartNs_{0};

    // ExampleFunction state
    bool    exLastSensor_       {false};
    int64_t exLastTriggerCount_ {0};
    int64_t exTriggerCount_     {0};
};

} // namespace civ_control
