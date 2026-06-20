#include "HardwareDemoRoutine.h"

#include <cstdint>
#include <cstdio>

namespace civ_control {

HardwareDemoRoutine::HardwareDemoRoutine(pdomodel::HardwareRegistry& registry) noexcept
    : registry_(registry)
{
}

void HardwareDemoRoutine::tick(uint64_t cycleCount, uint64_t nowNs) noexcept
{
    // Lazy-initialise walkStartNs_ on the very first tick.
    if (walkStartNs_ == 0) {
        walkStartNs_ = nowNs;
    }

    // ------------------------------------------------------------------
    // 1. Walk pattern: 7-step sequential digital output activation.
    //    Each step lasts kWalkStepNs (2 s).  Outputs arm a PulseMachine
    //    which auto-resets after the configured pulse duration.
    // ------------------------------------------------------------------
    const int newStep = static_cast<int>(
        ((nowNs - walkStartNs_) / kWalkStepNs) % static_cast<uint64_t>(kWalkSteps));

    if (newStep != walkStep_) {
        walkStep_ = newStep;
        registry_.setDigitalOutput(kIdWalkAFirst + static_cast<int64_t>(walkStep_), true);
        registry_.setDigitalOutput(kIdWalkBFirst + static_cast<int64_t>(walkStep_), true);
    }

    // ------------------------------------------------------------------
    // 2. Flip pattern: toggle two outputs every kFlipPeriod cycles.
    // ------------------------------------------------------------------
    if (cycleCount > 0 && (cycleCount % kFlipPeriod) == 0) {
        registry_.setDigitalOutput(kIdFlipA, true);
        registry_.setDigitalOutput(kIdFlipB, true);
    }

    // ------------------------------------------------------------------
    // 3. Pulse re-arm: re-arm simulated pulse outputs every kPulseRepeat
    //    cycles (~20 s) to verify PulseMachine timed auto-reset.
    // ------------------------------------------------------------------
    if (cycleCount > 0 && (cycleCount % kPulseRepeat) == 0) {
        registry_.setDigitalOutput(kIdSimDoA, true);
        registry_.setDigitalOutput(kIdSimDoB, true);
    }

    // ------------------------------------------------------------------
    // 4. ExampleFunction pattern: position-gated sensor trigger.
    //    Rising edge on kIdSensor arms kIdAction output if the encoder
    //    has advanced at least kExFnThreshold counts since the last
    //    trigger — prevents re-arming at the same physical position.
    // ------------------------------------------------------------------
    {
        const bool    sensorHigh = registry_.getDigitalInput(kIdSensor);
        const int64_t encoderPos = registry_.getEncoderCount(kIdEncoder);
        const bool    risingEdge = sensorHigh && !exLastSensor_;

        if (risingEdge) {
            const int64_t advance = encoderPos - exLastTriggerCount_;
            if (advance >= kExFnThreshold) {
                registry_.setDigitalOutput(kIdAction, true);
                exLastTriggerCount_ = encoderPos;
                ++exTriggerCount_;
                std::printf("[DemoRoutine] ExFn trigger #%lld at encoder=%lld (advance=%lld)\n",
                            static_cast<long long>(exTriggerCount_),
                            static_cast<long long>(encoderPos),
                            static_cast<long long>(advance));
            } else {
                std::printf("[DemoRoutine] ExFn rising edge suppressed — advance=%lld < threshold=%lld\n",
                            static_cast<long long>(advance),
                            static_cast<long long>(kExFnThreshold));
            }
        }
        exLastSensor_ = sensorHigh;
    }
}

} // namespace civ_control
