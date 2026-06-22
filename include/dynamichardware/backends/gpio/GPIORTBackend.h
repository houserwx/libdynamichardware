#pragma once
#include "dynamichardware/dhdo/IRuntimeAdapter.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"
#include "dynamichardware/backends/gpio/BoardVariant.h"

#include <string>
#include <unordered_map>
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
/// Real-time GPIO backend for I/O cycles.
///
/// Fully independent of discovery: opens its own chip handle in buildRT(),
/// reads catalog entries discovered by GPIODiscovery, builds PDOs from scratch.
class GPIORTBackend final
    : public dynamichardware::dhdo::IRuntimeAdapter {
public:
    /// Default constructor for self-registration via BackendRegistry.
    GPIORTBackend();

    /// Legacy parameterized constructor (retained for direct instantiation).
    GPIORTBackend(BoardVariant variant, std::string chipPath);

    ~GPIORTBackend() override;

    /// Inject per-backend configuration from orchestrator's enabledBackends map.
    /// Recognized keys: "chipPath" (optional; auto-detected if not provided).
    void configure(const std::unordered_map<std::string, std::string>& config) override;

    // --- RT lifecycle methods ----------------------------------------------
    /// Build PDO structure from consumer-selected entries and allocate image buffers.
    [[nodiscard]] bool buildRT();
    /// Pre-read hook: backend fills process image with fresh data.
    void onBeforeReadInputs()  noexcept override;
    /// Post-write hook: backend flushes process image to physical hardware.
    void onAfterWriteOutputs() noexcept override;

    // --- Builder interface -------------------------------------------------
    /// Set catalog reference so backend can resolve UUIDs internally.
    /// Called by orchestrator/factory BEFORE build(channels).
    void setCatalog(const dynamichardware::dhdo::HardwareCatalog* catalog) noexcept;

    /// Build RT state from mapped channel list. Looks up each UUID in the catalog
    /// and constructs internal GPIOLine entries without exposing GpioBackendData.
    [[nodiscard]] bool build(const std::vector<dynamichardware::dhdo::MappedChannel>& channels) override;

    [[nodiscard]] BoardVariant boardVariant() const noexcept { return variant_; }
    [[nodiscard]] std::size_t  lineCount()     const noexcept { return lines_.size(); }
    [[nodiscard]] bool         isActivated()   const noexcept { return activated_; }
    [[nodiscard]] bool         isStubMode()    const noexcept { return stubMode_; }

private:
    const dynamichardware::dhdo::HardwareCatalog* catalog_{nullptr};
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

    /// Populate lines_ vector from MappedChannel list using catalog lookups.
    void populateLinesFromChannels(const std::vector<dynamichardware::dhdo::MappedChannel>& channels);

    /// Internal helpers called from buildRT().
    bool openChip() noexcept;
    bool requestLine(GPIOLine& line, size_t index) noexcept;
    void closeChip() noexcept;
    void syncImagePointers();
};

} // namespace dynamichardware::gpio
