// ============================================================================
// DynamicHardwareBuilder — thin fluent API wrapper around HardwareOrchestrator.
// All heavy lifting (phase ordering, backend iteration) lives in the orchestrator.
// This class is ~80 lines and only handles consumer-facing convenience methods.
// ============================================================================

#include "dynamichardware/DynamicHardwareBuilder.h"
#include "dynamichardware/HardwareOrchestrator.h"

#include <unordered_map>

namespace dynamichardware {

DynamicHardwareBuilder::DynamicHardwareBuilder()
    : orchestrator_(std::make_unique<HardwareOrchestrator>(OrchestratorState{})) {}

DynamicHardwareBuilder& DynamicHardwareBuilder::catalogPath(std::string path) {
    orchestrator_->state_.catalogPath = std::move(path); // TODO: expose via method when private access is restricted
    return *this;
}

DynamicHardwareBuilder& DynamicHardwareBuilder::enableBackend(
        std::string name, 
        const std::unordered_map<std::string, std::string>& config) {
    
    auto& st = orchestrator_->state_; // Access internal state for now
    
    if (name == "EtherCAT") {
        st.enableEthercat = true;
        auto it = config.find("cycleNs");
        if (it != config.end()) {
            st.ethercatCycleNs = static_cast<uint32_t>(std::stoul(it->second));
        }
    } else if (name == "GPIO") {
        st.enableGPIO = true;
    } else if (name == "I2C") {
        st.enableI2C = true;
        auto it = config.find("busPath");
        if (it != config.end()) {
            st.i2cBusPath = it->second;
        }
    } else if (name == "SPI") {
        st.enableSPI = true;
        auto it = config.find("busPath");
        if (it != config.end()) {
            st.spiBusPath = it->second;
        }
    } else if (name == "Simulated") {
        st.enableSimulation = true;
        auto it = config.find("definitionsPath");
        if (it != config.end()) {
            st.simDefinitionsPath = it->second;
        }
    } else {
        std::fprintf(stderr, "[Builder] WARNING: Unknown backend name '%s' — ignoring\n", 
                     name.c_str());
    }
    
    return *this;
}

DynamicHardwareBuilder& DynamicHardwareBuilder::mapChannel(
        const std::string& keyOrUuid, dhdo::EntryType type, const std::string& friendlyName) {
    orchestrator_->addChannelDefinition(keyOrUuid, type, friendlyName);
    return *this;
}

DynamicHardwareBuilder& DynamicHardwareBuilder::mappingPath(std::string path) {
    orchestrator_->state_.mappingPath = std::move(path); // TODO: expose via method
    return *this;
}

size_t DynamicHardwareBuilder::loadMappings() {
    return orchestrator_->loadMappings();
}

bool DynamicHardwareBuilder::discover() {
    return orchestrator_->discover();
}

const dhdo::HardwareCatalog& DynamicHardwareBuilder::catalog() const noexcept {
    return orchestrator_->catalog();
}

dhdo::HardwareCatalog& DynamicHardwareBuilder::catalog() noexcept {
    return orchestrator_->catalog();
}

std::unique_ptr<DynamicHardwareContextObject> DynamicHardwareBuilder::buildRT() {
    return orchestrator_->buildRT();
}

} // namespace dynamichardware
