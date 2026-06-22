#include "dynamichardware/backends/gpio/GPIODiscovery.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"
#include "dynamichardware/dhdo/HardwareDescriptor.h"

#include "dynamichardware/backends/registration.h"
#include "dynamichardware/backends/gpio/GPIORTBackend.h"

#include <memory>
#include <variant>
#include <vector>

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
// IBackenScanner::scan() — pure data scan: open chip -> probe lines -> return descriptors.
// Does NOT mutate catalog_. All hardware probing logic lives here.
// ---------------------------------------------------------------------------
std::vector<dhdo::HardwareDescriptor> GPIODiscovery::scan()
{
    std::vector<dhdo::HardwareDescriptor> results;

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
        return results;
    }

#if HAS_LIBGPIOD
    auto* chip = gpiod_chip_open(chipPath_.c_str());
    if (!chip) {
        std::fprintf(stderr, "[GPIO] Cannot open %s — skipping\n", chipPath_.c_str());
        return results;
    }
    chipHandle_ = chip;
#else
    std::fprintf(stderr, "[GPIO] libgpiod not available — stub mode disabled for discovery\n");
    return results;
#endif

    uint32_t total_lines = 0;
#if HAS_LIBGPIOD
    total_lines = static_cast<uint32_t>(gpiod_chip_num_lines(static_cast<struct gpiod_chip*>(chipHandle_)));
#endif

    // Safe pin whitelist: only user-accessible pins on the board header.
    constexpr uint32_t pi4MaxSafe = 28;
    constexpr uint32_t pi5MaxSafe = 46;
    if (variant_ == BoardVariant::RASPBERRY_PI_4 && total_lines > pi4MaxSafe) {
        total_lines = pi4MaxSafe;
    } else if (variant_ == BoardVariant::RASPBERRY_PI_5 && total_lines > pi5MaxSafe) {
        total_lines = pi5MaxSafe;
    }

    std::printf("[GPIO] Scanning %u GPIO lines on %s\n", total_lines, chipPath_.c_str());

    // Known-safe user-accessible pins on BCM2711 header.
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

        // Build descriptor for this available line.
        dhdo::HardwareDescriptor desc{};
        const std::string chipModel = variant_ == BoardVariant::RASPBERRY_PI_5 ? "BCM2712" :
                                      variant_ == BoardVariant::RASPBERRY_PI_4 ? "BCM2711" : "GPIO";
        desc.channelType = "DigitalIO";
        desc.name        = chipModel + " GPIO " + std::to_string(i);
        desc.isOutput    = false;   ///< Bidirectional at discovery time
        desc.backend     = dhdo::BackendType::GPIO;
        desc.backendData = dhdo::GpioBackendData{
            0,                        /* chipIndex */
            i,                        /* lineOffset — used by factory and demos */
            chipModel                 /* chipModel */
        };

        results.push_back(std::move(desc));
        ++available;
    }

    availableLineCount_ = available;
    std::printf("[GPIO] Discovered %u available lines (%u skipped by kernel drivers)\n",
                available, claimed);

    // Release chip — scan is a one-shot probe.
    reset();
    return results;
}

// ---------------------------------------------------------------------------
// discover() — thin wrapper: calls scan(), feeds results into catalog_.
// Pure discovery: no DHDOEntry objects or hardware handles survive this call.
// ---------------------------------------------------------------------------
bool GPIODiscovery::discover()
{
    auto descriptors = scan();

    if (!catalog_) {
        std::fprintf(stderr, "[GPIO-Discovery] No catalog attached\n");
        return false;
    }

    for (auto& desc : descriptors) {
        dhdo::CatalogEntry entry{};
        entry.channelType = desc.channelType;
        entry.slaveName   = "";  // GPIO has no slave concept; populated from name/backendData instead
        entry.name        = desc.name;
        entry.isOutput    = desc.isOutput;
        entry.backend     = desc.backend;
        entry.backendData = std::move(desc.backendData);

        catalog_->addEntry(std::move(entry));
    }

    return !descriptors.empty();
}

// ---------------------------------------------------------------------------
// Self-registration with BackendRegistry — zero-boilerplate OCP compliance.
// ---------------------------------------------------------------------------
REGISTER_BACKEND("GPIO", []() {
    return std::make_pair(
        std::make_unique<gpio::GPIODiscovery>(),
        std::make_unique<gpio::GPIORTBackend>()
    );
});

} // namespace dynamichardware::gpio
