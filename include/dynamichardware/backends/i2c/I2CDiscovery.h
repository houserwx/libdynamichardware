#pragma once
#include "dynamichardware/dhdo/IDiscoveryBackend.h"

#include <string>

// ============================================================================
// I2CDiscovery — one-shot I2C bus scanner.
//
// Separation rationale (ISP): Discovery is a transient init-time concern.
// After discover() completes, this object can be destroyed entirely.
// No state survives into frozen RT mode.
//
// Lifecycle:
//   1. Context creates I2CDiscovery instance (with bus path)
//   2. Calls setCatalog(&catalog) so discovered channels get registered
//   3. Calls discover() → validates bus access, populates HardwareCatalog
//   4. Discovery object destroyed or discarded after consumer configuration phase
//
// Phase 1: Stub implementation — validates bus path exists via sysfs/dev check.
// Real I2C device probing will be implemented when hardware is available.
// ============================================================================

namespace dynamichardware::i2c {

class I2CDiscovery final : public dynamichardware::dhdo::IDiscoveryBackend {
public:
    explicit I2CDiscovery(std::string busPath);
    ~I2CDiscovery() override = default;

    // Non-copyable, non-movable
    I2CDiscovery(const I2CDiscovery&)            = delete;
    I2CDiscovery& operator=(const I2CDiscovery&) = delete;
    I2CDiscovery(I2CDiscovery&&)                 = delete;
    I2CDiscovery& operator=(I2CDiscovery&&)      = delete;

    /// Validate I2C bus accessibility and populate catalog from any detected devices.
    /// Returns true if at least one device found (or stub mode succeeds).
    [[nodiscard]] bool discover() override;

private:
    std::string busPath_;

    /// Check if the given bus path is accessible.
    bool validateBus() noexcept;
};

} // namespace dynamichardware::i2c
