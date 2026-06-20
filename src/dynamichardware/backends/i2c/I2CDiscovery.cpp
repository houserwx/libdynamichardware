#include "dynamichardware/backends/i2c/I2CDiscovery.h"

#include "dynamichardware/dhdo/HardwareCatalog.h"

#include <cstdio>
#include <fstream>

namespace dynamichardware::i2c {

I2CDiscovery::I2CDiscovery(std::string busPath)
    : busPath_(std::move(busPath)) {}

// ---------------------------------------------------------------------------
// IDiscoveryBackend::discover() — transient catalog population phase.
// Validates I2C bus and populates catalog with any discovered devices.
// After return, this object can be destroyed — no state survives.
// ---------------------------------------------------------------------------
bool I2CDiscovery::discover()
{
    if (!catalog_) {
        std::fprintf(stderr, "[I2C-Discovery] No catalog attached\n");
        return false;
    }

    // Phase 1: Stub — validate bus path exists via sysfs check.
    // Real implementation will probe /dev/i2c-N or sysfs for connected devices.
    if (!validateBus()) {
        std::printf("[I2C-Discovery] Bus '%s' not accessible (stub mode)\n", busPath_.c_str());
        return false;
    }

    std::printf("[I2C-Discovery] Bus '%s' validated (%zu catalog entries registered)\n", 
                busPath_.c_str(), catalog_->entries().size());
    
    // Note: In stub mode, consumer is expected to pre-register devices before calling discover().
    // The discovery adapter validates the bus, but actual device registration happens via the RT backend's registerDevice API.
    // This keeps the discovery adapter truly transient — it only scans and reports what's available.

    return true;  // Always succeed in stub mode if bus validates
}

// ---------------------------------------------------------------------------
// Helper: Check I2C bus accessibility via sysfs/dev filesystem.
// ---------------------------------------------------------------------------
bool I2CDiscovery::validateBus() noexcept
{
    // Try checking /sys/class/i2c-dev/ first (most reliable on Linux)
    std::string sysFsPath = "/sys/class/i2c-dev/" + busPath_;
    std::ifstream sysFs(sysFsPath);
    if (sysFs.good()) {
        return true;
    }

    // Fallback: check if /dev/i2c-N exists directly
    std::ifstream devFile(busPath_);
    return devFile.good();
}

} // namespace dynamichardware::i2c
