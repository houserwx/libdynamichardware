#pragma once
#include "backends/pdo/IHardwareAdapter.h"
#include "backends/ethercat/HardwareCatalog.h"

#include <string>
#include <vector>
#include <cstdint>

// ============================================================================
// I2CAdapter — I2C backend adapter for the PDO system.
//
// Implements IHardwareAdapter so I2C sensors appear as regular PDO entries
// to the RT cycle.  Each I2C device gets its own PDO with entries for each
// sensor axis or channel.
//
// Lifecycle:
//   1. Construct with bus path and sensor catalog reference.
//   2. Call registerDevice() for each I2C sensor (init time).
//   3. Call initialize() — opens I2C bus, validates devices, creates PDOs.
//   4. RT cycle: onBeforeReadInputs() reads all devices into PDO images.
//
// Phase 1: Stub implementation (returns zeros).  Real I2C communication
// will be implemented when hardware is available.
// ============================================================================

namespace fc::i2c {

struct I2CDevice {
    uint8_t  bus{0};
    uint8_t  address{0};
    std::string name;
    std::vector<fc::pdo::PDOEntry*> entries;  // entries owned by this device
};

class I2CAdapter final : public fc::pdo::IHardwareAdapter {
public:
    I2CAdapter(std::string busPath);
    void setCatalog(fc::pdo::HardwareCatalog* catalog) noexcept { catalog_ = catalog; }
    ~I2CAdapter() override = default;

    bool initialize() override;
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

    /// Register an I2C device and create PDO entries for its channels.
    /// @param deviceAddr   I2C bus address (7-bit)
    /// @param name         Device name (e.g. "MPU6050")
    /// @param entryTypes   Vector of EntryType values for each channel
    /// @return             Index of the created PDO in pdos_
    int registerDevice(uint8_t deviceAddr,
                       std::string name,
                       std::vector<fc::pdo::EntryType> entryTypes);

private:
    std::string busPath_;
    fc::pdo::HardwareCatalog* catalog_{nullptr};
    int i2cFd_{-1};
    std::vector<I2CDevice> devices_;

    // Phase 1: stub helpers (will use real I2C sysfs/dev interface)
    bool writeRegister(uint8_t addr, uint8_t reg, uint8_t value) noexcept;
    bool readRegisters(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) noexcept;
};

} // namespace fc::i2c
