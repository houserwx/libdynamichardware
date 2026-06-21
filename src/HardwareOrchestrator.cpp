// ============================================================================
// HardwareOrchestrator — internal phase coordination + backend iteration.
// 
// Extracted from DynamicHardwareContextFactory to fix Issue E (SRP violation).
// Uses new interfaces exclusively: scan() for discovery, build(channels) for RT.
// Old factory still works alongside; consumers migrate gradually in Phase 8.
// ============================================================================

#include "dynamichardware/HardwareOrchestrator.h"

#include "dynamichardware/DynamicHardwareContextObject.h"
#include "dynamichardware/dhdo/HardwareRegistry.h"
#include "dynamichardware/dhdo/DHDOFactory.h"

// Discovery backends
#include "dynamichardware/backends/ethercat/EthercatDiscovery.h"
#include "dynamichardware/backends/gpio/GPIODiscovery.h"
#include "dynamichardware/backends/i2c/I2CDiscovery.h"
#include "dynamichardware/backends/spi/SPIDiscovery.h"
#include "dynamichardware/backends/simulated/SimulatedDiscovery.h"

// Runtime backends
#include "dynamichardware/backends/ethercat/EthercatRTBackend.h"
#include "dynamichardware/backends/gpio/GPIORTBackend.h"
#include "dynamichardware/backends/i2c/I2CRTBackend.h"
#include "dynamichardware/backends/spi/SPIRTBackend.h"
#include "dynamichardware/backends/simulated/SimulatedRTBackend.h"

// For mapping persistence — need nlohmann::json for save/load mappings
#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <string>

namespace dynamichardware {

HardwareOrchestrator::HardwareOrchestrator(OrchestratorState state)
    : state_(std::move(state)) {}

void HardwareOrchestrator::addChannelDefinition(const std::string& keyOrUuid, 
                                                 dhdo::EntryType type,
                                                 const std::string& friendlyName) {
    ChannelDefinition def{};
    def.keyOrUuid    = keyOrUuid;
    def.type         = type;
    def.friendlyName = friendlyName;
    channelDefs_.push_back(std::move(def));
}

const dhdo::HardwareCatalog& HardwareOrchestrator::catalog() const noexcept { return catalog_; }
dhdo::HardwareCatalog& HardwareOrchestrator::catalog()       noexcept { return catalog_; }

// ============================================================================
// Mapping persistence helpers (extracted from old factory)
// Format: { "mappings": [ { "uuid": ..., "type": "BoolOutput", "name": ... }, ... ] }
// ============================================================================

size_t HardwareOrchestrator::loadMappings() {
    if (state_.mappingPath.empty()) return 0;

    std::ifstream f(state_.mappingPath);
    if (!f) return 0;

    try {
        nlohmann::json j;
        f >> j;

        size_t count = 0;
        for (const auto& m : j.value("mappings", nlohmann::json::array())) {
            ChannelDefinition def{};
            def.keyOrUuid    = m.value("uuid", "");
            def.friendlyName = m.value("name", "");

            std::string typeStr = m.value("type", "BoolInput");
            def.type = dhdo::DHDOFactory::stringToEntryType(typeStr);

            channelDefs_.push_back(std::move(def));
            ++count;
        }
        std::printf("[Mapping] Loaded %zu mappings from '%s'\n",
                    count, state_.mappingPath.c_str());
        return count;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[Mapping] Parse error loading '%s': %s\n",
                     state_.mappingPath.c_str(), ex.what());
        return 0;
    }
}

void HardwareOrchestrator::saveMappings() {
    if (state_.mappingPath.empty()) return;

    try {
        nlohmann::json j;
        nlohmann::json arr = nlohmann::json::array();

        for (const auto& cdef : channelDefs_) {
            nlohmann::json entry;
            entry["uuid"] = cdef.keyOrUuid;
            entry["type"] = dhdo::DHDOFactory::entryTypeToString(cdef.type);
            if (!cdef.friendlyName.empty()) {
                entry["name"] = cdef.friendlyName;
            }
            arr.push_back(std::move(entry));
        }

        j["mappings"] = arr;

        std::ofstream f(state_.mappingPath);
        if (!f) {
            std::fprintf(stderr, "[Mapping] Cannot open '%s' for writing\n",
                         state_.mappingPath.c_str());
            return;
        }
        f << j.dump(2) << '\n';
        std::printf("[Mapping] Saved %zu mappings to '%s'\n",
                    channelDefs_.size(), state_.mappingPath.c_str());
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[Mapping] Save error: %s\n", ex.what());
    }
}

