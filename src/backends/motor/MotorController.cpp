#include "backends/motor/MotorController.h"

#include <algorithm>

namespace fc::motor {

MotorController::MotorController(int motorIndex, float throttleMin, float throttleMax,
                                 int16_t rpmScale, int16_t rpmOffset)
    : motorIndex_(motorIndex)
    , throttleMin_(throttleMin)
    , throttleMax_(throttleMax)
    , rpmScale_(rpmScale)
    , rpmOffset_(rpmOffset)
{
}

void MotorController::setThrottle(float t) noexcept
{
    throttle_ = std::clamp(t, 0.0f, 1.0f);
}

int16_t MotorController::getAdcValue() const noexcept
{
    if (!enabled_) return 0;
    // Map throttle [0.0, 1.0] → ADC range [throttleMin, throttleMax]
    float scaled = throttleMin_ + throttle_ * (throttleMax_ - throttleMin_);
    return static_cast<int16_t>(scaled);
}

} // namespace fc::motor
