#include "dynamichardware/SimulatedDefinitionBuilder.h"

#include <climits>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace dynamichardware {

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

} // namespace dynamichardware
