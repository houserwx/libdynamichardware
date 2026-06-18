#include "backends/i2c/I2CAdapter.h"

#include <cstdio>
#include <cstring>

namespace fc::i2c {

I2CAdapter::I2CAdapter(std::string busPath)
    : busPath_(std::move(busPath))
{
}

bool I2CAdapter::initialize()
{
    // Phase 1: Stub — will open I2C bus via /dev/i2c-N or sysfs.
    // For now, just validate that devices have been registered.
    if (devices_.empty()) {
        std::fprintf(stderr, "[I2CAdapter] No devices registered\n");
        return false;
    }

    // Create PDOs for each device
    for (auto& device : devices_) {
        fc::pdo::PDO pdo;
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
                               std::vector<fc::pdo::EntryType> entryTypes)
{
    I2CDevice device;
    device.address = deviceAddr;
    device.name = std::move(name);

    // Create PDOEntry for each channel
    for (const auto& type : entryTypes) {
        fc::pdo::PDOEntry entry;
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

} // namespace fc::i2c
