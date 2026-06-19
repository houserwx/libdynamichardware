#include "dynamichardware/backends/gpio/GPIOAdapter.h"
#include "dynamichardware/backends/gpio/BoardVariant.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Try to include libgpiod — fall back gracefully if unavailable
// libgpiod 1.0+ uses the v2 API (gpiod_chip_open, gpiod_line_get_value, etc.)
// This is the standard API since 2019, shipped on both Pi 4 and Pi 5.
#ifdef GPIO_LIBGPIOD_AVAILABLE
#include <gpiod.h>
#define HAS_LIBGPIOD 2
#else
#define HAS_LIBGPIOD 0
#endif

namespace dynamichardware::gpio {

// ---------------------------------------------------------------------------
// Constructor — auto-detect board variant
// ---------------------------------------------------------------------------
GPIOAdapter::GPIOAdapter()
{
    variant_ = detectBoardVariant();
    chipPath_ = gpioChipPath(variant_);
}

GPIOAdapter::GPIOAdapter(BoardVariant variant, std::string chipPath)
    : variant_(variant), chipPath_(std::move(chipPath))
{
}

// ---------------------------------------------------------------------------
// Initialize — open chip, discover lines, create PDOs, populate catalog
// ---------------------------------------------------------------------------
bool GPIOAdapter::initialize()
{
    // Detect board variant if still unknown
    if (variant_ == BoardVariant::UNKNOWN) {
        variant_ = detectBoardVariant();
        if (variant_ != BoardVariant::UNKNOWN) {
            chipPath_ = gpioChipPath(variant_);
        }
    }

  #if HAS_LIBGPIOD
    // Try to open real GPIO chip
    if (!openChip()) {
        std::fprintf(stderr, "[GPIOAdapter] Cannot open %s — falling back to stub mode\n",
                     chipPath_.c_str());
        stubMode_ = true;
    }
#else
    std::fprintf(stderr, "[GPIOAdapter] libgpiod not available — stub mode\n");
    stubMode_ = true;
#endif

    // Discover GPIO lines — auto-populate all available lines in the catalog
    discoverLines();

    if (stubMode_) {
        // Initialize stub states for discovered lines
        stubStates_.resize(lines_.size());
        for (size_t i = 0; i < lines_.size(); ++i) {
            stubStates_[i].value = lines_[i].initialVal;
            stubStates_[i].toggleCycle = 0;
        }
    } else {
        // Request each discovered line from the GPIO chip
        for (size_t i = 0; i < lines_.size(); ++i) {
            if (!requestLine(lines_[i], i)) {
                std::fprintf(stderr, "[GPIOAdapter] Cannot request line %u (%s) — will skip\n",
                             lines_[i].offset, lines_[i].name.c_str());
            }
        }
    }

    // Create PDOs for GPIO lines (single PDO with all lines)
    {
        dynamichardware::pdo::PDO pdo;
        for (auto& line : lines_) {
            if (line.entry) {
                pdo.entries.push_back(*line.entry);
            }
        }

        if (!pdo.entries.empty()) {
            // Allocate image buffer (1 byte per entry for digital I/O)
            pdo.image.resize(pdo.entries.size());

            // Set image pointers into each entry
            for (size_t i = 0; i < pdo.entries.size(); ++i) {
                pdo.entries[i].image = pdo.image.data() + i;
            }

            pdos_.push_back(std::move(pdo));
        }
    }

    if (variant_ != BoardVariant::UNKNOWN) {
        std::printf("[GPIOAdapter] %s — %zu lines (%s)\n",
                    boardVariantName(variant_).c_str(),
                    lines_.size(),
                    stubMode_ ? "stub mode" : "libgpiod");
    } else {
        std::printf("[GPIOAdapter] Unknown board — %zu lines (%s)\n",
                    lines_.size(),
                    stubMode_ ? "stub mode" : "libgpiod");
    }

    return true;
}

// ---------------------------------------------------------------------------
// discoverLines() — scan GPIO chip, skip kernel-claimed lines, populate catalog
// ---------------------------------------------------------------------------
void GPIOAdapter::discoverLines()
{
    uint32_t total_lines = gpioLineCount(variant_);

#if HAS_LIBGPIOD
    if (!stubMode_ && chipHandle_) {
        auto* chip = static_cast<struct gpiod_chip*>(chipHandle_);
        total_lines = static_cast<uint32_t>(gpiod_chip_num_lines(chip));
    }
#endif

    std::printf("[GPIOAdapter] Scanning %u GPIO lines on %s\n",
                total_lines, chipPath_.c_str());

    // Reserve handles upfront (worst case: all lines are free)
    handles_.resize(total_lines);

    uint32_t claimed = 0;
    uint32_t registered = 0;

    // Discover each line — skip kernel-claimed lines (I2C, SPI, etc.)
    for (uint32_t i = 0; i < total_lines; ++i) {
#if HAS_LIBGPIOD
        if (!stubMode_ && chipHandle_) {
            // Check if this line is already claimed by a kernel consumer
            auto* chip = static_cast<struct gpiod_chip*>(chipHandle_);
            struct gpiod_line* probe = gpiod_chip_get_line(chip, i);
            if (probe) {
                const char* consumer = gpiod_line_consumer(probe);
                if (consumer && consumer[0] != '\0') {
                    // Line is in use by kernel driver — skip it
                    claimed++;
                    std::printf("[GPIOAdapter]   Skipping GPIO%u (claimed by %s)\n",
                                i, consumer);
                    continue;
                }
            }
        }
#endif

        GPIOLine line;
        line.offset = i;
        line.direction = LineDirection::INPUT;

        // Create human-readable name
        char name_buf[64];
        std::snprintf(name_buf, sizeof(name_buf), "GPIO%u", i);
        line.name = name_buf;

        // Create PDO entry for this line
        dynamichardware::pdo::PDOEntry entry;
        entry.type = dynamichardware::pdo::EntryType::BoolInput;
        entry.uuid = "GPIO|" + std::to_string(i);
        line.entry = &entry;

        lines_.push_back(std::move(line));
        registered++;

        // Register in hardware catalog
        if (catalog_) {
            dynamichardware::pdo::CatalogEntry catEntry;
            catEntry.key = "GPIO|00|" + std::to_string(i);
            catEntry.uuid = entry.uuid;
            catEntry.channelType = "DigitalInput";
            catEntry.name = "GPIO" + std::to_string(i);
            catEntry.slaveName = variant_ == BoardVariant::RASPBERRY_PI_5 ? "BCM2712" :
                                 variant_ == BoardVariant::RASPBERRY_PI_4 ? "BCM2711" : "GPIO";
            catEntry.slavePos = 0;
            catEntry.isOutput = false;
            catalog_->addEntry(std::move(catEntry));
        }
    }

    std::printf("[GPIOAdapter] Registered %zu available lines (%u skipped by kernel drivers)\n",
                registered, claimed);
}

// ---------------------------------------------------------------------------
// RT cycle: read input lines
// ---------------------------------------------------------------------------
void GPIOAdapter::onBeforeReadInputs() noexcept
{
    if (stubMode_) {
        // Stub: toggle inputs at a simulated rate for testing
        static uint64_t cycleCount = 0;
        ++cycleCount;

        for (size_t i = 0; i < lines_.size(); ++i) {
            if (lines_[i].direction == LineDirection::INPUT) {
                // Simple toggle every 20 cycles for simulation
                bool val = (cycleCount % 40) < 20;
                stubStates_[i].value = val;

                if (lines_[i].entry && lines_[i].entry->image) {
                    *(uint8_t*)lines_[i].entry->image = val ? 1 : 0;
                }
            }
        }
        return;
    }

    // Real hardware: read GPIO input lines
#if HAS_LIBGPIOD
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (lines_[i].direction != LineDirection::INPUT) continue;
        if (!handles_[i].gpiod_line) continue;

        int val = gpiod_line_get_value(static_cast<struct gpiod_line*>(handles_[i].gpiod_line));

        if (lines_[i].entry && lines_[i].entry->image) {
            *(uint8_t*)lines_[i].entry->image = static_cast<uint8_t>(val != 0);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// RT cycle: write output lines
// ---------------------------------------------------------------------------
void GPIOAdapter::onAfterWriteOutputs() noexcept
{
    if (stubMode_) {
        for (size_t i = 0; i < lines_.size(); ++i) {
            if (lines_[i].direction != LineDirection::OUTPUT) continue;

            if (lines_[i].entry && lines_[i].entry->image) {
                stubStates_[i].value = (*(uint8_t*)lines_[i].entry->image) != 0;
            }
        }
        return;
    }

    // Real hardware: write GPIO output lines
#if HAS_LIBGPIOD
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (lines_[i].direction != LineDirection::OUTPUT) continue;
        if (!handles_[i].gpiod_line) continue;

        int val = 0;
        if (lines_[i].entry && lines_[i].entry->image) {
            val = (*(uint8_t*)lines_[i].entry->image) != 0;
        }

        gpiod_line_set_value(static_cast<struct gpiod_line*>(handles_[i].gpiod_line), val);
    }
#endif
}

// ---------------------------------------------------------------------------
// Register a GPIO line
// ---------------------------------------------------------------------------
int GPIOAdapter::registerLine(uint32_t gpio_offset,
                              LineDirection direction,
                              std::string name,
                              dynamichardware::pdo::EntryType entryType)
{
    GPIOLine line;
    line.offset = gpio_offset;
    line.direction = direction;
    line.name = std::move(name);

    // Create PDO entry for this line
    dynamichardware::pdo::PDOEntry entry;
    entry.type = entryType;
    entry.uuid = "GPIO|" + std::to_string(gpio_offset);
    line.entry = &entry;

    const int idx = static_cast<int>(lines_.size());
    lines_.push_back(std::move(line));

    // Pre-allocate handle stubs
    handles_.resize(lines_.size());
    return idx;
}

// ---------------------------------------------------------------------------
// Open GPIO chip
// ---------------------------------------------------------------------------
bool GPIOAdapter::openChip() noexcept
{
#if HAS_LIBGPIOD
    struct gpiod_chip* chip = gpiod_chip_open(chipPath_.c_str());
    if (!chip) return false;

    chipHandle_ = chip;
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Request a single GPIO line
// ---------------------------------------------------------------------------
bool GPIOAdapter::requestLine(GPIOLine& line, size_t index) noexcept
{
#if HAS_LIBGPIOD
    auto* chip = static_cast<struct gpiod_chip*>(chipHandle_);
    if (!chip) return false;

    const char* consumer = "EtherCatDrone";

    // Get the line handle
    struct gpiod_line* l = gpiod_chip_get_line(chip, line.offset);
    if (!l) return false;

    // Use simple request functions (libgpiod 1.0+ API)
    if (line.direction == LineDirection::OUTPUT) {
        int init_val = line.initialVal ? 1 : 0;
        if (gpiod_line_request_output(l, consumer, init_val) != 0) {
            return false;
        }
    } else {
        if (gpiod_line_request_input(l, consumer) != 0) {
            return false;
        }
    }

    handles_[index].gpiod_line = l;
    return true;
#else
    (void)line;
    (void)index;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Close GPIO chip
// ---------------------------------------------------------------------------
void GPIOAdapter::closeChip() noexcept
{
#if HAS_LIBGPIOD
    if (chipHandle_) {
        gpiod_chip_close(static_cast<struct gpiod_chip*>(chipHandle_));
        chipHandle_ = nullptr;
    }
#endif
}

} // namespace dynamichardware::gpio
