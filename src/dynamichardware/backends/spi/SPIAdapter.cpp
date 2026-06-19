#include "dynamichardware/backends/spi/SPIAdapter.h"

#include <cstdio>
#include <cstring>

namespace dynamichardware::spi {

SPIAdapter::SPIAdapter(std::string busPath)
    : busPath_(std::move(busPath))
{
}

// ---------------------------------------------------------------------------
// IDiscoveryBackend::discover() — transient catalog population phase.
// Validates bus path and populates catalog from pre-registered devices.
// ---------------------------------------------------------------------------
bool SPIAdapter::discover()
{
    if (!catalog_ || devices_.empty()) {
        std::fprintf(stderr, "[SPIAdapter] No catalog or no devices registered\n");
        return false;
    }

    // Populate catalog with entries from all registered devices
    for (const auto& device : devices_) {
        for (const auto* entry : device.entries) {
            dynamichardware::pdo::CatalogEntry catEntry;
            catEntry.key     = "SPI|" + std::to_string(device.bus) + "|" + std::to_string(device.chipSelect);
            catEntry.uuid    = entry->uuid;
            catEntry.channelType = "SensorInput";  // Generic type for now
            catEntry.name    = device.name;
            catEntry.slaveName = "SPI:" + std::to_string(device.chipSelect);
            catEntry.slavePos = 0;
            catEntry.isOutput = false;
            catalog_->addEntry(std::move(catEntry));
        }
    }

    std::printf("[SPIAdapter] Discovered %zu devices in catalog\n", devices_.size());
    return true;
}

// ---------------------------------------------------------------------------
// IRTBackend::buildRT() — persistent RT setup phase.
// Opens SPI bus handle and constructs PDO structure from registered devices.
// Called after consumer configuration but before freeze().
// ---------------------------------------------------------------------------
bool SPIAdapter::buildRT()
{
    if (devices_.empty()) {
        std::fprintf(stderr, "[SPIAdapter] No devices to build RT PDOs for\n");
        return false;
    }

    // Phase 1: Stub — will open SPI bus via /dev/spidevX.Y.
    // For now, just construct PDOs without real hardware access.
    spiFd_ = -1; // Placeholder until real implementation

    // Create PDOs for each device
    for (auto& device : devices_) {
        dynamichardware::pdo::PDO pdo;
        // Dereference pointers to create value vector
        pdo.entries.reserve(device.entries.size());
        for (auto* entry : device.entries) {
            pdo.entries.push_back(*entry);
        }
        // Allocate image buffer (float per entry for sensor data)
        pdo.image.resize(pdo.entries.size() * sizeof(float));

        // Set image pointers into each entry
        for (size_t i = 0; i < pdo.entries.size(); ++i) {
            pdo.entries[i].image = pdo.image.data() + (i * sizeof(float));
        }

        pdos_.push_back(std::move(pdo));
    }

    std::printf("[SPIAdapter] Built RT: %zu PDOs\n", pdos_.size());
    return true;
}

void SPIAdapter::onBeforeReadInputs() noexcept
{
    // Phase 1: Stub — will read from SPI devices into PDO image buffers.
    // For now, entries remain at zero (default-initialized).
    // Real implementation will use spi_ioc_transfer ioctl.
}

void SPIAdapter::onAfterWriteOutputs() noexcept
{
    // Phase 1: Stub — SPI sensors are typically input-only.
    // Some devices may need configuration register writes.
}

int SPIAdapter::registerDevice(uint8_t chipSelect,
                               std::string name,
                               std::vector<dynamichardware::pdo::EntryType> entryTypes)
{
    SPIDevice device;
    device.chipSelect = chipSelect;
    device.name = std::move(name);

    // Create PDOEntry for each channel
    for (const auto& type : entryTypes) {
        dynamichardware::pdo::PDOEntry entry;
        entry.type = type;
        entry.uuid = "spi:" + std::to_string(chipSelect) + ":" + std::to_string(device.entries.size());
        device.entries.push_back(&entry);
    }

    const int idx = static_cast<int>(devices_.size());
    devices_.push_back(std::move(device));
    return idx;
}

bool SPIAdapter::transfer(uint8_t cs, const uint8_t* tx, uint8_t* rx, size_t len) noexcept
{
    // Phase 1: Stub — will use spi_ioc_transfer ioctl when real hardware available.
    (void)cs; (void)tx; (void)rx; (void)len;
    return false;
}

} // namespace dynamichardware::spi
