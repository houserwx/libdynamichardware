// SPDX-License-Identifier: MIT
#include "backends/EcatIoChain.h"
#include <cstring>
#include <algorithm>
#include "soem.h"
#include "ethercat_base.h"
#include "ethercat_config.h"

namespace dynamichardware::backends {

// Compile-time RT safety checks
static_assert(std::is_trivially_copyable_v<EcatIoChain>,
              "EcatIoChain must be trivially copyable for deterministic layout");

constexpr int EcatIoChain::kMaxSlaves;

EcatIoChain::EcatIoChain() noexcept : config_{}, ioMap_{} {
    std::memset(&state_, 0, sizeof(state_));
}

EcatIoChain::~EcatIoChain() = default;

bool EcatIoChain::initialize(const EcatConfig& cfg) {
    if (cfg.slaveCount == 0 || cfg.slaveCount > kMaxSlaves) return false;
    
    config_ = cfg;
    state_.slaveCount = cfg.slaveCount;
    state_.lastCycleTimeNs = 1'000'000; // Default 1ms
    
    // Initialize SOEM master (would call ecat_init in real implementation)
    // For now, set up the IO map structure with zeroed buffers
    
    std::memset(ioMap_.rxBuffer, 0, sizeof(ioMap_.rxBuffer));
    std::memset(ioMap_.txBuffer, 0, sizeof(ioMap_.txBuffer));
    
    state_.cycleCount = 0;
    state_.errorCount = 0;
    state_.isRunning = true;
    
    return true;
}

void EcatIoChain::processCycle() noexcept {
    // Deterministic cycle processing - NO heap allocation allowed
    if (!state_.isRunning) return;
    
    // Simulate EtherCAT cycle: read process data from slaves into RX buffer
    // In production this would call ecat_do_process() with DC sync timing
    
    ++state_.cycleCount;
    
    // Measure cycle time for diagnostics (uses pre-captured timestamp from scheduler)
    // Actual measurement happens in orchestrator to avoid per-backend clock calls
    
    // Process each slave's COE/SoE data here if needed
    // DC synchronization ensures all slaves update at same hardware timestamp
}

const EcatIoChain::IOData& EcatIoChain::getIOData() const noexcept {
    return ioMap_;
}

bool EcatIoChain::writeProcessData(const uint8_t* tx_data, size_t length) noexcept {
    if (!tx_data || length == 0 || length > sizeof(ioMap_.txBuffer)) {
        ++state_.errorCount;
        return false;
    }
    
    std::memcpy(ioMap_.txBuffer, tx_data, length);
    return true;
}

bool EcatIoChain::readProcessData(uint8_t* rx_data, size_t length) const noexcept {
    if (!rx_data || length == 0 || length > sizeof(ioMap_.rxBuffer)) return false;
    
    std::memcpy(rx_data, ioMap_.rxBuffer, length);
    return true;
}

void EcatIoChain::setCycleTimeNanoseconds(int64_t ns) noexcept {
    state_.lastCycleTimeNs = (ns > 0) ? ns : state_.lastCycleTimeNs;
}

int64_t EcatIoChain::getLastCycleTimeNanoseconds() const noexcept {
    return state_.lastCycleTimeNs;
}

const EcatIoChain::RuntimeState& EcatIoChain::getState() const noexcept {
    return state_;
}

void EcatIoChain::resetDiagnostics() noexcept {
    state_.cycleCount = 0;
    state_.errorCount = 0;
}

// ---------------------------------------------------------------------------
EcatIoChain::SlaveConfig::SlaveConfig() = default;
EcatIoChain::SlaveConfig::~SlaveConfig() = default;

} // namespace dynamichardware::backends
