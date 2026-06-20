// ============================================================================
// DynamicHardwareContextFactory — one-shot hardware scan + catalog update.
// Owns: HardwareCatalog, backend enable flags, paths.
// Does NOT own: RT backends or DHDOEntry objects (those live in ContextObject).
// ============================================================================

#include "dynamichardware/DynamicHardwareContextFactory.h"

#include "dynamichardware/DynamicHardwareContextObject.h"

#include "dynamichardware/dhdo/HardwareRegistry.h"

#include "dynamichardware/backends/ethercat/EthercatDiscovery.h"
#include "dynamichardware/backends/ethercat/EthercatRTBackend.h"
#include "dynamichardware/backends/gpio/GPIODiscovery.h"
#include "dynamichardware/backends/gpio/GPIORTBackend.h"
#include "dynamichardware/backends/i2c/I2CDiscovery.h"
#include "dynamichardware/backends/i2c/I2CRTBackend.h"
#include "dynamichardware/backends/spi/SPIDiscovery.h"
#include "dynamichardware/backends/spi/SPIRTBackend.h"
#include "dynamichardware/backends/simulated/SimulatedDiscovery.h"
#include "dynamichardware/backends/simulated/SimulatedRTBackend.h"

#include <cstdio>

namespace dynamichardware {

// ============================================================================
// Fluent configuration
// ============================================================================

DynamicHardwareContextFactory& DynamicHardwareContextFactory::catalogPath(std::string path)
{
    state_.catalogPath = std::move(path);
    return *this;
}

DynamicHardwareContextFactory& DynamicHardwareContextFactory::withEthercat(uint32_t cycleNs)
{
    state_.enableEthercat = true;
    state_.ethercatCycleNs = cycleNs;
    return *this;
}

DynamicHardwareContextFactory& DynamicHardwareContextFactory::withGPIO()
{
    state_.enableGPIO = true;
    return *this;
}

DynamicHardwareContextFactory& DynamicHardwareContextFactory::withI2C(std::string busPath)
{
    state_.enableI2C = true;
    state_.i2cBusPath = std::move(busPath);
    return *this;
}

DynamicHardwareContextFactory& DynamicHardwareContextFactory::withSPI(std::string busPath)
{
    state_.enableSPI = true;
    state_.spiBusPath = std::move(busPath);
    return *this;
}

DynamicHardwareContextFactory& DynamicHardwareContextFactory::withSimulation(
        std::optional<std::string> definitionsPath)
{
    state_.enableSimulation = true;
    state_.simDefinitionsPath = std::move(definitionsPath);
    return *this;
}

// ============================================================================
// Catalog access
// ============================================================================

const dhdo::HardwareCatalog& DynamicHardwareContextFactory::catalog() const noexcept { return catalog_; }
dhdo::HardwareCatalog& DynamicHardwareContextFactory::catalog()       noexcept { return catalog_; }

// ============================================================================
// Discovery phase — scan physical hardware, update catalog, purge stale keys
// ============================================================================

bool DynamicHardwareContextFactory::discover()
{
    // Load existing catalog (preserves UUIDs across restarts)
    catalog_.load(state_.catalogPath);

    // Begin discovery cycle — any entry NOT re-registered by the end gets purged.
    catalog_.beginDiscovery();

    bool anyDiscovered = false;

    // EtherCAT — one-shot scan via temporary object (all IgH resources released on scope exit)
    if (state_.enableEthercat) {
        ethercat::EthercatDiscovery discovery(state_.ethercatCycleNs);
        discovery.setCatalog(&catalog_);
        std::printf("[Discover] Scanning EtherCAT devices...\n");
        if (discovery.discover()) {
            anyDiscovered = true;
        } else {
            std::printf("[Discover] EtherCAT discovery failed (no hardware or stub mode)\n");
        }
    }

    // GPIO — one-shot scan via temporary object
    if (state_.enableGPIO) {
        gpio::GPIODiscovery gpioDiscovery;
        gpioDiscovery.setCatalog(&catalog_);
        auto variant = gpioDiscovery.boardVariant();
        if (variant == gpio::BoardVariant::UNKNOWN) {
            std::printf("[Discover] Skipping GPIO backend — not a recognized embedded platform\n");
        } else {
            std::printf("[Discover] Scanning GPIO lines (%s)...\n",
                        gpio::boardVariantName(variant).c_str());
            if (!gpioDiscovery.discover()) {
                std::printf("[Discover] GPIO discovery failed\n");
            } else {
                anyDiscovered = true;
            }
        }
    }

    // I2C — one-shot scan, then destroy
    if (state_.enableI2C) {
        i2c::I2CDiscovery i2cDiscovery(state_.i2cBusPath);
        i2cDiscovery.setCatalog(&catalog_);
        std::printf("[Discover] Scanning I2C devices...\n");
        if (!i2cDiscovery.discover()) {
            std::printf("[Discover] I2C discovery failed (stub mode)\n");
        } else {
            anyDiscovered = true;
        }
    }

    // SPI — one-shot scan, then destroy
    if (state_.enableSPI) {
        spi::SPIDiscovery spiDiscovery(state_.spiBusPath);
        spiDiscovery.setCatalog(&catalog_);
        std::printf("[Discover] Scanning SPI devices...\n");
        if (!spiDiscovery.discover()) {
            std::printf("[Discover] SPI discovery failed (stub mode)\n");
        } else {
            anyDiscovered = true;
        }
    }

    // Simulated — one-shot scan, then destroy
    if (state_.enableSimulation) {
        simulated::SimulatedDiscovery simDiscovery(state_.simDefinitionsPath.value_or(""));
        simDiscovery.setCatalog(&catalog_);
        std::printf("[Discover] Scanning Simulated entries...\n");
        if (!simDiscovery.discover()) {
            std::printf("[Discover] Simulated discovery failed\n");
        } else {
            anyDiscovered = true;
        }
    }

    // Purge entries that were NOT re-registered during this discovery cycle.
    auto purged = catalog_.purgeStaleEntries();
    if (purged > 0) {
        std::printf("[Discover] Removed %zu stale entries from catalog\n", purged);
    }

    // Save updated catalog after discovery phase.
    if (!state_.catalogPath.empty()) {
        catalog_.save(state_.catalogPath);
    }

    size_t totalEntries = catalog_.entries().size();
    std::printf("[Discover] Complete: %zu catalog entries (%s)\n",
                totalEntries, anyDiscovered ? "new channels found" : "no new channels");

    return anyDiscovered || totalEntries > 0;
}

// ============================================================================
// Build RT context — create backends, register PDOs, move into ContextObject
// ============================================================================

std::unique_ptr<DynamicHardwareContextObject> DynamicHardwareContextFactory::buildRT()
{
    // Create RT backend objects and build them against the discovered catalog.
    dhdo::HardwareRegistry registry;

    if (state_.enableEthercat) {
        auto rtBackend = std::make_unique<ethercat::EthercatRTBackend>(state_.ethercatCycleNs);
        std::printf("[RtBuild] Building EtherCAT backend...\n");
        if (rtBackend->buildRT()) {
            registry.addBackend(std::move(rtBackend));
        } else {
            std::printf("[RtBuild] EtherCAT RT setup failed\n");
        }
    }

    if (state_.enableGPIO) {
        auto rtBackend = std::make_unique<gpio::GPIORTBackend>();
        auto variant = rtBackend->boardVariant();
        if (variant != gpio::BoardVariant::UNKNOWN) {
            std::printf("[RtBuild] Building GPIO backend (%s)...\n",
                        gpio::boardVariantName(variant).c_str());
            if (rtBackend->buildRT()) {
                registry.addBackend(std::move(rtBackend));
            } else {
                std::printf("[RtBuild] GPIO RT setup failed\n");
            }
        }
    }

    if (state_.enableI2C) {
        auto rtBackend = std::make_unique<i2c::I2CRTBackend>(state_.i2cBusPath);
        std::printf("[RtBuild] Building I2C backend...\n");
        if (rtBackend->buildRT()) {
            registry.addBackend(std::move(rtBackend));
        } else {
            std::printf("[RtBuild] I2C RT setup failed\n");
        }
    }

    if (state_.enableSPI) {
        auto rtBackend = std::make_unique<spi::SPIRTBackend>(state_.spiBusPath);
        std::printf("[RtBuild] Building SPI backend...\n");
        if (rtBackend->buildRT()) {
            registry.addBackend(std::move(rtBackend));
        } else {
            std::printf("[RtBuild] SPI RT setup failed\n");
        }
    }

    if (state_.enableSimulation) {
        auto rtBackend = std::make_unique<simulated::SimulatedRTBackend>(state_.simDefinitionsPath.value_or(""));
        std::printf("[RtBuild] Building Simulated backend...\n");
        if (rtBackend->buildRT()) {
            registry.addBackend(std::move(rtBackend));
        } else {
            std::printf("[RtBuild] Simulated RT setup failed\n");
        }
    }

    // Build name → UUID mapping from catalog (.uuid == .key now)
    DynamicHardwareContextObject::Impl ctxImpl{std::move(registry), std::move(catalog_), {}};
    for (const auto& entry : ctxImpl.catalog.entries()) {
        if (!entry.name.empty()) {
            ctxImpl.nameToUuid[entry.name] = entry.uuid;
        }
    }

    std::printf("[RtBuild] Complete: %zu backends, %zu entries\n",
                ctxImpl.registry.backendCount(), ctxImpl.catalog.entries().size());

    return std::unique_ptr<DynamicHardwareContextObject>(
        new DynamicHardwareContextObject(std::move(ctxImpl)));
}

} // namespace dynamichardware
