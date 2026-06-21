#include "dynamichardware/backends/gpio/GPIORTBackend.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace dynamichardware::gpio {

// ---------------------------------------------------------------------------
// Constructor — auto-detect board variant
// ---------------------------------------------------------------------------
GPIORTBackend::GPIORTBackend()
{
    variant_ = detectBoardVariant();
    chipPath_ = gpioChipPath(variant_);
}

GPIORTBackend::GPIORTBackend(BoardVariant variant, std::string chipPath)
    : variant_(variant), chipPath_(std::move(chipPath)) {}

GPIORTBackend::~GPIORTBackend() noexcept { closeChip(); }

// ---------------------------------------------------------------------------
// buildRT() — open chip → request registered lines → build PDOs.
// Fully self-contained: does NOT depend on any prior discovery scan.
// ---------------------------------------------------------------------------
bool GPIORTBackend::buildRT()
{
    if (activated_) {
        std::fprintf(stderr, "[GPIO] Already built — skipping\n");
        return false;
    }

    if (lines_.empty()) {
        std::printf("[GPIO] No registered lines to build RT PDOs for\n");
        activated_ = true;
        return true;
    }

    // Detect board variant if still unknown.
    if (variant_ == BoardVariant::UNKNOWN) {
        variant_ = detectBoardVariant();
        if (variant_ != BoardVariant::UNKNOWN) {
            chipPath_ = gpioChipPath(variant_);
        }
    }

#if HAS_LIBGPIOD
    if (!openChip()) {
        std::fprintf(stderr, "[GPIO] Cannot open %s — falling back to stub mode\n",
                     chipPath_.c_str());
        stubMode_ = true;
    }
#else
    std::fprintf(stderr, "[GPIO] libgpiod not available — stub mode\n");
    stubMode_ = true;
#endif

    // Request hardware handles for each registered line.
    if (stubMode_) {
        stubStates_.resize(lines_.size());
        for (size_t i = 0; i < lines_.size(); ++i) {
            stubStates_[i].value       = lines_[i].initialVal;
            stubStates_[i].toggleCycle = 0;
        }
    } else {
        handles_.resize(lines_.size());
        for (size_t i = 0; i < lines_.size(); ++i) {
            if (!requestLine(lines_[i], i)) {
                std::fprintf(stderr, "[GPIO] Cannot request line %u (%s) — will skip\n",
                             lines_[i].offset, lines_[i].name.c_str());
            }
        }
    }

    // Build PDO from registered lines only.
    {   dynamichardware::dhdo::DHDO pdo;
        size_t byteOffset = 0;
        for (auto& line : lines_) {
            // Copy the value-owned entry into the PDO vector.
            auto e = line.entry;
            e.byteOffset = byteOffset++;     // Each GPIO line owns one byte in the process image
            pdo.entries.push_back(e);
        }

        if (!pdo.entries.empty()) {
            pdo.image.resize(byteOffset);    // One byte per line — freeze() will re-base .image pointers
            dhdos_.push_back(std::move(pdo));
        }
    }

    activated_ = true;
    std::printf("[GPIO] Built RT: %zu registered lines in process image\n", lines_.size());
    return true;
}

// ---------------------------------------------------------------------------
// syncImagePointers() — after PDO::freeze() sets .image on the PDO copies,
// propagate them back into the originals so the backend's read/write hooks
// can access valid image pointers from lines_[i].entry.image.
// Called by GPIORTBackend (no public API — internal housekeeping).
// ---------------------------------------------------------------------------
void GPIORTBackend::syncImagePointers()
{
    if (dhdos_.empty()) return;
    const auto& pdo = dhdos_[0];
    for (size_t i = 0; i < lines_.size() && i < pdo.entries.size(); ++i) {
        lines_[i].entry.image = pdo.entries[i].image;
    }
}

// ---------------------------------------------------------------------------
// Legacy compatibility wrapper — deferredActivate now delegates to buildRT().
void GPIORTBackend::deferredActivate()
{
    [[maybe_unused]] bool ok = buildRT();
    (void)ok;  // Suppress nodiscard warning — legacy API was fire-and-forget.
}

