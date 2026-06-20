#pragma once
#include "dynamichardware/dhdo/IRTBackend.h"
#include "dynamichardware/backends/gpio/BoardVariant.h"

#include <string>
#include <vector>
#include <cstdint>

// Try to include libgpiod headers for type declarations only.
#ifdef GPIO_LIBGPIOD_AVAILABLE
#include <gpiod.h>
#define HAS_LIBGPIOD 2
#else
#define HAS_LIBGPIOD 0
#endif

namespace dynamichardware::gpio {

/// GPIO line direction (also used by GPIOLine config struct)
enum class LineDirection : uint8_t {
    INPUT  = 0,
    OUTPUT = 1,
};

/// GPIO line configuration — passed from DynamicHardwareContext to registerLine().
struct GPIOLine {
    uint32_t          offset{0};        // GPIO line number (e.g., 17 for GPIO17)
    LineDirection     direction{LineDirection::INPUT};
    std::string       name;             // Human-readable: "GPIO17-Encoder-A"
    bool              activeHigh{true};

    // Input-specific
    uint32_t          debounceUs{0};

    // Output-specific
    bool              initialVal{false};
    bool              pwmEnabled{false};
    uint32_t          pwmFrequency{0};
    float             pwmDutyCycle{0.0f};

    /// DHDOEntry owned BY VALUE — lives as long as the lines_ vector owns this GPIOLine.
    /// This is safe because value type survives scope exit (unlike stack-local pointers).
    dynamichardware::dhdo::DHDOEntry entry{};
};

/// ---- GPIORTBackend -------------------------------------------------------
/// Real-time GPIO backend for I/O cycles. Implements IRTBackend.
///
/// Fully independent of discovery: opens its own chip handle in buildRT(),
/// reads catalog entries discovered by GPIODiscovery, builds PDOs from scratch.
class GPIORTBackend final : public dynamichardware::dhdo::IRTBackend {
public:
    GPIORTBackend();
    GPIORTBackend(BoardVariant variant, std::string chipPath);

    ~GPIORTBackend() override;

    // --- IRTBackend implementation ------------------------------------------
    [[nodiscard]] bool buildRT() override;
    void activate() override { deferredActivate(); }
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

    /// Legacy compatibility wrapper — delegates to buildRT().
    void deferredActivate();

    /// Register a GPIO line for use in the process image. Must be called before buildRT().
    /// Called by DynamicHardwareContext during consumer configuration phase.
    int registerLine(uint32_t gpio_offset, LineDirection direction, std::string name,
                     dynamichardware::dhdo::EntryType entryType);

    [[nodiscard]] BoardVariant boardVariant() const noexcept { return variant_; }
    [[nodiscard]] std::size_t  lineCount()     const noexcept { return lines_.size(); }
    [[nodiscard]] bool         isActivated()   const noexcept { return activated_; }
    [[nodiscard]] bool         isStubMode()    const noexcept { return stubMode_; }

private:
#if HAS_LIBGPIOD
    struct gpiod_chip*   chipHandle_{nullptr};
#else
    void*                chipHandle_{nullptr};
#endif
    BoardVariant      variant_{BoardVariant::UNKNOWN};
    std::string       chipPath_;
    std::vector<GPIOLine> lines_;

    bool              stubMode_{false};
    bool              activated_{false};

    struct LineHandle {
        void* gpiod_line{nullptr}; // raw pointer into libgpiod handle — never freed individually (closed with chip)
    };
    std::vector<LineHandle> handles_;

    struct StubState {
        bool value{false};
        uint64_t toggleCycle{0};
    };
    std::vector<StubState> stubStates_;

    /// Internal helpers called from buildRT().
    bool openChip() noexcept;
    bool requestLine(GPIOLine& line, size_t index) noexcept;
    void closeChip() noexcept;
};

} // namespace dynamichardware::gpio
