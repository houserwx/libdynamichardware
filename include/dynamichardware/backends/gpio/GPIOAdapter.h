#pragma once
#include "dynamichardware/backends/gpio/BoardVariant.h"
#include "dynamichardware/pdo/IHardwareAdapter.h"
#include "dynamichardware/pdo/HardwareCatalog.h"

#include <string>
#include <vector>
#include <cstdint>

// ============================================================================
// GPIOAdapter — GPIO backend adapter for the PDO system.
//
// Implements IHardwareAdapter so GPIO lines appear as regular PDO entries
// to the RT cycle.  Supports both input (digital reads) and output (digital
// writes) lines, with optional debounce for inputs and PWM for outputs.
//
// Raspberry Pi support:
//   - Pi 4 (BCM2711): gpiochip0, 54 lines, libgpiod v1/v2
//   - Pi 5 (BCM2712): gpiochip0, 54 lines, libgpiod v2
//   - Auto-detection via /proc/device-tree/model or compatible
//
// Lifecycle:
//   1. Construct with chip path (auto-detected if empty).
//   2. Call registerLine() for each GPIO line needed (init time).
//   3. Call initialize() — opens gpiochip, requests line handles, creates PDOs.
//   4. RT cycle: onBeforeReadInputs() reads input lines into PDO image.
//   5. RT cycle: onAfterWriteOutputs() writes output lines from PDO image.
//
// Phase 1: libgpiod-based implementation with real hardware access.
// Falls back to stub mode if libgpiod or /dev/gpiochip* is unavailable.
// ============================================================================

namespace dynamichardware::gpio {

/// GPIO line direction
enum class LineDirection : uint8_t {
    INPUT  = 0,
    OUTPUT = 1,
};

/// GPIO line configuration
struct GPIOLine {
    uint32_t    offset{0};          // GPIO line number (e.g., 17 for GPIO17)
    LineDirection direction{LineDirection::INPUT};
    std::string  name;              // Human-readable: "GPIO17-Encoder-A"
    bool         activeHigh{true};  // Line is active-high (invert=false)

    // Input-specific
    uint32_t     debounceUs{0};     // Hardware debounce in microseconds (0=disabled)

    // Output-specific
    bool         initialVal{false}; // Initial output value
    bool         pwmEnabled{false}; // PWM mode (requires hardware PWM or bit-bang)
    uint32_t     pwmFrequency{0};   // PWM frequency in Hz
    float        pwmDutyCycle{0.0f};// PWM duty cycle 0.0-1.0

    // PDO entry pointer (owned by this adapter, registered during init)
    dynamichardware::pdo::PDOEntry* entry{nullptr};
};

class GPIOAdapter final : public dynamichardware::pdo::IHardwareAdapter {
public:
    /// Construct with auto-detected board variant and chip path.
    GPIOAdapter();

    /// Construct with explicit board variant and chip path.
    GPIOAdapter(BoardVariant variant, std::string chipPath);

    ~GPIOAdapter() override = default;

    bool initialize() override;
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

    void setCatalog(dynamichardware::pdo::HardwareCatalog* catalog) noexcept { catalog_ = catalog; }

    /// Register a GPIO line and create a PDO entry for it.
    /// @param gpio_offset    GPIO line offset (BCM number, e.g., 17)
    /// @param direction      INPUT or OUTPUT
    /// @param name           Human-readable name
    /// @param entryType      PDO EntryType (DigitalInput or DigitalOutput)
    /// @return               Index of the created GPIOLine in lines_
    int registerLine(uint32_t gpio_offset,
                     LineDirection direction,
                     std::string name,
                     dynamichardware::pdo::EntryType entryType);

    /// Get the detected board variant.
    [[nodiscard]] BoardVariant boardVariant() const noexcept { return variant_; }

    /// Get the number of registered GPIO lines.
    [[nodiscard]] std::size_t lineCount() const noexcept { return lines_.size(); }

    /// Check if running in stub mode (no real hardware).
    [[nodiscard]] bool isStubMode() const noexcept { return stubMode_; }

private:
    BoardVariant      variant_{BoardVariant::UNKNOWN};
    std::string       chipPath_;
    dynamichardware::pdo::HardwareCatalog* catalog_{nullptr};
    std::vector<GPIOLine> lines_;
    bool                stubMode_{false};

    // libgpiod handles (real hardware mode)
    struct LineHandle {
        // libgpiod line handle — abstracted behind opaque pointer
        void* gpiod_line{nullptr};
    };
    void*               chipHandle_{nullptr}; // gpiod_chip* stored separately
    std::vector<LineHandle> handles_;

    // Stub fallback mode (no libgpiod)
    struct StubState {
        bool value{false};
        uint64_t toggleCycle{0};
    };
    std::vector<StubState> stubStates_;

    // Internal helpers
    void discoverLines();
    bool openChip() noexcept;
    bool requestLine(GPIOLine& line, size_t index) noexcept;
    void closeChip() noexcept;
};

} // namespace dynamichardware::gpio