// ---------------------------------------------------------------------------
// registerLine() — THE CONSUMER SHOULD NEVER CALL THIS.  THIS IS SET UP INTERNALLY VIA THE this.Build();  The context should 
// just do foreach(backend in backends) backend.buildRT(ListOfMapped DHDOEntries for this backend);  Each backend is responsible for knowing its own build needs.  
// ---------------------------------------------------------------------------
int GPIORTBackend::registerLine(uint32_t gpio_offset, LineDirection direction,
                                 std::string name, dynamichardware::dhdo::EntryType entryType,
                                 const std::string& uuid)
{
    GPIOLine line{};
    line.offset     = gpio_offset;
    line.direction  = direction;
    line.name       = std::move(name);

    // Store DHDOEntry by value — no dangling pointer risk.
    line.entry.type      = entryType;
    // Use provided catalog UUID if available; fall back to legacy format for compat.
    line.entry.uuid      = uuid.empty()
        ? ("GPIO|00|" + std::to_string(gpio_offset))
        : uuid;

    const int idx = static_cast<int>(lines_.size());
    lines_.push_back(std::move(line));

    handles_.resize(lines_.size());
    return idx;
}

// ---------------------------------------------------------------------------
// RT cycle: read input lines into PDO image buffers.
// ---------------------------------------------------------------------------
void GPIORTBackend::onBeforeReadInputs() noexcept
{
    if (stubMode_) {
        static uint64_t cycleCount = 0;
        ++cycleCount;

        for (size_t i = 0; i < lines_.size(); ++i) {
            if (lines_[i].direction != LineDirection::INPUT) continue;

            bool val = (cycleCount % 40) < 20;
            stubStates_[i].value = val;

            if (lines_[i].entry.image) {
                *(uint8_t*)lines_[i].entry.image = val ? 1 : 0;
            }
        }
        return;
    }

#if HAS_LIBGPIOD
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (lines_[i].direction != LineDirection::INPUT) continue;
        if (!handles_[i].gpiod_line) continue;

        int val = gpiod_line_get_value(static_cast<struct gpiod_line*>(handles_[i].gpiod_line));

        if (lines_[i].entry.image) {
            *(uint8_t*)lines_[i].entry.image = static_cast<uint8_t>(val != 0);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// RT cycle: write output lines from PDO image buffers.
// ---------------------------------------------------------------------------
void GPIORTBackend::onAfterWriteOutputs() noexcept
{
    if (stubMode_) {
        for (size_t i = 0; i < lines_.size(); ++i) {
            if (lines_[i].direction != LineDirection::OUTPUT) continue;

            if (lines_[i].entry.image) {
                stubStates_[i].value = (*(uint8_t*)lines_[i].entry.image) != 0;
            }
        }
        return;
    }

#if HAS_LIBGPIOD
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (lines_[i].direction != LineDirection::OUTPUT) continue;
        if (!handles_[i].gpiod_line) continue;

        int val = 0;
        if (lines_[i].entry.image) {
            val = (*(uint8_t*)lines_[i].entry.image) != 0;
        }

        gpiod_line_set_value(static_cast<struct gpiod_line*>(handles_[i].gpiod_line), val);
    }
#endif
}

// ---------------------------------------------------------------------------
// openChip() — acquire GPIO chip handle.
// ---------------------------------------------------------------------------
bool GPIORTBackend::openChip() noexcept
{
#if HAS_LIBGPIOD
    auto* chip = gpiod_chip_open(chipPath_.c_str());
    if (!chip) return false;

    chipHandle_ = chip;
    return true;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// requestLine() — request a single line from libgpiod.
// ---------------------------------------------------------------------------
bool GPIORTBackend::requestLine(GPIOLine& line, size_t index) noexcept
{
#if HAS_LIBGPIOD
    auto* chip = static_cast<struct gpiod_chip*>(chipHandle_);
    if (!chip) return false;

    const char* consumer = "EtherCatDrone";

    struct gpiod_line* l = gpiod_chip_get_line(chip, line.offset);
    if (!l) return false;

    if (line.direction == LineDirection::OUTPUT) {
        int init_val = line.initialVal ? 1 : 0;
        if (gpiod_line_request_output(l, consumer, init_val) != 0) return false;
    } else {
        if (gpiod_line_request_input(l, consumer) != 0) return false;
    }

    handles_[index].gpiod_line = l;
    return true;
#else
    (void)line; (void)index; return false;
#endif
}

// ---------------------------------------------------------------------------
// closeChip() — release GPIO chip handle.
// ---------------------------------------------------------------------------
void GPIORTBackend::closeChip() noexcept
{
#if HAS_LIBGPIOD
    if (chipHandle_) {
        gpiod_chip_close(static_cast<struct gpiod_chip*>(chipHandle_));
        chipHandle_ = nullptr;
    }
#endif
}

} // namespace dynamichardware::gpio
