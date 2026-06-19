#pragma once
#include "dynamichardware/pdo/IHardwareAdapter.h"
#include "dynamichardware/pdo/HardwareCatalog.h"

#include <string>
#include <vector>
#include <cstdint>

// ============================================================================
// SPIAdapter — SPI backend adapter for the PDO system.
//
// Implements IHardwareAdapter so SPI sensors appear as regular PDO entries
// to the RT cycle.  Each SPI device gets its own PDO with entries for each
// sensor axis or channel.
//
// Lifecycle:
//   1. Construct with bus path and sensor catalog reference.
//   2. Call registerDevice() for each SPI sensor (init time).
//   3. Call initialize() — opens SPI bus, validates devices, creates PDOs.
//   4. RT cycle: onBeforeReadInputs() reads all devices into PDO images.
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

class SPIAdapter final : public dynamichardware::pdo::IHardwareAdapter {
public:
    SPIAdapter(std::string busPath);
    void setCatalog(dynamichardware::pdo::HardwareCatalog* catalog) noexcept { catalog_ = catalog; }
    ~SPIAdapter() override = default;

    bool initialize() override;
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
    dynamichardware::pdo::HardwareCatalog* catalog_{nullptr};
    int spiFd_{-1};
    std::vector<SPIDevice> devices_;

    // Phase 1: stub helpers (will use real SPI ioctl interface)
    bool transfer(uint8_t cs, const uint8_t* tx, uint8_t* rx, size_t len) noexcept;
};

} // namespace dynamichardware::spi
