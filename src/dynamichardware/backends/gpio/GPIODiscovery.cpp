#include "dynamichardware/backends/gpio/GPIODiscovery.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"

#include <variant>

#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace dynamichardware::gpio {

// ---------------------------------------------------------------------------
// Constructor — auto-detect board variant
// ---------------------------------------------------------------------------
GPIODiscovery::GPIODiscovery()
{
    variant_ = detectBoardVariant();
    chipPath_ = gpioChipPath(variant_);
}

GPIODiscovery::GPIODiscovery(BoardVariant variant, std::string chipPath)
    : variant_(variant), chipPath_(std::move(chipPath)) {}

GPIODiscovery::~GPIODiscovery() { reset(); }

void GPIODiscovery::reset() noexcept
{
#if HAS_LIBGPIOD
    if (chipHandle_) {
        gpiod_chip_close(static_cast<struct gpiod_chip*>(chipHandle_));
        chipHandle_ = nullptr;
    }
#endif
    availableLineCount_ = 0;
}

// ---------------------------------------------------------------------------
// discover() — open chip → scan lines → populate catalog → release chip.
// Pure discovery: no DHDOEntry objects or hardware handles survive this call.
// ---------------------------------------------------------------------------
bool GPIODiscovery::discover()
{
    // Detect board variant if still unknown.
    if (variant_ == BoardVariant::UNKNOWN) {
        variant_ = detectBoardVariant();
        if (variant_ != BoardVariant::UNKNOWN) {
            chipPath_ = gpioChipPath(variant_);
        }
    }

    if (variant_ == BoardVariant::UNKNOWN) {
        std::fprintf(stderr,
                     "[GPIO] Not a recognized embedded platform — skipping GPIO backend\n");
        return false;
    }

#if HAS_LIBGPIOD
    auto* chip = gpiod_chip_open(chipPath_.c_str());
    if (!chip) {
        std::fprintf(stderr, "[GPIO] Cannot open %s — skipping\n", chipPath_.c_str());
        return false;
    }
    chipHandle_ = chip;
#else
    std::fprintf(stderr, "[GPIO] libgpiod not available — stub mode disabled for discovery\n");
    return false;
#endif

    uint32_t total_lines = 0;
#if HAS_LIBGPIOD
    total_lines = static_cast<uint32_t>(gpiod_chip_num_lines(static_cast<struct gpiod_chip*>(chipHandle_)));
#endif

    // Safe pin whitelist: only user-accessible pins on the board header.
    // Internal/reserved pins (SD card data lanes, PWM alt-funcs, etc.) can crash the
    // kernel when libgpiod tries to reconfigure pinctrl. Never touch these.
    // BCM2711 (Pi 4): GPIO 0-27 are generally safe; 28+ are internal/SD-card/etc.
    constexpr uint32_t pi4MaxSafe = 28;
    constexpr uint32_t pi5MaxSafe = 46;
    if (variant_ == BoardVariant::RASPBERRY_PI_4 && total_lines > pi4MaxSafe) {
        total_lines = pi4MaxSafe;
    } else if (variant_ == BoardVariant::RASPBERRY_PI_5 && total_lines > pi5MaxSafe) {
        total_lines = pi5MaxSafe;
    }

    std::printf("[GPIO] Scanning %u GPIO lines on %s\n", total_lines, chipPath_.c_str());

    // Known-safe user-accessible pins on BCM2711 header (all others risk pinctrl conflicts):
    // GPIO 0-5, GPIO 12-13 (SPI CE), GPIO 16-23 (GPIO bank), GPIO 24-25 (SPI data/clock)
    // Pins to NEVER touch: 6-7 (I2C used by kernel), 8-9 (UART), 10-11 (SPI—pinctrl reserved),
    //                      26-27 (SD card alt-func on some revisions)
    static const uint32_t pi4SafePins[] = {
        0, 1, 2, 3, 4, 5,       // General purpose bank
        12, 13,                  // SPI CE lines
        16, 17, 18, 19, 20, 21, // Extended GPIO bank
        22, 23                   // SPI MOSI/MISO
    };
    constexpr size_t pi4SafePinCount = sizeof(pi4SafePins) / sizeof(pi4SafePins[0]);

    uint32_t claimed   = 0;
    uint32_t available = 0;

    for (uint32_t i = 0; i < total_lines; ++i) {
#if HAS_LIBGPIOD
        auto* c = static_cast<struct gpiod_chip*>(chipHandle_);
        struct gpiod_line* probe = gpiod_chip_get_line(c, i);
        if (!probe) continue;

        // is_used() catches named consumers and some alternate functions.
        if (gpiod_line_is_used(probe)) {
            const char* consumer = gpiod_line_consumer(probe);
            if (consumer && consumer[0] != '\0') {
                std::printf("[GPIO]   Skipping GPIO%u (claimed by %s)\n", i, consumer);
            } else {
                std::printf("[GPIO]   Skipping GPIO%u (alternate function / reserved)\n", i);
            }
            claimed++;
            continue;
        }

#endif

        // On BCM2711, even "unclaimed" lines might have kernel-assigned alternate functions.
        // Use explicit whitelist of known-safe user-accessible header pins to avoid crashes.
        if (variant_ == BoardVariant::RASPBERRY_PI_4) {
            bool isSafePin = false;
            for (size_t s = 0; s < pi4SafePinCount; ++s) {
                if (pi4SafePins[s] == i) { isSafePin = true; break; }
            }
            if (!isSafePin) {
                std::printf("[GPIO]   Skipping GPIO%u (not in safe whitelist for BCM2711)\n", i);
                claimed++;
                continue;
            }
        }

        // Register in catalog as available bidirectional pin.
        // Populates structured backendData so consumers use getDetails() instead of
        // parsing raw key strings.  GpioBackendData.lineOffset holds the GPIO line number.
        if (catalog_) {
           dynamichardware::dhdo::CatalogEntry catEntry{};
            const std::string chipModel = variant_ == BoardVariant::RASPBERRY_PI_5 ? "BCM2712" :
                                          variant_ == BoardVariant::RASPBERRY_PI_4 ? "BCM2711" : "GPIO";
            // UUID will be auto-generated from GpioBackendData in addEntry().
            catEntry.channelType = "DigitalIO";
            catEntry.slaveName   = chipModel;
            catEntry.name        = chipModel + " GPIO " + std::to_string(i);
            catEntry.isOutput    = false;   ///< Bidirectional at discovery time
            catEntry.backend     = dynamichardware::dhdo::BackendType::GPIO;
            catEntry.backendData = dynamichardware::dhdo::GpioBackendData{
                0,                        /* chipIndex */
                i,                        /* lineOffset — used by factory and demos */
                chipModel                 /* chipModel */
            };
            catalog_->addEntry(std::move(catEntry));
        }

        ++available;
    }

    availableLineCount_ = available;
    std::printf("[GPIO] Discovered %u available lines (%u skipped by kernel drivers)\n",
                available, claimed);

    // Release chip — discovery is done.
    reset();
    return true;
}

} // namespace dynamichardware::gpio
