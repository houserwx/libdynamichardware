#pragma once

// ============================================================================
// BoardVariant — Raspberry Pi board detection and GPIO controller abstraction.
//
// Pi 4 and Pi 5 use different GPIO controller architectures:
//   Pi 4: BCM2711 — single gpiochip0, 54 GPIO lines (0-53), libgpiod v1 API
//   Pi 5: BCM2712 — single gpiochip0, 54 GPIO lines (0-53), libgpiod v2 API
//          plus optional secondary gpiochip (I2C/SPI controller)
//
// Linux GPIO subsystem (gpiod) abstracts the hardware differences.
// This module detects which variant is running and configures the
// GPIO adapter accordingly.
//
// Pin mapping (GPIO vs board header pin):
//   Both boards share the same 40-pin header layout.
//   The GPIO adapter works with GPIO numbers (BCM pin numbers), not
//   physical header pins.  E.g., GPIO 17 = header pin 11.
// ============================================================================

#include <cstdint>
#include <string>

namespace fc::gpio {

// Board variant enum — set during detection
enum class BoardVariant : uint8_t {
    UNKNOWN = 0,
    RASPBERRY_PI_4,
    RASPBERRY_PI_5,
};

/// Detect the running Raspberry Pi variant by reading /proc/device-tree.
/// Returns UNKNOWN if not a Raspberry Pi or detection fails.
[[nodiscard]] BoardVariant detectBoardVariant() noexcept;

/// Human-readable name for a board variant.
[[nodiscard]] std::string boardVariantName(BoardVariant variant) noexcept;

/// GPIO chip path — both Pi 4 and Pi 5 use /dev/gpiochip0 for the
/// main SoC GPIO bank (BCM2711/BCM2712).
[[nodiscard]] std::string gpioChipPath(BoardVariant variant) noexcept;

/// Total number of GPIO lines on the variant (54 for both Pi 4/5).
[[nodiscard]] uint32_t gpioLineCount(BoardVariant variant) noexcept;

/// Check whether the GPIO chip path exists and is accessible.
[[nodiscard]] bool gpioChipAvailable(BoardVariant variant) noexcept;

} // namespace fc::gpio
