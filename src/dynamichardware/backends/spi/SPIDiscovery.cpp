#include "dynamichardware/backends/spi/SPIDiscovery.h"

#include "dynamichardware/dhdo/HardwareCatalog.h"
#include "dynamichardware/dhdo/HardwareDescriptor.h"

#include "dynamichardware/backends/registration.h"
#include "dynamichardware/backends/spi/SPIRTBackend.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>

namespace dynamichardware::spi {

// ---------------------------------------------------------------------------
// Default constructor — used by self-registration (REGISTER_BACKEND macro).
// Config injected later via configure() after factory instantiation.
// ---------------------------------------------------------------------------
SPIDiscovery::SPIDiscovery() = default;

// ---------------------------------------------------------------------------
// Parameterized constructor — legacy direct-instantiation path.
// ---------------------------------------------------------------------------
SPIDiscovery::SPIDiscovery(std::string busPath)
    : busPath_(std::move(busPath)) {}

// ---------------------------------------------------------------------------
// Post-creation configuration hook (Phase 8 OCP compliance).
// Orchestrator calls this AFTER factory lambda returns the scanner+adapter pair,
// passing config from enabledBackends map. Must extract "busPath" here
// so scan() can validate the correct SPI bus device during discovery phase.
// ---------------------------------------------------------------------------
void SPIDiscovery::configure(const std::unordered_map<std::string, std::string>& config)
{
    auto it = config.find("busPath");
    if (it != config.end()) {
        busPath_ = it->second;
    }
}

// ---------------------------------------------------------------------------
// IBackenScanner::scan() — pure data scan: validate bus, return empty descriptors in stub mode.
// Stub implementation returns empty vector (no devices auto-discovered yet).
// ---------------------------------------------------------------------------
std::vector<dhdo::HardwareDescriptor> SPIDiscovery::scan()
{
    std::vector<dhdo::HardwareDescriptor> results;
    if (!validateBus()) {
        std::printf("[SPI-Discovery] Bus '%s' not accessible (stub mode)\n", busPath_.c_str());
        return results;
    }
    // Note: In stub mode, no devices are auto-discovered. Consumer pre-registers via RT backend.
    return results;
}

// ---------------------------------------------------------------------------
// discover() — thin wrapper: calls scan(), feeds results into catalog_.
// After return, this object can be destroyed — no state survives.
// ---------------------------------------------------------------------------
bool SPIDiscovery::discover()
{
    auto descriptors = scan();

    if (!catalog_) {
        std::fprintf(stderr, "[SPI-Discovery] No catalog attached\n");
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

    std::printf("[SPI-Discovery] Bus '%s' validated (%zu catalog entries registered)\n",
                busPath_.c_str(), catalog_->entries().size());

    // Note: In stub mode, consumer is expected to pre-register devices before calling discover().
    return !descriptors.empty() || validateBus();  // Succeed if bus accessible even with zero descriptors
}

// ---------------------------------------------------------------------------
// Helper: Check SPI bus accessibility via sysfs/dev filesystem.
// ---------------------------------------------------------------------------
bool SPIDiscovery::validateBus() noexcept
{
    // Try checking /sys/class/spi_dev/ first (most reliable on Linux)
    std::string sysFsPath = "/sys/class/spi_dev/" + busPath_;
    std::ifstream sysFs(sysFsPath);
    if (sysFs.good()) {
        return true;
    }

    // Fallback: check if /dev/spidevX.Y exists directly
    std::ifstream devFile(busPath_);
    return devFile.good();
}

// ---------------------------------------------------------------------------
// Self-registration with BackendRegistry — zero-boilerplate OCP compliance.
// ---------------------------------------------------------------------------
REGISTER_BACKEND("SPI", []() {
    return std::make_pair(
        std::make_unique<spi::SPIDiscovery>(),
        std::make_unique<spi::SPIRTBackend>()
    );
});

} // namespace dynamichardware::spi
