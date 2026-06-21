#pragma once
#include "dynamichardware/dhdo/IBackendScanner.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"

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

class I2CDiscovery final
    : public dynamichardware::dhdo::IBackendScanner {
public:
    explicit I2CDiscovery(std::string busPath);
    ~I2CDiscovery() override = default;

    /// Attach target catalog — discover() will register entries here after scan().
    void setCatalog(dhdo::HardwareCatalog* catalog) noexcept { catalog_ = catalog; }

    // Non-copyable, non-movable
    I2CDiscovery(const I2CDiscovery&)            = delete;
    I2CDiscovery& operator=(const I2CDiscovery&) = delete;
    I2CDiscovery(I2CDiscovery&&)                 = delete;
    I2CDiscovery& operator=(I2CDiscovery&&)      = delete;

    /// Pure data scan — returns descriptors without mutating catalog.
    [[nodiscard]] std::vector<dhdo::HardwareDescriptor> scan() override;

    /// Legacy wrapper — calls scan(), feeds results into catalog_.
    [[nodiscard]] bool discover();

private:
    std::string            busPath_;
    dhdo::HardwareCatalog* catalog_{nullptr};

    /// Check if the given bus path is accessible.
    bool validateBus() noexcept;
};

} // namespace dynamichardware::i2c
