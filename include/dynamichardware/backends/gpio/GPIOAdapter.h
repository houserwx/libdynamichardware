#pragma once
#include "dynamichardware/backends/gpio/BoardVariant.h"
#include "dynamichardware/pdo/IDiscoveryBackend.h"
#include "dynamichardware/pdo/IRTBackend.h"
#include "dynamichardware/pdo/HardwareCatalog.h"

#include <string>
#include <vector>
#include <cstdint>

// ============================================================================
// GPIOAdapter — GPIO backend adapter for the PDO system.
//
// Implements both IDiscoveryBackend and IRTBackend so GPIO lines appear as 
// regular PDO entries to the RT cycle.  Supports both input (digital reads) 
// and output (digital writes) lines, with optional debounce for inputs and 
// PWM for outputs.
//
// Raspberry Pi support:
//   - Pi 4 (BCM2711): gpiochip0, 54 lines, libgpiod v1/v2
//   - Pi 5 (BCM2712): gpiochip0, 54 lines, libgpiod v2
//   - Auto-detection via /proc/device-tree/model or compatible
//
// Two-phase lifecycle (ISP split):
//
// DISCOVERY PHASE (IDiscoveryBackend — transient, discarded after build):
//   1. Construct with chip path (auto-detected if empty).
//   2. Call discover() — opens gpiochip, scans available pins, populates 
//      hardware catalog only (no PDOEntry or handle creation).
//   3. Discovery interface can be destroyed — no state survives into freeze.
//
// SETUP + ACTIVATION (IRTBackend — survives through frozen mode):
//   4. Application inspects catalog to find desired pins by UUID.
//   5. App calls registerLine(offset, direction, name, type) for each pin.
//   6. deferredActivate()/buildRT() — requests libgpiod handles ONLY for 
//      registered lines; builds PDO structure from GPIOLine entries; 
//      allocates process-image buffers.
//
// RT CYCLE:
//   7. onBeforeReadInputs() reads ONLY registered/input lines into PDO image.
//   8. onAfterWriteOutputs() writes ONLY registered/output lines from PDO image.
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

// Inherits both discovery (transient) and RT lifecycle (persistent) interfaces.
// Discovery phase can be discarded after build(); RT interface survives through freeze.
class GPIOAdapter final
    : public dynamichardware::pdo::IDiscoveryBackend,
      public dynamichardware::pdo::IRTBackend {
public:
    /// Construct with auto-detected board variant and chip path.
    GPIOAdapter();

    /// Construct with explicit board variant and chip path.
    GPIOAdapter(BoardVariant variant, std::string chipPath);

    ~GPIOAdapter() override = default;

    // --- IDiscoveryBackend implementation -----------------------------------
    [[nodiscard]] bool discover() override;

    // --- IRTBackend implementation ------------------------------------------
    [[nodiscard]] bool buildRT() override;
    void activate() override { deferredActivate(); }
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

    /// Called during freezeForRt() — activates only explicitly registered lines.
    /// Requests hardware handles, creates PDOs, and allocates process-image buffers.
    /// After this call no further registerLine() calls are permitted.
    void deferredActivate();

    void setCatalog(dynamichardware::pdo::HardwareCatalog* catalog) noexcept { catalog_ = catalog; }

    /// Check if the adapter has been activated (deferredActivate called).
    [[nodiscard]] bool isActivated() const noexcept { return activated_; }

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

    /// Get the number of registered GPIO lines (actively used in RT cycle).
    [[nodiscard]] std::size_t lineCount() const noexcept { return lines_.size(); }

    /// Get the number of available lines discovered in the catalog.
    [[nodiscard]] uint32_t availableLineCount() const noexcept { return availableLineCount_; }

    /// Check if running in stub mode (no real hardware).
    [[nodiscard]] bool isStubMode() const noexcept { return stubMode_; }

private:
    BoardVariant      variant_{BoardVariant::UNKNOWN};
    std::string       chipPath_;
    dynamichardware::pdo::HardwareCatalog* catalog_{nullptr};
    std::vector<GPIOLine> lines_;
    uint32_t            availableLineCount_{0};  ///< Lines discovered in catalog
    bool                stubMode_{false};
    bool                activated_{false};        ///< true after deferredActivate()

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
