#pragma once
#include "dynamichardware/dhdo/IBackendScanner.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"

#include <string>
#include <unordered_map>

// ============================================================================
// SPIDiscovery — one-shot SPI bus scanner.
//
// Separation rationale (ISP): Discovery is a transient init-time concern.
// After discover() completes, this object can be destroyed entirely.
// No state survives into frozen RT mode.
// ============================================================================

namespace dynamichardware::spi {

class SPIDiscovery final
    : public dynamichardware::dhdo::IBackendScanner {
public:
    /// Default constructor for self-registration via BackendRegistry.
    SPIDiscovery();

    /// Legacy parameterized constructor (retained for direct instantiation).
    explicit SPIDiscovery(std::string busPath);
    ~SPIDiscovery() override = default;

    /// Inject per-backend configuration from orchestrator's enabledBackends map.
    void configure(const std::unordered_map<std::string, std::string>& config) override;

    /// Attach target catalog — discover() will register entries here after scan().
    void setCatalog(dhdo::HardwareCatalog* catalog) noexcept { catalog_ = catalog; }

    // Non-copyable, non-movable
    SPIDiscovery(const SPIDiscovery&)            = delete;
    SPIDiscovery& operator=(const SPIDiscovery&) = delete;
    SPIDiscovery(SPIDiscovery&&)                 = delete;
    SPIDiscovery& operator=(SPIDiscovery&&)      = delete;

    /// Pure data scan — returns descriptors without mutating catalog.
    [[nodiscard]] std::vector<dhdo::HardwareDescriptor> scan() override;

    /// Convenience wrapper — calls scan() through IBackendScanner, feeds results into catalog_.
    [[nodiscard]] bool discover();

private:
    std::string            busPath_;
    dhdo::HardwareCatalog* catalog_{nullptr};

    /// Check if the given bus path is accessible.
    bool validateBus() noexcept;
};

} // namespace dynamichardware::spi
