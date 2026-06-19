#include "dynamichardware/DynamicHardwareContext.h"

#include "dynamichardware/pdo/HardwareRegistry.h"
#include "dynamichardware/pdo/HardwareCatalog.h"
#include "dynamichardware/pdo/PDOFactory.h"

#include "dynamichardware/backends/ethercat/EthercatDiscovery.h"
#include "dynamichardware/backends/ethercat/EthercatRTBackend.h"
#include "dynamichardware/backends/gpio/GPIOAdapter.h"
#include "dynamichardware/backends/i2c/I2CAdapter.h"
#include "dynamichardware/backends/spi/SPIAdapter.h"
#include "dynamichardware/backends/simulated/SimulatedAdapter.h"

#include <cmath>
#include <climits>
#include <cstdio>
#include <fstream>
#include <memory>
#include <unordered_map>

namespace dynamichardware {

// ============================================================================
// Internal implementation
// ============================================================================
struct DynamicHardwareContext::Impl {
    pdo::HardwareRegistry registry;
    pdo::HardwareCatalog  catalog;

    // Name → UUID mapping (built from catalog)
    std::unordered_map<std::string, std::string> nameToUuid;

    // Owning RT backend handle — moved into registry during build()
    std::unique_ptr<ethercat::EthercatRTBackend>  ethercatRTBackend;
    std::unique_ptr<gpio::GPIOAdapter>          gpioAdapter;
    std::unique_ptr<i2c::I2CAdapter>            i2cAdapter;
    std::unique_ptr<spi::SPIAdapter>            spiAdapter;
    std::unique_ptr<simulated::SimulatedAdapter> simAdapter;

