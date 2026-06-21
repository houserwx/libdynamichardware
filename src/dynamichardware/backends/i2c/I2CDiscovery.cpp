#include "dynamichardware/backends/i2c/I2CDiscovery.h"

#include "dynamichardware/dhdo/HardwareCatalog.h"
#include "dynamichardware/dhdo/HardwareDescriptor.h"

#include <cstdio>
#include <fstream>
#include <vector>

namespace dynamichardware::i2c {

I2CDiscovery::I2CDiscovery(std::string busPath)
    : busPath_(std::move(busPath)) {}

// ---------------------------------------------------------------------------
// IBackenScanner::scan() — pure data scan: validate bus, return empty descriptors in stub mode.
// Stub implementation returns empty vector (no devices auto-discovered yet).
// ---------------------------------------------------------------------------
std::vector<dhdo::HardwareDescriptor> I2CDiscovery::scan()
{
    std::vector<dhdo::HardwareDescriptor> results;
    if (!validateBus()) {
        std::printf("[I2C-Discovery] Bus '%s' not accessible (stub mode)\n", busPath_.c_str());
        return results;
    }
    // Note: In stub mode, no devices are auto-discovered. Consumer pre-registers via RT backend.
    return results;
}

// ---------------------------------------------------------------------------
// discover() — thin wrapper: calls scan(), feeds results into catalog_.
// After return, this object can be destroyed — no state survives.
// ---------------------------------------------------------------------------
bool I2CDiscovery::discover()
{
    auto descriptors = scan();

    if (!catalog_) {
        std::fprintf(stderr, "[I2C-Discovery] No catalog attached\n");
        return false;
    }

    for (auto& desc : descriptors) {
        dhdo::CatalogEntry entry{};
        entry.uuid          = desc.uuid;
        entry.channelType   = desc.channelType;
        entry.name          = desc.name;
        entry.isOutput      = desc.isOutput;
        entry.backend       = desc.backend;
        entry.backendData   = std::move(desc.backendData);

        catalog_->addEntry(std::move(entry));
    }

    std::printf("[I2C-Discovery] Bus '%s' validated (%zu catalog entries registered)\n",
                busPath_.c_str(), catalog_->entries().size());

    // Note: In stub mode, consumer is expected to pre-register devices before calling discover().
    // The discovery adapter validates the bus, but actual device registration happens via the RT backend's registerDevice API.
    return !descriptors.empty() || validateBus();  // Succeed if bus accessible even with zero descriptors
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
