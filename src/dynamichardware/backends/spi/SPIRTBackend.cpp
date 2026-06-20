#include "dynamichardware/backends/spi/SPIRTBackend.h"

#include <cstdio>

namespace dynamichardware::spi {

SPIRTBackend::SPIRTBackend(std::string busPath)
    : busPath_(std::move(busPath)) {}

// ---------------------------------------------------------------------------
// buildRT() — construct PDOs from pre-registered devices (stub mode).
// Fully self-contained: does NOT depend on any prior discovery scan.
// ---------------------------------------------------------------------------
bool SPIRTBackend::buildRT()
{
    if (devices_.empty()) {
        std::fprintf(stderr, "[SPI] No devices to build RT PDOs for\n");
        return false;
    }

    spiFd_ = -1; // Stub placeholder.

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

    std::printf("[SPI] Built RT: %zu PDOs\n", dhdos_.size());
    return true;
}

void SPIRTBackend::onBeforeReadInputs()  noexcept {}
void SPIRTBackend::onAfterWriteOutputs() noexcept {}

int SPIRTBackend::registerDevice(uint8_t chipSelect, std::string name,
                                  std::vector<dynamichardware::dhdo::EntryType> entryTypes)
{
    Device device{};
    device.chipSelect = chipSelect;
    device.name       = std::move(name);

    for (const auto& type : entryTypes) {
        dynamichardware::dhdo::DHDOEntry entry{};
        entry.type   = type;
        entry.uuid   = "spi:" + std::to_string(chipSelect) + ":" + std::to_string(device.entries.size());
        device.entries.push_back(&entry);
    }

    const int idx = static_cast<int>(devices_.size());
    devices_.push_back(std::move(device));
    return idx;
}

bool SPIRTBackend::transfer(uint8_t cs, const uint8_t* tx, uint8_t* rx, size_t len) noexcept {
    (void)cs; (void)tx; (void)rx; (void)len; return false;
}

} // namespace dynamichardware::spi