// ============================================================================
// Discovery phase — scan() pipeline feeds into catalog, then purge stale entries
// ============================================================================

bool HardwareOrchestrator::runDiscoveryScan() {
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

    return anyDiscovered;
}

bool HardwareOrchestrator::discover() {
    // Phase starts at DISCOVERY by default; no advance needed on first call.
    // If called again after reset, ensure we're in DISCOVERY:
    if (!phaseManager_.isAt(config::HardwarePhase::DISCOVERY)) {
        try { (void)phaseManager_.advance(config::HardwarePhase::DISCOVERY); } catch (...) {}
    }

    // Load existing catalog (preserves UUIDs across restarts)
    catalog_.load(state_.catalogPath);

    // Begin discovery cycle — any entry NOT re-registered by the end gets purged.
    catalog_.beginDiscovery();

    bool anyDiscovered = runDiscoveryScan();

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

    // Warn about previously-mapped UUIDs that no longer exist in current hardware scan.
    if (!channelDefs_.empty()) {
        size_t staleCount = 0;
        for (const auto& cdef : channelDefs_) {
            const auto* catEntry = catalog_.findByUuid(cdef.keyOrUuid);

            if (!catEntry) {
                ++staleCount;
                std::fprintf(stderr,
                    "[Mapping] WARNING: Previously-mapped entry '%s' "
                    "(type=%s) no longer exists in discovered hardware!\n",
                    cdef.keyOrUuid.c_str(),
                    dhdo::DHDOFactory::entryTypeToString(cdef.type));
            }
        }
        if (staleCount > 0) {
            std::fprintf(stderr,
                "[Mapping] %zu of %zu mapped entries are STALE — remap required.\n",
                staleCount, channelDefs_.size());
        } else {
            std::printf("[Mapping] All %zu mapped entries still valid.\n", channelDefs_.size());
        }
    }

    // Persist current channel definitions (user's mapped channels).
    saveMappings();

    // Advance to MAPPING phase after discovery completes (if not already there)
    if (!phaseManager_.isAt(config::HardwarePhase::MAPPING)) {
        (void)phaseManager_.advance(config::HardwarePhase::MAPPING);
    }

    return anyDiscovered || totalEntries > 0;
}

// ============================================================================
// Build RT context — create backends using build(channels), move into ContextObject
// ============================================================================

std::unique_ptr<DynamicHardwareContextObject> HardwareOrchestrator::buildRT() {
    // Ensure we've advanced through MAPPING before BUILD_RT
    if (!phaseManager_.isAt(config::HardwarePhase::MAPPING)) {
        try { (void)phaseManager_.advance(config::HardwarePhase::MAPPING); } catch (...) {}
    }
    if (!phaseManager_.isAt(config::HardwarePhase::BUILD_RT)) {
        (void)phaseManager_.advance(config::HardwarePhase::BUILD_RT);
    }

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
            // Collect mapped channels for this backend — no backend-specific data types leak out.
            std::vector<dhdo::MappedChannel> gpioChannels;
            for (const auto& cdef : channelDefs_) {
                const auto* catEntry = catalog_.findByUuid(cdef.keyOrUuid);
                if (!catEntry || catEntry->backend != dhdo::BackendType::GPIO) continue;

                gpioChannels.push_back({cdef.keyOrUuid, cdef.type, ""});
            }

            // Backend resolves its own UUIDs internally via catalog reference.
            rtBackend->setCatalog(&catalog_);

            std::printf("[RtBuild] Building GPIO backend (%s)...\n",
                        gpio::boardVariantName(variant).c_str());
            if (rtBackend->build(gpioChannels)) {
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
        auto rtBackend = std::make_unique<simulated::SimulatedRTBackend>(
                state_.simDefinitionsPath.value_or(""));
        std::printf("[RtBuild] Building Simulated backend...\n");
        if (rtBackend->buildRT()) {
            registry.addBackend(std::move(rtBackend));
        } else {
            std::printf("[RtBuild] Simulated RT setup failed\n");
        }
    }

    // Build name → UUID mapping from catalog using deterministic hash-based UUIDs
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
