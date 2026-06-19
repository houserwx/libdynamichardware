#pragma once
#include "dynamichardware/pdo/IRTBackend.h"

#include <string>
#include <vector>
#include <cstdint>

namespace dynamichardware::spi {

/// ---- SPIRTBackend --------------------------------------------------------
/// Real-time SPI process-data backend. Implements IRTBackend.
/// Fully independent of discovery — acquires own resources in buildRT().
class SPIRTBackend final : public dynamichardware::pdo::IRTBackend {
public:
    explicit SPIRTBackend(std::string busPath);
    ~SPIRTBackend() override = default;

    [[nodiscard]] bool buildRT() override;
    void onBeforeReadInputs()  noexcept override;
    void onAfterWriteOutputs() noexcept override;

    /// Register an SPI device before calling buildRT(). Returns device index.
    /// Called by DynamicHardwareContext during consumer configuration phase — NOT by external consumers directly.
    int registerDevice(uint8_t chipSelect, std::string name,
                       std::vector<dynamichardware::pdo::EntryType> entryTypes);

private:
    struct Device {
        uint8_t   bus{0};
        uint8_t   chipSelect{0};
        uint32_t  maxSpeedHz{1'000'000};
        uint8_t   mode{0};
        std::string name;
        std::vector<dynamichardware::pdo::PDOEntry*> entries;
    };

    std::string       busPath_;
    int               spiFd_{-1}; // Stub placeholder for /dev/spidevX.Y handle
    std::vector<Device> devices_;

    bool transfer(uint8_t cs, const uint8_t* tx, uint8_t* rx, size_t len) noexcept;
};

} // namespace dynamichardware::spi
