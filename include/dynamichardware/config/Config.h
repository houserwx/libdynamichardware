#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace common::config {

struct PdoEntryDef {
    // Simulation parameters — populated only when isSimulated() && "sim" key present.
    struct SimParams {
        float    rpm           {0.0f};
        float    rollerDiamMm  {0.0f};
        uint32_t resolutionPpr {0};
        bool     quadrature    {false};
        float    partsPerMin   {0.0f};
        float    partWidthMm   {0.0f};
        float    variancePercent{0.0f};
    };

    std::string name;
    std::string hwUuid;
    std::string channelType;
    uint32_t    pulseMs{0};
    uint32_t    debounceMs{0};
    int         pin{-1};
    SimParams   sim{};

    [[nodiscard]] bool isSimulated() const { return hwUuid.rfind("virt-", 0) == 0; }
    [[nodiscard]] bool isGpio()      const { return pin >= 0; }
};

struct Config {
    int                      cycleTimeUs{500};
    int                      demoCycles{0};

    [[nodiscard]] uint32_t cycleNs() const noexcept {
        return static_cast<uint32_t>(cycleTimeUs) * 1000U;
    }
    std::string              hardwareCatalogPath{"config/shared/hardware_catalog.json"};
    std::vector<PdoEntryDef> pdoEntries;

    static Config loadFromJson(const std::string& path);
};

} // namespace common::config
