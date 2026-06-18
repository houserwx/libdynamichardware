#include "dynamichardware/config/Config.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace common::config {

Config Config::loadFromJson(const std::string& path)
{
    Config cfg;
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open config file: " + path);
    }

    json j = json::parse(ifs);

    cfg.cycleTimeUs = j.value("cycleTimeUs", cfg.cycleTimeUs);
    cfg.demoCycles = j.value("demoCycles", cfg.demoCycles);
    cfg.hardwareCatalogPath = j.value("hardwareCatalogPath", cfg.hardwareCatalogPath);

    if (j.contains("pdoEntries")) {
        for (const auto& entry_j : j["pdoEntries"]) {
            PdoEntryDef def;
            def.name = entry_j.value("name", "");
            def.hwUuid = entry_j.value("hwUuid", "");
            def.channelType = entry_j.value("channelType", "");
            def.pulseMs = entry_j.value("pulseMs", 0);
            def.debounceMs = entry_j.value("debounceMs", 0);
            def.pin = entry_j.value("pin", -1);

            if (entry_j.contains("sim")) {
                const auto& sim_j = entry_j["sim"];
                def.sim.rpm = sim_j.value("rpm", 0.0f);
                def.sim.rollerDiamMm = sim_j.value("rollerDiamMm", 0.0f);
                def.sim.resolutionPpr = sim_j.value("resolutionPpr", 0);
                def.sim.quadrature = sim_j.value("quadrature", false);
                def.sim.partsPerMin = sim_j.value("partsPerMin", 0.0f);
                def.sim.partWidthMm = sim_j.value("partWidthMm", 0.0f);
                def.sim.variancePercent = sim_j.value("variancePercent", 0.0f);
            }

            cfg.pdoEntries.push_back(std::move(def));
        }
    }

    return cfg;
}

} // namespace common::config
