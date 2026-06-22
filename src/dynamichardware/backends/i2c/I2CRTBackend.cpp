#include "dynamichardware/backends/i2c/I2CRTBackend.h"

#include <cstdio>
#include <string>

namespace dynamichardware::i2c {

// Default constructor for self-registration via BackendRegistry.
I2CRTBackend::I2CRTBackend()
    : busPath_("/dev/i2c-1") {}  // Default fallback path

I2CRTBackend::I2CRTBackend(std::string busPath)
    : busPath_(std::move(busPath)) {}

void I2CRTBackend::configure(const std::unordered_map<std::string, std::string>& config)
{
    auto it = config.find("busPath");
    if (it != config.end()) {
        busPath_ = it->second;
    }
}

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

// ---------------------------------------------------------------------------
// IRuntimeAdapter::setCatalog()
// ---------------------------------------------------------------------------
void I2CRTBackend::setCatalog(const dynamichardware::dhdo::HardwareCatalog* catalog) noexcept
{
    catalog_ = catalog;
}

// ---------------------------------------------------------------------------
// IDHDOBuilder::build(channels) — populate devices from mapped channels.
// Stub implementation: collects channel info but doesn't access real hardware yet.
// ---------------------------------------------------------------------------
bool I2CRTBackend::build(const std::vector<dynamichardware::dhdo::MappedChannel>& channels)
{
    // Group channels by backend data (device address).
    if (!channels.empty()) {
        Device device{};
        for (const auto& ch : channels) {
            dynamichardware::dhdo::DHDOEntry entry{};
            entry.type = ch.type;
            entry.uuid = ch.uuid;
            device.entries.push_back(&entry);
        }
        device.name = "I2C-Device";
        devices_.push_back(std::move(device));
    }

    return buildRT();  // Delegate to existing PDO construction logic.
}

bool I2CRTBackend::writeRegister(uint8_t addr, uint8_t reg, uint8_t value) noexcept {
    (void)addr; (void)reg; (void)value; return false;
}

bool I2CRTBackend::readRegisters(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) noexcept {
    (void)addr; (void)reg; (void)buf; (void)len; return false;
}

} // namespace dynamichardware::i2c
