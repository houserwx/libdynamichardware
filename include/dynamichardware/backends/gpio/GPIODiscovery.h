#pragma once
#include "dynamichardware/dhdo/IDiscoveryBackend.h"
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
/// One-shot scanner for available GPIO lines. Implements IDiscoveryBackend.
///
/// Opens the gpiochip, scans every line for kernel claims, populates catalog
/// with available pins, then releases everything on destruction/reset().
/// No PDO entries or RT state is created.
class GPIODiscovery final : public dynamichardware::dhdo::IDiscoveryBackend {
public:
    /// Construct with auto-detected board variant and chip path.
    GPIODiscovery();

    /// Construct with explicit board variant and chip path.
    GPIODiscovery(BoardVariant variant, std::string chipPath);

    ~GPIODiscovery() override;

    // setCatalog inherited from IDiscoveryBackend.

    [[nodiscard]] BoardVariant boardVariant() const noexcept { return variant_; }

    /// Open chip, scan lines into catalog (no DHDOEntry/handle creation), release chip.
    [[nodiscard]] bool discover() override;

    /// Release all resources early if desired (also called by destructor).
    void reset() noexcept;

private:
#if HAS_LIBGPIOD
    struct gpiod_chip*   chipHandle_{nullptr};
#else
    void*                chipHandle_{nullptr};
#endif
    BoardVariant      variant_{BoardVariant::UNKNOWN};
    std::string       chipPath_;
    uint32_t          availableLineCount_{0};
};

} // namespace dynamichardware::gpio
