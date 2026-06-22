// ============================================================================
// gpio_demo.cpp — GPIO Digital Output Demo with Real-Time Statistics.
//
// Architecture: Main drains SPSC rings + prints. Hot-path thread walks lights.
// All stats are CUMULATIVE via Welford's online algorithm (purely additive).
// Walk delay: 1 second per light. Stats printed every second (cumulative).
// ============================================================================

#include <cstdio>
#include <string>
#include <vector>

#include "dynamichardware/DynamicHardwareBuilder.h"
#include "../stats_demos/chaser_with_stats.h"

using namespace dynamichardware;

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);   // Line-buffered for SSH / pipe visibility.
    printf("==========================================================\n");
    printf("  libdynamichardware — GPIO Digital Output Demo (Stats)\n");
    printf("==========================================================\n\n");

    DynamicHardwareBuilder builder;
    builder.catalogPath("hardware.json")
           .mappingPath("gpio_mappings.json")
           .enableBackend("GPIO");

    if (!builder.discover()) {
        fprintf(stderr, "[Demo] Discovery failed\n"); return 1;
    }

    struct GpioCandidate { std::string uuid; std::string name; };
    std::vector<GpioCandidate> candidates;

    constexpr const char* kDemoNames[] = { 
        "BCM2711 GPIO 27", "BCM2711 GPIO 21", 
        "BCM2711 GPIO 13", "BCM2711 GPIO 26" 
    };

    const auto& catalog = builder.catalog();
    for (const auto& entry : catalog.entries()) {
        if (entry.channelType != "DigitalIO") continue;

        bool wanted = false;
        for (auto wn : kDemoNames)
            if (entry.name == wn) { wanted = true; break; }
        if (!wanted) continue;

        builder.mapChannel(entry.uuid, dhdo::EntryType::BoolOutput);
        candidates.push_back({entry.uuid, entry.name});
    }

    if (candidates.empty()) {
        fprintf(stderr, "[Demo] No suitable GPIO pins found.\n"); return 1;
    }

    printf("[Demo] Mapped %u GPIO output channels:\n", static_cast<unsigned>(candidates.size()));
    for (size_t i = 0; i < candidates.size(); ++i)
        printf("  [%zu] %s\n", i, candidates[i].name.c_str());

    auto ctx = builder.buildRT();
    if (!ctx) { fprintf(stderr, "[Demo] RT context build failed\n"); return 1; }

    if (!ctx->freeze()) { fprintf(stderr, "[Demo] Context freeze failed\n"); return 1; }

    printf("\n[Demo] Health check — backends: %zu, entries: %zu, healthy: %s\n\n",
           ctx->backendCount(), ctx->entryCount(),
           ctx->allBackendsHealthy() ? "yes" : "no");

    std::vector<dhdo::DHDOEntry*> gpio_outs;
    std::vector<std::string> channel_names;

    for (const auto& c : candidates) {
        dhdo::DHDOEntry* p = ctx->lookupByUuid(c.uuid);
        if (p) {
            gpio_outs.push_back(p);
            channel_names.push_back(c.name);
        }
    }

    if (gpio_outs.empty()) {
        fprintf(stderr, "[Demo] No GPIO output channels resolved.\n"); return 1;
    }

    printf("[Demo] Resolved %u GPIO output channel(s)\n", static_cast<unsigned>(gpio_outs.size()));
    printf("[Demo] [light] on each switch | [stats] every second (cumulative). Ctrl+C to stop.\n\n");

    constexpr long long kTargetPeriodNs = 1'000'000LL;   // 1 ms target cycle time

    try {
        runChaserWithStats(gpio_outs, channel_names,
                           kTargetPeriodNs, 1000);      // cycles_per_light: ~1s walk delay at 1kHz
    } catch (...) {
        fprintf(stderr, "[Demo] Unexpected exception — shutting down.\n");
    }

    ctx->shutdown();
    printf("\n[Demo] Complete — context shut down cleanly.\n");
    return 0;
}
