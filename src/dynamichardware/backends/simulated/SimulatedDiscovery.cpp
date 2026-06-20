#include "dynamichardware/backends/simulated/SimulatedDiscovery.h"

#include "dynamichardware/dhdo/HardwareCatalog.h"
#include "dynamichardware/dhdo/DHDO.h"

#include <cmath>
#include <climits>
#include <cstdio>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

namespace dynamichardware::simulated {

// ---------------------------------------------------------------------------
// Constructor — store path to JSON definitions file.
// ---------------------------------------------------------------------------
SimulatedDiscovery::SimulatedDiscovery(std::string definitionsPath)
    : definitionsPath_(std::move(definitionsPath)) {}

// ---------------------------------------------------------------------------
// Helper: map channelType string → EntryType enum + isOutput flag
// ---------------------------------------------------------------------------
static void resolveChannelType(const std::string& type, dhdo::EntryType& outType, bool& outIsOutput) noexcept
{
    outIsOutput = false;

    if (type == "BoolInput")   outType = dhdo::EntryType::BoolInput;
    else if (type == "BoolOutput") { outType = dhdo::EntryType::BoolOutput; outIsOutput = true; }
    else if (type == "Int8Input")  outType = dhdo::EntryType::Int8Input;
    else if (type == "Int16Input") outType = dhdo::EntryType::Int16Input;
    else if (type == "Int32Input") outType = dhdo::EntryType::Int32Input;
    else if (type == "Int16Output") { outType = dhdo::EntryType::Int16Output; outIsOutput = true; }
    else if (type == "FloatInput")  outType = dhdo::EntryType::FloatInput;
    else if (type == "FloatOutput") { outType = dhdo::EntryType::FloatOutput; outIsOutput = true; }
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
        std::string chanName = ch.value("name", "");
        std::string simKey   = ch.value("uuid", "SIM|" + chanName);

        dynamichardware::dhdo::CatalogEntry entry{};
        entry.key         = simKey;
        entry.channelType = ch.value("channelType", "BoolInput");
        entry.name        = chanName;
        entry.slaveName   = "Simulated";
        entry.isSimulated = true;

        dhdo::EntryType resolvedType{};
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
