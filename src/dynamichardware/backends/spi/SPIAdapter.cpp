#include "dynamichardware/backends/spi/SPIAdapter.h"

#include <cstdio>
#include <cstring>

namespace dynamichardware::spi {

SPIAdapter::SPIAdapter(std::string busPath)
    : busPath_(std::move(busPath))
{
}

bool SPIAdapter::initialize()
{
    // Phase 1: Stub — will open SPI bus via /dev/spidevX.Y.
    // For now, just validate that devices have been registered.
    if (devices_.empty()) {
        std::fprintf(stderr, "[SPIAdapter] No devices registered\n");
        return false;
    }

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
    // Some devices may need configuration writes.
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
    // Phase 1: Stub
    (void)cs; (void)tx; (void)rx; (void)len;
    return false;
}

} // namespace dynamichardware::spi
