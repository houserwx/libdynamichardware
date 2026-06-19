#pragma once
#include "dynamichardware/pdo/IRTBackend.h"

#include <string>
#include <vector>
#include <cstdint>

namespace dynamichardware::i2c {

/// ---- I2CRTBackend --------------------------------------------------------
/// Real-time I2C process-data backend. Implements IRTBackend.
///
/// Fully independent of discovery: acquires its own resources in buildRT(),
/// registers devices via registerDevice() (called by DynamicHardwareContext), builds PDOs from scratch.
class I2CRTBackend final : public dynamichardware::pdo::IRTBackend {
public:
    explicit I2CRTBackend(std::string busPath);
    ~I2CRTBackend() override = default;

    // --- IRTBackend implementation ------------------------------------------
    [[nodiscard]] bool buildRT() override;
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

    /// Register an I2C device before calling buildRT(). Returns device index.
    /// Called by DynamicHardwareContext during consumer configuration phase — NOT by external consumers directly.
    int registerDevice(uint8_t deviceAddr, std::string name,
                       std::vector<dynamichardware::pdo::EntryType> entryTypes);

private:
    struct Device {
        uint8_t  bus{0};
        uint8_t  address{0};
        std::string name;
        std::vector<dynamichardware::pdo::PDOEntry*> entries;
    };

    std::string busPath_;
    int         i2cFd_{-1};
    std::vector<Device> devices_;

    bool writeRegister(uint8_t addr, uint8_t reg, uint8_t value) noexcept;
    bool readRegisters(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) noexcept;
};

} // namespace dynamichardware::i2c
