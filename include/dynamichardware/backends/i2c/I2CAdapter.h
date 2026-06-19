#pragma once
#include "dynamichardware/pdo/IDiscoveryBackend.h"
#include "dynamichardware/pdo/IRTBackend.h"
#include "dynamichardware/pdo/HardwareCatalog.h"

#include <string>
#include <vector>
#include <cstdint>

// ============================================================================
// I2CAdapter — I2C backend adapter for the PDO system.
//
// Implements both IDiscoveryBackend and IRTBackend so I2C sensors appear as 
// regular PDO entries to the RT cycle.  Each I2C device gets its own PDO with 
// entries for each sensor axis or channel.
//
// Two-phase lifecycle (ISP split):
//
// DISCOVERY PHASE (IDiscoveryBackend — transient):
//   1. Construct with bus path.
//   2. App calls registerDevice() for desired sensors.
//   3. discover() — validates bus access, populates catalog from registered devices.
//   4. Discovery interface can be destroyed after consumer configuration phase.
//
// RT SETUP + CYCLE (IRTBackend — persistent through freeze):
//   5. buildRT() — opens I2C bus handle, constructs PDO structure from devices.
//   6. activate() — no-op (bus already opened during buildRT).
//   7. onBeforeReadInputs()/onAfterWriteOutputs() — per-cycle read/write hooks.
//
// Phase 1: Stub implementation (returns zeros).  Real I2C communication
// will be implemented when hardware is available.
// ============================================================================

namespace dynamichardware::i2c {

struct I2CDevice {
    uint8_t  bus{0};
    uint8_t  address{0};
    std::string name;
    std::vector<dynamichardware::pdo::PDOEntry*> entries;  // entries owned by this device
};

// Inherits both discovery (transient) and RT lifecycle (persistent) interfaces.
class I2CAdapter final
    : public dynamichardware::pdo::IDiscoveryBackend,
      public dynamichardware::pdo::IRTBackend {
public:
    I2CAdapter(std::string busPath);
    // setCatalog inherited from IDiscoveryBackend.
    ~I2CAdapter() override = default;

    // --- IDiscoveryBackend implementation -----------------------------------
    [[nodiscard]] bool discover() override;

    // --- IRTBackend implementation ------------------------------------------
    [[nodiscard]] bool buildRT() override;
    // activate() uses default no-op from IRTBackend.
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

    /// Register an I2C device and create PDO entries for its channels.
    /// @param deviceAddr   I2C bus address (7-bit)
    /// @param name         Device name (e.g. "MPU6050")
    /// @param entryTypes   Vector of EntryType values for each channel
    /// @return             Index of the created PDO in pdos_
    int registerDevice(uint8_t deviceAddr,
                       std::string name,
                       std::vector<dynamichardware::pdo::EntryType> entryTypes);

private:
    std::string busPath_;
    // catalog_ inherited from IDiscoveryBackend for discovery phase.
    // Note: private members below store OS-level resources (fd, device array) that have
    // no home in generic PDOEntry — necessary implementation detail behind abstract interface.
    int i2cFd_{-1};
    std::vector<I2CDevice> devices_;

    // Phase 1: stub helpers (will use real I2C sysfs/dev interface)
    bool writeRegister(uint8_t addr, uint8_t reg, uint8_t value) noexcept;
    bool readRegisters(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) noexcept;
};

} // namespace dynamichardware::i2c
