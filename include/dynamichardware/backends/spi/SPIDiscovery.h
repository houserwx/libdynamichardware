#pragma once
#include "dynamichardware/dhdo/IBackendScanner.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"

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

class SPIDiscovery final
    : public dynamichardware::dhdo::IBackendScanner {
public:
    explicit SPIDiscovery(std::string busPath);
    ~SPIDiscovery() override = default;

    /// Attach target catalog — discover() will register entries here after scan().
    void setCatalog(dhdo::HardwareCatalog* catalog) noexcept { catalog_ = catalog; }

    // Non-copyable, non-movable
    SPIDiscovery(const SPIDiscovery&)            = delete;
    SPIDiscovery& operator=(const SPIDiscovery&) = delete;
    SPIDiscovery(SPIDiscovery&&)                 = delete;
    SPIDiscovery& operator=(SPIDiscovery&&)      = delete;

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

} // namespace dynamichardware::spi
