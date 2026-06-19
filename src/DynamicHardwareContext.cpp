#include "dynamichardware/DynamicHardwareContext.h"

#include "dynamichardware/pdo/HardwareRegistry.h"
#include "dynamichardware/pdo/HardwareCatalog.h"
#include "dynamichardware/pdo/PDOFactory.h"

#include "dynamichardware/backends/ethercat/EthercatAdapter.h"
#include "dynamichardware/backends/gpio/GPIOAdapter.h"
#include "dynamichardware/backends/i2c/I2CAdapter.h"
#include "dynamichardware/backends/spi/SPIAdapter.h"
#include "dynamichardware/backends/simulated/SimulatedAdapter.h"

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

    // Owning adapter handles — moved into registry during build()
    std::unique_ptr<ethercat::EthercatAdapter>  ethercatAdapter;
    std::unique_ptr<gpio::GPIOAdapter>          gpioAdapter;
    std::unique_ptr<i2c::I2CAdapter>            i2cAdapter;
    std::unique_ptr<spi::SPIAdapter>            spiAdapter;
    std::unique_ptr<simulated::SimulatedAdapter> simAdapter;

    // Raw pointer to GPIO adapter for post-move access (deferredActivate on freeze)
    // Survives unique_ptr move into registry.
    gpio::GPIOAdapter* gpioPtr{nullptr};
};
SimulatedDefinitionBuilder SimulatedDefinitionBuilder::create()
{
    return SimulatedDefinitionBuilder{};
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::cycleTimeUs(int us)
{
    cycleTimeUs_ = static_cast<uint32_t>(us);
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::encoder(const std::string& name, const std::string& uuid)
{
    channels_.push_back({name, uuid, "Encoder", {}});
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::digitalInput(const std::string& name, const std::string& uuid)
{
    channels_.push_back({name, uuid, "DigitalInput", {}});
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::digitalOutput(const std::string& name, const std::string& uuid)
{
    channels_.push_back({name, uuid, "DigitalOutput", {}});
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::analogInput(const std::string& name, const std::string& uuid)
{
    channels_.push_back({name, uuid, "AnalogInput", {}});
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::analogOutput(const std::string& name, const std::string& uuid)
{
    channels_.push_back({name, uuid, "AnalogOutput", {}});
    return *this;
}

// ---- Simulation parameters ----

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::rpm(float v)
{
    lastChannel().sim.rpm = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::rollerDiamMm(float v)
{
    lastChannel().sim.rollerDiamMm = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::resolutionPpr(uint32_t v)
{
    lastChannel().sim.resolutionPpr = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::quadrature(bool v)
{
    lastChannel().sim.quadrature = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::partsPerMin(float v)
{
    lastChannel().sim.partsPerMin = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::partWidthMm(float v)
{
    lastChannel().sim.partWidthMm = v;
    return *this;
}

SimulatedDefinitionBuilder& SimulatedDefinitionBuilder::variancePercent(float v)
{
    lastChannel().sim.variancePercent = v;
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

SimulatedDefinitionBuilder::Channel& SimulatedDefinitionBuilder::lastChannel()
{
    if (channels_.empty()) {
        std::fprintf(stderr, "[SimulatedDefinitionBuilder] No channel added yet — add a channel first\n");
        static Channel dummy;
        return dummy;
    }
    return channels_.back();
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
        if (ch.sim.rpm > 0.0f) sim["rpm"] = ch.sim.rpm;
        if (ch.sim.rollerDiamMm > 0.0f) sim["rollerDiamMm"] = ch.sim.rollerDiamMm;
        if (ch.sim.resolutionPpr > 0) sim["resolutionPpr"] = ch.sim.resolutionPpr;
        if (ch.sim.quadrature) sim["quadrature"] = true;
        if (ch.sim.partsPerMin > 0.0f) sim["partsPerMin"] = ch.sim.partsPerMin;
        if (ch.sim.partWidthMm > 0.0f) sim["partWidthMm"] = ch.sim.partWidthMm;
        if (ch.sim.variancePercent > 0.0f) sim["variancePercent"] = ch.sim.variancePercent;
        if (ch.sim.pulseMs > 0) sim["pulseMs"] = ch.sim.pulseMs;
        if (ch.sim.debounceMs > 0) sim["debounceMs"] = ch.sim.debounceMs;
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
        impl_->ethercatAdapter = std::make_unique<ethercat::EthercatAdapter>(builderState_.ethercatCycleNs);
        impl_->ethercatAdapter->setCatalog(&catalog);
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

    // Initialize adapters and add to registry
    bool catalogChanged = false;

    if (impl_->ethercatAdapter) {
        std::printf("[Context] Initializing EtherCAT backend...\n");
        if (impl_->ethercatAdapter->initialize()) {
            impl_->registry.addBackend(std::move(impl_->ethercatAdapter));
            catalogChanged = true;
        } else {
            std::printf("[Context] EtherCAT initialization failed (no hardware or stub mode)\n");
        }
    }

    if (impl_->gpioAdapter) {
        auto variant = impl_->gpioAdapter->boardVariant();
        if (variant == gpio::BoardVariant::UNKNOWN) {
            std::printf("[Context] Skipping GPIO backend — not a recognized embedded platform\n");
        } else {
            std::printf("[Context] Initializing GPIO backend (%s)...\n",
                        gpio::boardVariantName(variant).c_str());
            if (impl_->gpioAdapter->initialize()) {
                // Save raw pointer before moving unique_ptr into registry
                impl_->gpioPtr = impl_->gpioAdapter.get();
                impl_->registry.addBackend(std::move(impl_->gpioAdapter));
                catalogChanged = true;
            } else {
                std::printf("[Context] GPIO initialization failed\n");
            }
        }
    }

    if (impl_->i2cAdapter) {
        std::printf("[Context] Initializing I2C backend...\n");
        if (impl_->i2cAdapter->initialize()) {
            impl_->registry.addBackend(std::move(impl_->i2cAdapter));
            catalogChanged = true;
        } else {
            std::printf("[Context] I2C initialization failed\n");
        }
    }

    if (impl_->spiAdapter) {
        std::printf("[Context] Initializing SPI backend...\n");
        if (impl_->spiAdapter->initialize()) {
            impl_->registry.addBackend(std::move(impl_->spiAdapter));
            catalogChanged = true;
        } else {
            std::printf("[Context] SPI initialization failed\n");
        }
    }

    if (impl_->simAdapter) {
        std::printf("[Context] Initializing Simulated backend...\n");
        if (impl_->simAdapter->initialize()) {
            impl_->registry.addBackend(std::move(impl_->simAdapter));
            catalogChanged = true;
        } else {
            std::printf("[Context] Simulated initialization failed\n");
        }
    }

    // Save catalog if it changed
    if (catalogChanged) {
        catalog.save(builderState_.catalogPath);
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

    // Activate GPIO hardware handles for registered lines only.
    // Must happen before registry freeze so PDOs are built with real data.
    if (impl_->gpioPtr && !impl_->gpioPtr->isActivated()) {
        impl_->gpioPtr->deferredActivate();
    }

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
