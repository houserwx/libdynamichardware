#pragma once
#include "dynamichardware/dhdo/IBackendScanner.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"
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

/// ---- GPIODiscovery -------------------------------------------------------
/// One-shot scanner for available GPIO lines.
///
/// Opens the gpiochip, scans every line for kernel claims, populates catalog
/// with available pins, then releases everything on destruction/reset().
/// No PDO entries or RT state is created.
class GPIODiscovery final
    : public dynamichardware::dhdo::IBackendScanner {
public:
    /// Construct with auto-detected board variant and chip path.
    GPIODiscovery();

    /// Construct with explicit board variant and chip path.
    GPIODiscovery(BoardVariant variant, std::string chipPath);

    ~GPIODiscovery() override;

    /// Attach target catalog — discover() will register entries here after scan().
    void setCatalog(dhdo::HardwareCatalog* catalog) noexcept { catalog_ = catalog; }

    [[nodiscard]] BoardVariant boardVariant() const noexcept { return variant_; }

    /// Pure data scan — open chip, probe lines, return descriptors without mutating catalog.
    [[nodiscard]] std::vector<dhdo::HardwareDescriptor> scan() override;

    /// Legacy wrapper — calls scan(), feeds results into catalog_.
    [[nodiscard]] bool discover();

    /// Release all resources early if desired (also called by destructor).
    void reset() noexcept;

private:
#if HAS_LIBGPIOD
    struct gpiod_chip*   chipHandle_{nullptr};
#else
    void*                chipHandle_{nullptr};
#endif
    dhdo::HardwareCatalog* catalog_{nullptr};
    BoardVariant           variant_{BoardVariant::UNKNOWN};
    std::string            chipPath_;
    uint32_t               availableLineCount_{0};
};

} // namespace dynamichardware::gpio
