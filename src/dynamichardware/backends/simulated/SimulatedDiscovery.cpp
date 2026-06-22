#include "dynamichardware/backends/simulated/SimulatedDiscovery.h"

#include "dynamichardware/dhdo/HardwareCatalog.h"
#include "dynamichardware/dhdo/HardwareDescriptor.h"
#include "dynamichardware/dhdo/DHDO.h"

#include "dynamichardware/backends/registration.h"
#include "dynamichardware/backends/simulated/SimulatedRTBackend.h"

#include <cmath>
#include <climits>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
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
// IBackenScanner::scan() — pure data scan: parse JSON into HardwareDescriptor vector.
// Does NOT mutate catalog_. Fixes Issue I by returning structured data instead of side effects.
// ---------------------------------------------------------------------------
std::vector<dhdo::HardwareDescriptor> SimulatedDiscovery::scan()
{
    std::vector<dhdo::HardwareDescriptor> results;

    std::ifstream f(definitionsPath_);
    if (!f) {
        std::fprintf(stderr, "[Simulated-Discovery] Cannot open '%s'\n", definitionsPath_.c_str());
        return results;
    }

    using json = nlohmann::json;
    auto j = json::parse(f);

    if (!j.contains("channels")) {
        std::fprintf(stderr, "[Simulated-Discovery] No 'channels' key in '%s'\n", definitionsPath_.c_str());
        return results;
    }

    for (const auto& ch : j["channels"]) {
        dhdo::HardwareDescriptor desc{};

        // Use user-provided uuid as the channel identity for simulated entries.
        desc.uuid  = ch.value("uuid", "");

        // Channel type and name.
        desc.channelType   = ch.value("channelType", "BoolInput");
        std::string chanName = ch.value("name", "");
        if (chanName.empty()) {
            chanName = "Simulated " + desc.channelType;
        }
        desc.name = chanName;

        // Resolve direction from channelType string (isOutput set via out parameter).
        dhdo::EntryType resolvedType{};
        resolveChannelType(desc.channelType, resolvedType, desc.isOutput);

        // Backend-specific data (simulated has no extra fields beyond common).
        desc.backend     = dhdo::BackendType::SIMULATED;
        desc.backendData = dhdo::SimulatedBackendData{};

        results.push_back(std::move(desc));
    }

    return results;
}

// ---------------------------------------------------------------------------
// discover() — thin wrapper: calls scan(), feeds results into catalog_.
// Sim params extracted from JSON are merged per-channel since HardwareDescriptor lacks them yet.
// DISCOVERY DISCOVERS WHAT HARDWARE IS AVAILABLE AND POPULATES CATALOG. THAT IS ALL.
// After return, this object can be destroyed — no state survives.
// ---------------------------------------------------------------------------
bool SimulatedDiscovery::discover()
{
    auto descriptors = scan();

    if (!catalog_) {
        std::fprintf(stderr, "[Simulated-Discovery] No catalog attached\n");
        return false;
    }

    if (descriptors.empty()) {
        return false;  // scan() already logged any errors
    }

    for (auto& desc : descriptors) {
        dhdo::CatalogEntry entry{};
        entry.uuid          = desc.uuid;   // User-defined UUID from JSON for simulated channels
        entry.channelType   = desc.channelType;
        entry.name          = desc.name;
        entry.slaveName     = "Simulated";
        entry.isOutput      = desc.isOutput;
        entry.isSimulated   = true;
        entry.backend       = desc.backend;
        entry.backendData   = std::move(desc.backendData);

        // Note: sim params (togglePeriodMs, amplitude, etc.) are NOT transferred here in Phase 4.
        // They live in the JSON and will be handled by the RT backend's buildRT() separately.
        // In a later phase we may add structured sim params to HardwareDescriptor/CatalogEntry.

        catalog_->addEntry(std::move(entry));
    }

    std::printf("[Simulated-Discovery] Loaded %zu simulated channels from '%s'\n",
                descriptors.size(), definitionsPath_.c_str());
    return !descriptors.empty();
}

// ---------------------------------------------------------------------------
// Self-registration with BackendRegistry — zero-boilerplate OCP compliance.
// ---------------------------------------------------------------------------
REGISTER_BACKEND("Simulated", []() {
    return std::make_pair(
        std::make_unique<simulated::SimulatedDiscovery>(),
        std::make_unique<simulated::SimulatedRTBackend>()
    );
});

} // namespace dynamichardware::simulated
