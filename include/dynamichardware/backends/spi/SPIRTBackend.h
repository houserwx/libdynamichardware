#pragma once
#include "dynamichardware/dhdo/IRuntimeAdapter.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"

#include <string>
#include <vector>
#include <cstdint>

namespace dynamichardware::spi {

/// ---- SPIRTBackend --------------------------------------------------------
/// Real-time SPI process-data backend.
/// Fully independent of discovery — acquires own resources in buildRT().
class SPIRTBackend final
    : public dynamichardware::dhdo::IRuntimeAdapter {
public:
    explicit SPIRTBackend(std::string busPath);
    ~SPIRTBackend() override = default;

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
        uint8_t   bus{0};
        uint8_t   chipSelect{0};
        uint32_t  maxSpeedHz{1'000'000};
        uint8_t   mode{0};
        std::string name;
        std::vector<dynamichardware::dhdo::DHDOEntry*> entries;
    };

    std::string       busPath_;
    int               spiFd_{-1}; // Stub placeholder for /dev/spidevX.Y handle
    std::vector<Device> devices_;

    bool transfer(uint8_t cs, const uint8_t* tx, uint8_t* rx, size_t len) noexcept;
};

} // namespace dynamichardware::spi
