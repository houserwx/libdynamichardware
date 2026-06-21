#pragma once
#include "dynamichardware/dhdo/IRuntimeAdapter.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"

#include <string>
#include <vector>
#include <cstdint>

namespace dynamichardware::i2c {

/// ---- I2CRTBackend --------------------------------------------------------
/// Real-time I2C process-data backend.
///
/// Fully independent of discovery: acquires its own resources in buildRT(),
/// registers devices via registerDevice() (called by DynamicHardwareContext), builds PDOs from scratch.
class I2CRTBackend final
    : public dynamichardware::dhdo::IRuntimeAdapter {
public:
    explicit I2CRTBackend(std::string busPath);
    ~I2CRTBackend() override = default;

    // --- RT lifecycle methods ----------------------------------------------
    [[nodiscard]] bool buildRT();
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

    // --- Builder interface -------------------------------------------------
    void setCatalog(const dynamichardware::dhdo::HardwareCatalog* catalog) noexcept;
    [[nodiscard]] bool build(const std::vector<dynamichardware::dhdo::MappedChannel>& channels) override;

 private:
    const dynamichardware::dhdo::HardwareCatalog* catalog_{nullptr};
    struct Device {
        uint8_t  bus{0};
        uint8_t  address{0};
        std::string name;
        std::vector<dynamichardware::dhdo::DHDOEntry*> entries;
    };

    std::string busPath_;
    int         i2cFd_{-1};
    std::vector<Device> devices_;

    bool writeRegister(uint8_t addr, uint8_t reg, uint8_t value) noexcept;
    bool readRegisters(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) noexcept;
};

} // namespace dynamichardware::i2c
