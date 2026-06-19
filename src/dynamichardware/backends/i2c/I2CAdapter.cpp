#include "dynamichardware/backends/i2c/I2CAdapter.h"

#include <cstdio>
#include <cstring>

namespace dynamichardware::i2c {

I2CAdapter::I2CAdapter(std::string busPath)
    : busPath_(std::move(busPath))
{
}

// ---------------------------------------------------------------------------
// IDiscoveryBackend::discover() — transient catalog population phase.
// Validates bus path and populates catalog from pre-registered devices.
// ---------------------------------------------------------------------------
bool I2CAdapter::discover()
{
    if (!catalog_ || devices_.empty()) {
        std::fprintf(stderr, "[I2CAdapter] No catalog or no devices registered\n");
        return false;
    }

    // Populate catalog with entries from all registered devices
    for (const auto& device : devices_) {
        for (const auto* entry : device.entries) {
            dynamichardware::pdo::CatalogEntry catEntry;
            catEntry.key     = "I2C|" + std::to_string(device.bus) + "|" + std::to_string(device.address);
            catEntry.uuid    = entry->uuid;
            catEntry.channelType = "SensorInput";  // Generic type for now
            catEntry.name    = device.name;
            catEntry.slaveName = "I2C:" + std::to_string(device.address);
            catEntry.slavePos = 0;
            catEntry.isOutput = false;
            catalog_->addEntry(std::move(catEntry));
        }
    }

    std::printf("[I2CAdapter] Discovered %zu devices in catalog\n", devices_.size());
    return true;
}

// ---------------------------------------------------------------------------
// IRTBackend::buildRT() — persistent RT setup phase.
// Opens I2C bus handle and constructs PDO structure from registered devices.
// Called after consumer configuration but before freeze().
// ---------------------------------------------------------------------------
bool I2CAdapter::buildRT()
{
    if (devices_.empty()) {
        std::fprintf(stderr, "[I2CAdapter] No devices to build RT PDOs for\n");
        return false;
    }

    // Phase 1: Stub — will open I2C bus via /dev/i2c-N or sysfs.
    // For now, just construct PDOs without real hardware access.
    i2cFd_ = -1; // Placeholder until real implementation

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

    std::printf("[I2CAdapter] Built RT: %zu PDOs\n", pdos_.size());
    return true;
}

void I2CAdapter::onBeforeReadInputs() noexcept
{
    // Phase 1: Stub — will read from I2C devices into PDO image buffers.
    // For now, entries remain at zero (default-initialized).
    // Real implementation will use i2c_smbus_read_byte_data or similar.
}

void I2CAdapter::onAfterWriteOutputs() noexcept
{
    // Phase 1: Stub — I2C sensors are typically input-only.
    // Some devices (e.g., IMU configuration registers) may need writes.
}

int I2CAdapter::registerDevice(uint8_t deviceAddr,
                               std::string name,
                               std::vector<dynamichardware::pdo::EntryType> entryTypes)
{
    I2CDevice device;
    device.address = deviceAddr;
    device.name = std::move(name);

    // Create PDOEntry for each channel
    for (const auto& type : entryTypes) {
        dynamichardware::pdo::PDOEntry entry;
        entry.type = type;
        entry.uuid = "i2c:" + std::to_string(deviceAddr) + ":" + std::to_string(device.entries.size());
        device.entries.push_back(&entry);
    }

    const int idx = static_cast<int>(devices_.size());
    devices_.push_back(std::move(device));
    return idx;
}

bool I2CAdapter::writeRegister(uint8_t addr, uint8_t reg, uint8_t value) noexcept
{
    // Phase 1: Stub
    (void)addr; (void)reg; (void)value;
    return false;
}

bool I2CAdapter::readRegisters(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) noexcept
{
    // Phase 1: Stub
    (void)addr; (void)reg; (void)buf; (void)len;
    return false;
}

} // namespace dynamichardware::i2c
