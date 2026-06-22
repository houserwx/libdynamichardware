// ============================================================================
// gpio_demo.cpp — GPIO Digital Output Demo with RT Statistics.
// 
// Thin entry point that wires up: builder → catalog filter → freeze → chaser loop.
// All RT timing infrastructure (SPSC ring, stats thread) lives in ../stats_demos/.
// Walk delay: 1 second per light. Stats printed every 5 seconds by collector thread.
// ============================================================================

#include <cstdio>
#include <string>
#include <vector>

#include "dynamichardware/DynamicHardwareBuilder.h"
#include "../stats_demos/spsc_ring.h"
#include "../stats_demos/stats_collector.h"
#include "../stats_demos/chaser_loop.h"

using namespace dynamichardware;

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);   // Line-buffered for SSH / pipe visibility.
    std::printf("==========================================================\n");
    std::printf("  libdynamichardware — GPIO Digital Output Demo (Stats)\n");
    std::printf("==========================================================\n\n");

    // ── Step 1: Discover hardware via builder API ────────────────────────
    DynamicHardwareBuilder builder;
    builder.catalogPath("hardware.json")
           .mappingPath("gpio_mappings.json")
           .enableBackend("GPIO");

    if (!builder.discover()) {
        std::fprintf(stderr, "[Demo] Discovery failed\n"); return 1;
    }

    // ── Step 2: Filter catalog entries by name only (no backend internals) ──
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
        std::fprintf(stderr, "[Demo] No suitable GPIO pins found.\n"); return 1;
    }

    std::printf("[Demo] Mapped %u GPIO output channels:\n", static_cast<unsigned>(candidates.size()));
    for (size_t i = 0; i < candidates.size(); ++i)
        std::printf("  [%zu] %s\n", i, candidates[i].name.c_str());

    // ── Step 3: Build RT context from discovered data + consumer mappings ────
    auto ctx = builder.buildRT();
    if (!ctx) { std::fprintf(stderr, "[Demo] RT context build failed\n"); return 1; }

    // ── Step 4: Freeze — locks entries for RT operation ──────────────────────
    if (!ctx->freeze()) { std::fprintf(stderr, "[Demo] Context freeze failed\n"); return 1; }

    std::printf("\n[Demo] Health check — backends: %zu, entries: %zu, healthy: %s\n\n",
                ctx->backendCount(), ctx->entryCount(),
                ctx->allBackendsHealthy() ? "yes" : "no");

    // ── Step 5: Cache live DHDOEntry pointers by resolving catalog UUIDs ──────
    std::vector<dhdo::DHDOEntry*> gpio_outs;

    for (const auto& c : candidates) {
        dhdo::DHDOEntry* p = ctx->lookupByUuid(c.uuid);
        if (p) gpio_outs.push_back(p);
    }

    if (gpio_outs.empty()) {
        std::fprintf(stderr, "[Demo] No GPIO output channels resolved.\n"); return 1;
    }

    std::printf("[Demo] Resolved %u GPIO output channel(s)\n", static_cast<unsigned>(gpio_outs.size()));
    std::printf("[Demo] Stats will print every 5 seconds. Ctrl+C to stop.\n\n");

    // ── Step 6: Collect channel names for stats display ───────────────────────
    std::vector<std::string> channel_names;
    for (const auto& c : candidates) {
        channel_names.push_back(c.name);  // e.g., "BCM2711 GPIO 27"
    }

    // ── Step 7: Launch stats thread + RT chaser loop ─────────────────────────
    constexpr long long kTargetPeriodNs = 1'000'000LL;   // 1 ms target in ns (1ms cycle)
    
    SpscRing<int64_t, 8192> timing_ring;
    SpscRing<unsigned, 256> event_ring;
    StatsCollector collector(timing_ring, event_ring, kTargetPeriodNs, &channel_names);

    try {

        runChaserLoop(gpio_outs, timing_ring, event_ring,
                      kTargetPeriodNs, static_cast<unsigned>(1000 / 1));
    } catch (...) {
        std::fprintf(stderr, "[Demo] Unexpected exception — shutting down.\n");
    }

    ctx->shutdown();
    std::printf("\n[Demo] Complete — context shut down cleanly.\n");
    return 0;
}
