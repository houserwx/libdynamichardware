#include "dynamichardware/backends/simulated/SimulatedDiscovery.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>

namespace dynamichardware::simulated {

// ---------------------------------------------------------------------------
// Helper: map channelType string → EntryType enum + isOutput flag
// ---------------------------------------------------------------------------
static void resolveChannelType(const std::string& type, pdo::EntryType& outType, bool& outIsOutput) noexcept
{
    outIsOutput = false;

    if (type == "BoolInput")   outType = pdo::EntryType::BoolInput;
    else if (type == "BoolOutput") { outType = pdo::EntryType::BoolOutput; outIsOutput = true; }
    else if (type == "Int8Input")  outType = pdo::EntryType::Int8Input;
    else if (type == "Int16Input") outType = pdo::EntryType::Int16Input;
    else if (type == "Int32Input") outType = pdo::EntryType::Int32Input;
    else if (type == "Int16Output") { outType = pdo::EntryType::Int16Output; outIsOutput = true; }
    else if (type == "FloatInput")  outType = pdo::EntryType::FloatInput;
    else if (type == "FloatOutput") { outType = pdo::EntryType::FloatOutput; outIsOutput = true; }
}

// ---------------------------------------------------------------------------
// IDiscoveryBackend::discover() — read JSON definitions and populate catalog.
// DISCOVERY DISCOVERS WHAT HARDWARE IS AVAILABLE (CHANNELS) AND POPULATES CATALOG. THAT IS ALL.
// After return, this object can be destroyed — no state survives.
// ---------------------------------------------------------------------------
bool SimulatedDiscovery::discover()
{
    if (!catalog_) {
        std::fprintf(stderr, "[Simulated-Discovery] No catalog attached\n");
        return false;
    }

    std::ifstream f(definitionsPath_);
    if (!f) {
        std::fprintf(stderr, "[Simulated-Discovery] Cannot open '%s'\n", definitionsPath_.c_str());
        return false;
    }

    using json = nlohmann::json;
    auto j = json::parse(f);

    if (!j.contains("channels")) {
        std::fprintf(stderr, "[Simulated-Discovery] No 'channels' key in '%s'\n", definitionsPath_.c_str());
        return false;
    }

    for (const auto& ch : j["channels"]) {
        dynamichardware::pdo::CatalogEntry entry{};
        entry.key         = "SIM|" + ch.value("name", "");
        entry.uuid        = ch.value("uuid", "");
        entry.channelType = ch.value("channelType", "BoolInput");
        entry.name        = ch.value("name", "");
        entry.slaveName   = "Simulated";
        entry.isSimulated = true;

        pdo::EntryType resolvedType{};
        bool isOutput{false};
        resolveChannelType(entry.channelType, resolvedType, isOutput);
        entry.isOutput = isOutput;

        if (ch.contains("sim")) {
            const auto& s = ch["sim"];
            // Generic rate-of-change parameters
            entry.sim.togglePeriodMs     = s.value("togglePeriodMs", 0u);
            entry.sim.dutyCyclePercent   = s.value("dutyCyclePercent", 50.0f);
            entry.sim.incrementPerCycle  = s.value("incrementPerCycle", 1);
            entry.sim.minValue           = s.value("minValue", static_cast<int64_t>(INT64_MIN));
            entry.sim.maxValue           = s.value("maxValue", static_cast<int64_t>(INT64_MAX));
            entry.sim.amplitude          = s.value("amplitude", 1.0f);
            entry.sim.frequencyHz        = s.value("frequencyHz", 1.0f);
            entry.sim.offset             = s.value("offset", 0.0f);
            // I/O configuration
            entry.sim.pulseMs            = s.value("pulseMs", 0u);
            entry.sim.debounceMs         = s.value("debounceMs", 0u);
        }

        catalog_->addEntry(std::move(entry));
    }

    std::printf("[Simulated-Discovery] Loaded %zu simulated channels from '%s'\n",
                j["channels"].size(), definitionsPath_.c_str());
    return true;
}

} // namespace dynamichardware::simulated
