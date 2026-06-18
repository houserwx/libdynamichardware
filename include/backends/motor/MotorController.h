#pragma once
#include <cstdint>

namespace fc::motor {

// ---------------------------------------------------------------------------
// MotorController — one ESC/motor channel.
// Wraps AnalogOutput (speed ref), DigitalOutput (enable), Encoder (RPM feedback).
//
// RT-safe: all methods are noexcept, no allocation after construction.
// ---------------------------------------------------------------------------
class MotorController {
public:
    MotorController(int motorIndex, float throttleMin, float throttleMax,
                    int16_t rpmScale, int16_t rpmOffset);

    /// Set throttle (0.0–1.0).
    void setThrottle(float t) noexcept;

    /// Enable/disable motor.
    void enable()  noexcept { enabled_ = true;  }
    void disable() noexcept { enabled_ = false; throttle_ = 0.0f; }

    /// Get current RPM (from encoder feedback).
    [[nodiscard]] float getRpm() const noexcept { return rpm_; }

    /// Get current throttle command.
    [[nodiscard]] float getThrottle() const noexcept { return throttle_; }

    /// Is motor enabled?
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    /// Safe state: disable and zero throttle.
    void safeState() noexcept { disable(); }

    /// Get ADC value for analog output (throttle → ADC mapping).
    [[nodiscard]] int16_t getAdcValue() const noexcept;

private:
    int     motorIndex_;
    float   throttleMin_;
    float   throttleMax_;
    int16_t rpmScale_;
    int16_t rpmOffset_;

    float   throttle_{0.0f};
    bool    enabled_{false};
    float   rpm_{0.0f};
};

} // namespace fc::motor
