#include "dynamichardware/backends/gpio/BoardVariant.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <algorithm>

// Try to include libgpiod — fall back gracefully if unavailable
// Note: libgpiod 1.6+ uses the v2 API (gpiod_chip_open, gpiod_line_get_value, etc.)
// Older versions used the v1 API (gpiod_chip_open_by_path, gpiod_get_line_value, etc.)
// The v2 API is the standard since libgpiod 1.0 (2019) and is what Pi 4/5 ship.
#ifdef GPIO_LIBGPIOD_AVAILABLE
#include <gpiod.h>
#endif

namespace dynamichardware::gpio {

// ---------------------------------------------------------------------------
// Board detection
// ---------------------------------------------------------------------------
BoardVariant detectBoardVariant() noexcept
{
    // Method 1: Read /proc/device-tree/model (most reliable on Pi 4/5)
    {
        std::ifstream modelFile("/proc/device-tree/model");
        if (modelFile.is_open()) {
            std::string model;
            std::getline(modelFile, model);

            if (model.find("Raspberry Pi 4") != std::string::npos ||
                model.find("BCM2711") != std::string::npos) {
                return BoardVariant::RASPBERRY_PI_4;
            }
            if (model.find("Raspberry Pi 5") != std::string::npos ||
                model.find("BCM2712") != std::string::npos) {
                return BoardVariant::RASPBERRY_PI_5;
            }
        }
    }

    // Method 2: Read /proc/cpuinfo for hardware field
    {
        std::ifstream cpuinfo("/proc/cpuinfo");
        if (cpuinfo.is_open()) {
            std::string line;
            while (std::getline(cpuinfo, line)) {
                if (line.find("Hardware") == 0) {
                    if (line.find("BCM2711") != std::string::npos) {
                        return BoardVariant::RASPBERRY_PI_4;
                    }
                    if (line.find("BCM2712") != std::string::npos) {
                        return BoardVariant::RASPBERRY_PI_5;
                    }
                    break;
                }
            }
        }
    }

    // Method 3: Check /sys/firmware/devicetree/base/compatible
    {
        std::ifstream compatFile("/sys/firmware/devicetree/base/compatible");
        if (compatFile.is_open()) {
            std::string content((std::istreambuf_iterator<char>(compatFile)),
                                std::istreambuf_iterator<char>());

            if (content.find("brcm,bcm2711") != std::string::npos) {
                return BoardVariant::RASPBERRY_PI_4;
            }
            if (content.find("brcm,bcm2712") != std::string::npos) {
                return BoardVariant::RASPBERRY_PI_5;
            }
        }
    }

    return BoardVariant::UNKNOWN;
}

std::string boardVariantName(BoardVariant variant) noexcept
{
    switch (variant) {
        case BoardVariant::RASPBERRY_PI_4: return "Raspberry Pi 4 (BCM2711)";
        case BoardVariant::RASPBERRY_PI_5: return "Raspberry Pi 5 (BCM2712)";
        case BoardVariant::UNKNOWN:
        default:                           return "Unknown";
    }
}

std::string gpioChipPath(BoardVariant variant) noexcept
{
    // Both Pi 4 and Pi 5 use gpiochip0 for the main SoC GPIO bank
    (void)variant;
    return "/dev/gpiochip0";
}

uint32_t gpioLineCount(BoardVariant variant) noexcept
{
    switch (variant) {
        case BoardVariant::RASPBERRY_PI_4: return 54;
        case BoardVariant::RASPBERRY_PI_5: return 54;
        case BoardVariant::UNKNOWN:
        default:                           return 54; // Default to BCM2711 count
    }
}

bool gpioChipAvailable(BoardVariant variant) noexcept
{
#ifdef GPIO_LIBGPIOD_AVAILABLE
    const std::string chipPath = gpioChipPath(variant);

    // Try to open the GPIO chip (libgpiod v2 API — standard since 2019)
    struct gpiod_chip* chip = gpiod_chip_open(chipPath.c_str());
    if (chip) {
        gpiod_chip_close(chip);
        return true;
    }
#endif

    // Fallback: just check if the device file exists
    {
        const std::string chipPath = gpioChipPath(variant);
        FILE* f = fopen(chipPath.c_str(), "r");
        if (f) {
            fclose(f);
            return true;
        }
    }

    return false;
}

} // namespace dynamichardware::gpio
