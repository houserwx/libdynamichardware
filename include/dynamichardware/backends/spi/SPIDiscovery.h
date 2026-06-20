#pragma once
#include "dynamichardware/dhdo/IDiscoveryBackend.h"

#include <string>

// ============================================================================
// SPIDiscovery — one-shot SPI bus scanner.
//
// Separation rationale (ISP): Discovery is a transient init-time concern.
// After discover() completes, this object can be destroyed entirely.
// No state survives into frozen RT mode.
//
// Lifecycle:
//   1. Context creates SPIDiscovery instance (with bus path)
//   2. Calls setCatalog(&catalog) so discovered channels get registered
//   3. Calls discover() → validates bus access, populates HardwareCatalog
//   4. Discovery object destroyed or discarded after consumer configuration phase
//
// Phase 1: Stub implementation — validates bus path exists via sysfs/dev check.
// Real SPI device probing will be implemented when hardware is available.
// ============================================================================

namespace dynamichardware::spi {

class SPIDiscovery final : public dynamichardware::dhdo::IDiscoveryBackend {
public:
    explicit SPIDiscovery(std::string busPath);
    ~SPIDiscovery() override = default;

    // Non-copyable, non-movable
    SPIDiscovery(const SPIDiscovery&)            = delete;
    SPIDiscovery& operator=(const SPIDiscovery&) = delete;
    SPIDiscovery(SPIDiscovery&&)                 = delete;
    SPIDiscovery& operator=(SPIDiscovery&&)      = delete;

    /// Validate SPI bus accessibility and populate catalog from any detected devices.
    /// Returns true if at least one device found (or stub mode succeeds).
    [[nodiscard]] bool discover() override;

private:
    std::string busPath_;

    /// Check if the given bus path is accessible.
    bool validateBus() noexcept;
};

} // namespace dynamichardware::spi
