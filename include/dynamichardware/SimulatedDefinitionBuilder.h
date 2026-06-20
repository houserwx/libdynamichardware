#pragma once

// ============================================================================
// SimulatedDefinitionBuilder — fluent builder for simulated adapter definitions.
//
// Generates JSON consumed by SimulatedAdapter::loadDefinitions().
// Mirrors EntryType channel types with rate-of-change simulation:
//   BoolInput  → periodic square-wave toggle
//   Int*Input  → linear increment per cycle (optional bounds)
//   FloatInput → sinusoidal oscillation
//   Output     → pass-through / echo (no sim params needed)
//
// Usage:
//   auto defs = SimulatedDefinitionBuilder::create()
//       .cycleTimeUs(500)
//       .boolInput("LimitSwitch-1", "virt-di-0001")
//           .togglePeriodMs(100).dutyCyclePercent(30.0f)
//       .int32Input("Encoder-A", "virt-enc-a-0001")
//           .incrementPerCycle(10)
//       .floatInput("TempSensor-1", "virt-fi-0001")
//           .amplitude(5.0f).frequencyHz(0.5f).offset(25.0f)
//       .save("SimulatedAdapterDefinitions.json");
// ============================================================================

#include "dynamichardware/dhdo/HardwareCatalog.h"

#include <string>
#include <vector>

namespace dynamichardware {

class SimulatedDefinitionBuilder {
public:
    /// Create a new builder (static factory).
    static SimulatedDefinitionBuilder create();

    /// Set cycle time in microseconds.
    SimulatedDefinitionBuilder& cycleTimeUs(int us);

    // ---- Channel types (mirror EntryType system) ----

    /// Add a boolean input channel.
    SimulatedDefinitionBuilder& boolInput(const std::string& name, const std::string& uuid);

    /// Add a boolean output channel.
    SimulatedDefinitionBuilder& boolOutput(const std::string& name, const std::string& uuid);

    /// Add an 8-bit integer input channel.
    SimulatedDefinitionBuilder& int8Input(const std::string& name, const std::string& uuid);

    /// Add a 16-bit integer input channel.
    SimulatedDefinitionBuilder& int16Input(const std::string& name, const std::string& uuid);

    /// Add a 32-bit integer input channel.
    SimulatedDefinitionBuilder& int32Input(const std::string& name, const std::string& uuid);

    /// Add a 16-bit integer output channel.
    SimulatedDefinitionBuilder& int16Output(const std::string& name, const std::string& uuid);

    /// Add a float input channel.
    SimulatedDefinitionBuilder& floatInput(const std::string& name, const std::string& uuid);

    /// Add a float output channel.
    SimulatedDefinitionBuilder& floatOutput(const std::string& name, const std::string& uuid);

    // ---- Simulation parameters (applied to the last-added channel) ----

    // Boolean: periodic toggle behavior
    SimulatedDefinitionBuilder& togglePeriodMs(uint32_t v);
    SimulatedDefinitionBuilder& dutyCyclePercent(float v);

    // Integer: linear ramp / bounded sawtooth
    SimulatedDefinitionBuilder& incrementPerCycle(int32_t v);
    SimulatedDefinitionBuilder& minValue(int64_t v);
    SimulatedDefinitionBuilder& maxValue(int64_t v);

    // Float: sinusoidal oscillation
    SimulatedDefinitionBuilder& amplitude(float v);
    SimulatedDefinitionBuilder& frequencyHz(float v);
    SimulatedDefinitionBuilder& offset(float v);

    // I/O configuration (any type)
    SimulatedDefinitionBuilder& pulseMs(uint32_t v);
    SimulatedDefinitionBuilder& debounceMs(uint32_t v);

    /// Save definitions to a JSON file. Returns true on success.
    bool save(const std::string& path) const;

    /// Get the JSON string representation.
    std::string toJson() const;

private:
    struct Channel {
        std::string name;
        std::string uuid;
        std::string channelType;  ///< EntryType name string (e.g. "BoolInput", "Int32Input")
        dhdo::CatalogEntry::SimParams sim{};
    };

    std::vector<Channel> channels_;
    uint32_t cycleTimeUs_{1000};

    Channel& lastChannel();
};

} // namespace dynamichardware
