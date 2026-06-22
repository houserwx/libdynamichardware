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

    // Pure delegation to orchestrator state — no knowledge of which backends exist.
    // Validation happens at discover/build time when constructors succeed or fail.
    orchestrator_->state_.enabledBackends[name] = config;
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
