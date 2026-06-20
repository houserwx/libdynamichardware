#include "dynamichardware/backends/i2c/I2CRTBackend.h"

#include <cstdio>

namespace dynamichardware::i2c {

I2CRTBackend::I2CRTBackend(std::string busPath)
    : busPath_(std::move(busPath)) {}

// ---------------------------------------------------------------------------
// buildRT() — construct PDOs from pre-registered devices (stub mode).
// Fully self-contained: does NOT depend on any prior discovery scan.
// ---------------------------------------------------------------------------
bool I2CRTBackend::buildRT()
{
    if (devices_.empty()) {
        std::fprintf(stderr, "[I2C] No devices to build RT PDOs for\n");
        return false;
    }

    // Stub — no real hardware access yet.
    i2cFd_ = -1;

    for (auto& device : devices_) {
        dynamichardware::dhdo::DHDO pdo;
        pdo.entries.reserve(device.entries.size());
        for (auto* entry : device.entries) {
            pdo.entries.push_back(*entry);
        }

        pdo.image.resize(pdo.entries.size() * sizeof(float));
        for (size_t i = 0; i < pdo.entries.size(); ++i) {
            pdo.entries[i].image = pdo.image.data() + (i * sizeof(float));
        }

        dhdos_.push_back(std::move(pdo));
    }

    std::printf("[I2C] Built RT: %zu PDOs\n", dhdos_.size());
    return true;
}

void I2CRTBackend::onBeforeReadInputs() noexcept {}
void I2CRTBackend::onAfterWriteOutputs() noexcept {}

int I2CRTBackend::registerDevice(uint8_t deviceAddr, std::string name,
                                  std::vector<dynamichardware::dhdo::EntryType> entryTypes)
{
    Device device{};
    device.address = deviceAddr;
    device.name    = std::move(name);

    for (const auto& type : entryTypes) {
        dynamichardware::dhdo::DHDOEntry entry{};
        entry.type = type;
        entry.uuid = "i2c:" + std::to_string(deviceAddr) + ":" + std::to_string(device.entries.size());
        device.entries.push_back(&entry);
    }

    const int idx = static_cast<int>(devices_.size());
    devices_.push_back(std::move(device));
    return idx;
}

bool I2CRTBackend::writeRegister(uint8_t addr, uint8_t reg, uint8_t value) noexcept {
    (void)addr; (void)reg; (void)value; return false;
}

bool I2CRTBackend::readRegisters(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) noexcept {
    (void)addr; (void)reg; (void)buf; (void)len; return false;
}

} // namespace dynamichardware::i2c
