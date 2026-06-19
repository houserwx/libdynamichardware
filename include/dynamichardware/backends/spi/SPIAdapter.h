#pragma once
#include "dynamichardware/pdo/IDiscoveryBackend.h"
#include "dynamichardware/pdo/IRTBackend.h"
#include "dynamichardware/pdo/HardwareCatalog.h"

#include <string>
#include <vector>
#include <cstdint>

// ============================================================================
// SPIAdapter — SPI backend adapter for the PDO system.
//
// Implements both IDiscoveryBackend and IRTBackend so SPI sensors appear as 
// regular PDO entries to the RT cycle.  Each SPI device gets its own PDO with 
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
//   5. buildRT() — opens SPI bus handle, constructs PDO structure from devices.
//   6. activate() — no-op (bus already opened during buildRT).
//   7. onBeforeReadInputs()/onAfterWriteOutputs() — per-cycle read/write hooks.
//
// Phase 1: Stub implementation (returns zeros).  Real SPI communication
// will be implemented when hardware is available.
// ============================================================================

namespace dynamichardware::spi {

struct SPIDevice {
    uint8_t  bus{0};
    uint8_t  chipSelect{0};
    uint32_t maxSpeedHz{1000000};
    uint8_t  mode{0};  // SPI mode (0-3)
    std::string name;
    std::vector<dynamichardware::pdo::PDOEntry*> entries;
};

// Inherits both discovery (transient) and RT lifecycle (persistent) interfaces.
class SPIAdapter final
    : public dynamichardware::pdo::IDiscoveryBackend,
      public dynamichardware::pdo::IRTBackend {
public:
    SPIAdapter(std::string busPath);
    // setCatalog inherited from IDiscoveryBackend.
    ~SPIAdapter() override = default;

    // --- IDiscoveryBackend implementation -----------------------------------
    [[nodiscard]] bool discover() override;

    // --- IRTBackend implementation ------------------------------------------
    [[nodiscard]] bool buildRT() override;
    // activate() uses default no-op from IRTBackend.
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

    /// Register an SPI device and create PDO entries for its channels.
    /// @param chipSelect   SPI chip select line
    /// @param name         Device name (e.g. "ICM20689")
    /// @param entryTypes   Vector of EntryType values for each channel
    /// @return             Index of the created PDO in pdos_
    int registerDevice(uint8_t chipSelect,
                       std::string name,
                       std::vector<dynamichardware::pdo::EntryType> entryTypes);

private:
    std::string busPath_;
    // catalog_ inherited from IDiscoveryBackend for discovery phase.
    // Note: private members below store OS-level resources (fd, device array) that have
    // no home in generic PDOEntry — necessary implementation detail behind abstract interface.
    int spiFd_{-1};
    std::vector<SPIDevice> devices_;

    // Phase 1: stub helpers (will use real SPI ioctl interface)
    bool transfer(uint8_t cs, const uint8_t* tx, uint8_t* rx, size_t len) noexcept;
};

} // namespace dynamichardware::spi
