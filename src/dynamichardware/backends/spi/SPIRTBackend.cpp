#include "dynamichardware/backends/spi/SPIRTBackend.h"

#include <cstdio>
#include <string>

namespace dynamichardware::spi {

// Default constructor for self-registration via BackendRegistry.
SPIRTBackend::SPIRTBackend()
    : busPath_("/dev/spidev0.0") {}  // Default fallback path

SPIRTBackend::SPIRTBackend(std::string busPath)
    : busPath_(std::move(busPath)) {}

void SPIRTBackend::configure(const std::unordered_map<std::string, std::string>& config)
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

// ---------------------------------------------------------------------------
// IRuntimeAdapter::setCatalog()
// ---------------------------------------------------------------------------
void SPIRTBackend::setCatalog(const dynamichardware::dhdo::HardwareCatalog* catalog) noexcept
{
    catalog_ = catalog;
}

// ---------------------------------------------------------------------------
// IDHDOBuilder::build(channels) — populate devices from mapped channels.
// Stub implementation: collects channel info but doesn't access real hardware yet.
// ---------------------------------------------------------------------------
bool SPIRTBackend::build(const std::vector<dynamichardware::dhdo::MappedChannel>& channels)
{
    if (!channels.empty()) {
        Device device{};
        for (const auto& ch : channels) {
            dynamichardware::dhdo::DHDOEntry entry{};
            entry.type = ch.type;
            entry.uuid = ch.uuid;
            device.entries.push_back(&entry);
        }
        device.name = "SPI-Device";
        devices_.push_back(std::move(device));
    }

    return buildRT();  // Delegate to existing PDO construction logic.
}

bool SPIRTBackend::transfer(uint8_t cs, const uint8_t* tx, uint8_t* rx, size_t len) noexcept {
    (void)cs; (void)tx; (void)rx; (void)len; return false;
}

} // namespace dynamichardware::spi
