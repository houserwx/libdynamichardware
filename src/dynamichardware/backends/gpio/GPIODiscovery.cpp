#include "dynamichardware/backends/gpio/GPIODiscovery.h"
#include "dynamichardware/pdo/HardwareCatalog.h"

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
// Pure discovery: no PDOEntry objects or hardware handles survive this call.
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

    // Scan all lines into catalog.
    uint32_t total_lines = 0;
#if HAS_LIBGPIOD
    total_lines = static_cast<uint32_t>(gpiod_chip_num_lines(static_cast<struct gpiod_chip*>(chipHandle_)));
#endif

    std::printf("[GPIO] Scanning %u GPIO lines on %s\n", total_lines, chipPath_.c_str());

    uint32_t claimed   = 0;
    uint32_t available = 0;

    for (uint32_t i = 0; i < total_lines; ++i) {
#if HAS_LIBGPIOD
        auto* c = static_cast<struct gpiod_chip*>(chipHandle_);
        struct gpiod_line* probe = gpiod_chip_get_line(c, i);
        if (probe) {
            const char* consumer = gpiod_line_consumer(probe);
            if (consumer && consumer[0] != '\0') {
                claimed++;
                std::printf("[GPIO]   Skipping GPIO%u (claimed by %s)\n", i, consumer);
                continue;
            }
        }
#endif

        // Register in catalog as available bidirectional pin.
        if (catalog_) {
            dynamichardware::pdo::CatalogEntry catEntry{};
            catEntry.key         = "GPIO|00|" + std::to_string(i);
            catEntry.uuid        = "GPIO|" + std::to_string(i);
            catEntry.channelType = "DigitalIO";
            catEntry.slaveName   = variant_ == BoardVariant::RASPBERRY_PI_5 ? "BCM2712" :
                                   variant_ == BoardVariant::RASPBERRY_PI_4 ? "BCM2711" : "GPIO";
            catEntry.slavePos    = 0;
            catEntry.isOutput    = false;
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