    // Raw pointer to GPIO adapter for post-move access (deferredActivate on freeze)
    // Survives unique_ptr move into registry.
    gpio::GPIOAdapter* gpioPtr{nullptr};
};

// ============================================================================
// SimulatedDefinitionBuilder implementation
// ============================================================================
SimulatedDefinitionBuilder SimulatedDefinitionBuilder::create()
{
    return SimulatedDefinitionBuilder{};
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::cycleTimeUs(int us)
{
    cycleTimeUs_ = static_cast<uint32_t>(us);
    return *this;
}

// ---- Channel types (mirror EntryType system) ----

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::boolInput(const std::string& name, const std::string& uuid)
{
    channels_.push_back({name, uuid, "BoolInput", {}});
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::boolOutput(const std::string& name, const std::string& uuid)
{
    channels_.push_back({name, uuid, "BoolOutput", {}});
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::int8Input(const std::string& name, const std::string& uuid)
{
    channels_.push_back({name, uuid, "Int8Input", {}});
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::int16Input(const std::string& name, const std::string& uuid)
{
    channels_.push_back({name, uuid, "Int16Input", {}});
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::int32Input(const std::string& name, const std::string& uuid)
{
    channels_.push_back({name, uuid, "Int32Input", {}});
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::int16Output(const std::string& name, const std::string& uuid)
{
    channels_.push_back({name, uuid, "Int16Output", {}});
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::floatInput(const std::string& name, const std::string& uuid)
{
    channels_.push_back({name, uuid, "FloatInput", {}});
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::floatOutput(const std::string& name, const std::string& uuid)
{
    channels_.push_back({name, uuid, "FloatOutput", {}});
    return *this;
}

// ---- Simulation parameters (applied to the last-added channel) ----

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::togglePeriodMs(uint32_t v)
{
    lastChannel().sim.togglePeriodMs = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::dutyCyclePercent(float v)
{
    lastChannel().sim.dutyCyclePercent = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::incrementPerCycle(int32_t v)
{
    lastChannel().sim.incrementPerCycle = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::minValue(int64_t v)
{
    lastChannel().sim.minValue = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::maxValue(int64_t v)
{
    lastChannel().sim.maxValue = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::amplitude(float v)
{
    lastChannel().sim.amplitude = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::frequencyHz(float v)
{
    lastChannel().sim.frequencyHz = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::offset(float v)
{
    lastChannel().sim.offset = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::pulseMs(uint32_t v)
{
    lastChannel().sim.pulseMs = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::debounceMs(uint32_t v)
{
    lastChannel().sim.debounceMs = v;
    return *this;
}

std::string SimulatedDefinitionBuilder::toJson() const
{
    using json = nlohmann::json;
    json defs = {{"cycleTimeUs", cycleTimeUs_}, {"channels", json::array()}};

    for (const auto& ch : channels_) {
        json obj;
        obj["name"] = ch.name;
        obj["uuid"] = ch.uuid;
        obj["channelType"] = ch.channelType;

        // Only include sim params that are non-default
        json sim;
        if (ch.sim.togglePeriodMs > 0)       sim["togglePeriodMs"]     = ch.sim.togglePeriodMs;
        if (std::abs(ch.sim.dutyCyclePercent - 50.0f) > 0.01f)  sim["dutyCyclePercent"] = ch.sim.dutyCyclePercent;
        if (ch.sim.incrementPerCycle != 1)   sim["incrementPerCycle"]  = ch.sim.incrementPerCycle;
        if (ch.sim.minValue != INT64_MIN)    sim["minValue"]           = ch.sim.minValue;
        if (ch.sim.maxValue != INT64_MAX)    sim["maxValue"]           = ch.sim.maxValue;
        if (std::abs(ch.sim.amplitude - 1.0f) > 0.01f)  sim["amplitude"]      = ch.sim.amplitude;
        if (std::abs(ch.sim.frequencyHz - 1.0f) > 0.01f) sim["frequencyHz"]    = ch.sim.frequencyHz;
        if (ch.sim.offset != 0.0f)           sim["offset"]             = ch.sim.offset;
        if (ch.sim.pulseMs > 0)              sim["pulseMs"]            = ch.sim.pulseMs;
        if (ch.sim.debounceMs > 0)           sim["debounceMs"]         = ch.sim.debounceMs;
        if (!sim.empty()) obj["sim"] = sim;

        defs["channels"].push_back(obj);
    }

    return defs.dump(2);
}

bool SimulatedDefinitionBuilder::save(const std::string& path) const
{
    std::ofstream out(path);
    if (!out) {
        std::fprintf(stderr, "[SimulatedDefinitionBuilder] Cannot open '%s' for writing\n", path.c_str());
        return false;
    }
    out << toJson() << std::endl;
    out.close();

    std::printf("[SimulatedDefinitionBuilder] Saved %zu channel definitions to '%s'\n",
                channels_.size(), path.c_str());
    return true;
}

SimulatedDefinitionBuilder::Channel& SimulatedDefinitionBuilder::lastChannel()
{
    if (channels_.empty()) {
        std::fprintf(stderr, "[SimulatedDefinitionBuilder] No channel added yet — add a channel first\n");
        static Channel dummy;
        return dummy;
    }
    return channels_.back();
}

// ============================================================================
// Builder
// ============================================================================
DynamicHardwareContextBuilder& DynamicHardwareContextBuilder::catalogPath(std::string path)
{
    state_.catalogPath = std::move(path);
    return *this;
}

DynamicHardwareContextBuilder& DynamicHardwareContextBuilder::configPath(std::string path)
{
    state_.configPath = std::move(path);
    return *this;
}

DynamicHardwareContextBuilder& DynamicHardwareContextBuilder::withEthercat(uint32_t cycleNs)
{
    state_.enableEthercat = true;
    state_.ethercatCycleNs = cycleNs;
    return *this;
}

DynamicHardwareContextBuilder& DynamicHardwareContextBuilder::withGPIO()
{
    state_.enableGPIO = true;
    return *this;
}

DynamicHardwareContextBuilder& DynamicHardwareContextBuilder::withI2C(std::string busPath)
{
    state_.enableI2C = true;
    state_.i2cBusPath = std::move(busPath);
    return *this;
}

DynamicHardwareContextBuilder& DynamicHardwareContextBuilder::withSPI(std::string busPath)
{
    state_.enableSPI = true;
    state_.spiBusPath = std::move(busPath);
    return *this;
}

DynamicHardwareContextBuilder& DynamicHardwareContextBuilder::withSimulation(std::optional<std::string> definitionsPath)
{
    state_.enableSimulation = true;
    state_.simDefinitionsPath = std::move(definitionsPath);
    return *this;
}

std::shared_ptr<DynamicHardwareContext> DynamicHardwareContextBuilder::build()
{
    auto ctx = std::shared_ptr<DynamicHardwareContext>(
        new DynamicHardwareContext(std::move(state_)),
        [](DynamicHardwareContext* p) { delete p; }
    );
    return ctx;
}

// ============================================================================
// DynamicHardwareContext
// ============================================================================
DynamicHardwareContextBuilder DynamicHardwareContext::builder()
{
    return DynamicHardwareContextBuilder{};
}

DynamicHardwareContext::DynamicHardwareContext(DynamicHardwareContextBuilder::State&& state)
    : impl_(new Impl{})
    , state_(State::PRE_BUILD)
    , builderState_(std::move(state))
{
}

DynamicHardwareContext::~DynamicHardwareContext()
{
    if (state_ != State::SHUTDOWN) {
        shutdown();
    }
    delete impl_;
}

bool DynamicHardwareContext::build()
{
    if (state_ != State::PRE_BUILD) return false;

    auto& catalog = impl_->catalog;

    // Load existing catalog (preserves UUIDs across restarts)
    catalog.load(builderState_.catalogPath);

    // Create and configure adapters
    if (builderState_.enableEthercat) {
        impl_->ethercatRTBackend = std::make_unique<ethercat::EthercatRTBackend>(builderState_.ethercatCycleNs);
    }

    if (builderState_.enableGPIO) {
        impl_->gpioAdapter = std::make_unique<gpio::GPIOAdapter>();
        impl_->gpioAdapter->setCatalog(&catalog);
    }

    if (builderState_.enableI2C) {
        impl_->i2cAdapter = std::make_unique<i2c::I2CAdapter>(builderState_.i2cBusPath);
        impl_->i2cAdapter->setCatalog(&catalog);
    }

    if (builderState_.enableSPI) {
        impl_->spiAdapter = std::make_unique<spi::SPIAdapter>(builderState_.spiBusPath);
        impl_->spiAdapter->setCatalog(&catalog);
    }

    if (builderState_.enableSimulation) {
        impl_->simAdapter = std::make_unique<simulated::SimulatedAdapter>();
        impl_->simAdapter->setCatalog(&catalog);

        if (builderState_.simDefinitionsPath.has_value()) {
            impl_->simAdapter->loadDefinitions(builderState_.simDefinitionsPath.value());
        }
    }

    // ==================================================================
    // Phase 1 — Discovery: populate catalog from each enabled backend.
    // Backends scan hardware and register entries into the shared catalog.
    // After this phase, consumers can inspect catalog + register lines.
    // ==================================================================
    bool catalogChanged = false;

    if (builderState_.enableEthercat) {
        // Discovery is a one-shot scan — create temporary object, discover, then destroy.
        // All resources (master, domain) are released when the object goes out of scope.
        {
            ethercat::EthercatDiscovery discovery(builderState_.ethercatCycleNs);
            discovery.setCatalog(&catalog);
            std::printf("[Context] Discovering EtherCAT devices...\n");
            if (!discovery.discover()) {
                std::printf("[Context] EtherCAT discovery failed (no hardware or stub mode)\n");
            } else {
                catalogChanged = true;
            }
        } // discovery destroyed here — all IgH resources released
    }

    if (impl_->gpioAdapter) {
        auto variant = impl_->gpioAdapter->boardVariant();
        if (variant == gpio::BoardVariant::UNKNOWN) {
            std::printf("[Context] Skipping GPIO backend — not a recognized embedded platform\n");
        } else {
            std::printf("[Context] Discovering GPIO lines (%s)...\n",
                        gpio::boardVariantName(variant).c_str());
            if (!impl_->gpioAdapter->discover()) {
                std::printf("[Context] GPIO discovery failed\n");
                impl_->gpioAdapter.reset();
            } else {
                catalogChanged = true;
            }
        }
    }

    if (impl_->i2cAdapter) {
        std::printf("[Context] Discovering I2C devices...\n");
        if (!impl_->i2cAdapter->discover()) {
            std::printf("[Context] I2C discovery failed\n");
            impl_->i2cAdapter.reset();
        } else {
            catalogChanged = true;
        }
    }

    if (impl_->spiAdapter) {
        std::printf("[Context] Discovering SPI devices...\n");
        if (!impl_->spiAdapter->discover()) {
            std::printf("[Context] SPI discovery failed\n");
            impl_->spiAdapter.reset();
        } else {
            catalogChanged = true;
        }
    }

    if (impl_->simAdapter) {
        std::printf("[Context] Discovering Simulated entries...\n");
        if (!impl_->simAdapter->discover()) {
            std::printf("[Context] Simulated discovery failed\n");
            impl_->simAdapter.reset();
        } else {
            catalogChanged = true;
        }
    }

    // Save catalog after discovery phase
    if (catalogChanged) {
        catalog.save(builderState_.catalogPath);
    }

    // ==================================================================
    // Phase 2 — RT Setup: build PDO structures and activate backends.
    // Backends that succeed are moved into the HardwareRegistry for RT.
    // ==================================================================
    if (impl_->ethercatRTBackend) {
        std::printf("[Context] Building EtherCAT backend...\n");
        if (impl_->ethercatRTBackend->buildRT()) {
            impl_->registry.addBackend(std::move(impl_->ethercatRTBackend));
        } else {
            std::printf("[Context] EtherCAT RT setup failed (no hardware or stub mode)\n");
        }
    }

    if (impl_->gpioAdapter) {
        auto variant = impl_->gpioAdapter->boardVariant();
        if (variant != gpio::BoardVariant::UNKNOWN) {
            std::printf("[Context] Building GPIO backend (%s)...\n",
                        gpio::boardVariantName(variant).c_str());
            if (impl_->gpioAdapter->buildRT()) {
                // Save raw pointer before moving unique_ptr into registry
                impl_->gpioPtr = impl_->gpioAdapter.get();
                impl_->registry.addBackend(std::move(impl_->gpioAdapter));
            } else {
                std::printf("[Context] GPIO RT setup failed\n");
            }
        }
    }

    if (impl_->i2cAdapter) {
        std::printf("[Context] Building I2C backend...\n");
        if (impl_->i2cAdapter->buildRT()) {
            impl_->registry.addBackend(std::move(impl_->i2cAdapter));
        } else {
            std::printf("[Context] I2C RT setup failed\n");
        }
    }

    if (impl_->spiAdapter) {
        std::printf("[Context] Building SPI backend...\n");
        if (impl_->spiAdapter->buildRT()) {
            impl_->registry.addBackend(std::move(impl_->spiAdapter));
        } else {
            std::printf("[Context] SPI RT setup failed\n");
        }
    }

    if (impl_->simAdapter) {
        std::printf("[Context] Building Simulated backend...\n");
        if (impl_->simAdapter->buildRT()) {
            impl_->registry.addBackend(std::move(impl_->simAdapter));
        } else {
            std::printf("[Context] Simulated RT setup failed\n");
        }
    }

    // Build name → UUID mapping from catalog
    for (const auto& entry : catalog.entries()) {
        if (!entry.name.empty()) {
            impl_->nameToUuid[entry.name] = entry.uuid;
        }
    }

    state_ = State::BUILT;
    std::printf("[Context] Build complete: %zu backends, %zu catalog entries\n",
                backendCount(), catalog.entries().size());
    return true;
}

bool DynamicHardwareContext::freeze()
{
    if (state_ != State::BUILT) return false;

    // GPIO activation now happens inline during buildRT() in phase 2 of build().
    // The activate() wrapper on GPIOAdapter calls deferredActivate() which is a no-op
    // once buildRT has already been called, so we just skip this legacy path entirely.

    impl_->registry.freezeForRt();
    state_ = State::FROZEN;
    std::printf("[Context] Frozen: %zu total PDO entries ready for RT\n", entryCount());
    return true;
}

void DynamicHardwareContext::shutdown()
{
    if (state_ == State::SHUTDOWN) return;
    state_ = State::SHUTDOWN;
    std::printf("[Context] Shutdown\n");
}

// ---- RT cycle ----

void DynamicHardwareContext::readAll() noexcept
{
    impl_->registry.readAll();
}

void DynamicHardwareContext::writeAll() noexcept
{
    impl_->registry.writeAll();
}

// ---- Channel access ----

pdo::PDOEntry* DynamicHardwareContext::lookupByUuid(std::string_view uuid) noexcept
{
    return impl_->registry.lookupByUuid(uuid);
}

pdo::PDOEntry* DynamicHardwareContext::lookupByName(std::string_view name) noexcept
{
    auto it = impl_->nameToUuid.find(std::string{name});
    if (it == impl_->nameToUuid.end()) return nullptr;
    return impl_->registry.lookupByUuid(it->second);
}

const std::vector<pdo::CatalogEntry>& DynamicHardwareContext::catalogEntries() const noexcept
{
    return impl_->catalog.entries();
}

// ---- Internal layer access ----

pdo::HardwareRegistry& DynamicHardwareContext::registry() noexcept
{
    return impl_->registry;
}

pdo::HardwareCatalog& DynamicHardwareContext::catalog() noexcept
{
    return impl_->catalog;
}

// ---- Health monitoring ----

std::size_t DynamicHardwareContext::backendCount() const noexcept
{
    return impl_->registry.backendCount();
}

bool DynamicHardwareContext::allBackendsHealthy() const noexcept
{
    return impl_->registry.allBackendsHealthy();
}

std::size_t DynamicHardwareContext::entryCount() const noexcept
{
    return impl_->registry.entryCount();
}

// ---- Debug ----

void DynamicHardwareContext::printState() const
{
    impl_->registry.printState();
}

} // namespace dynamichardware
