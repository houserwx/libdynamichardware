// ============================================================================
// ethercat_demo.cpp — EtherCAT Digital Output Demo with RT Statistics.
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
    std::printf("  libdynamichardware — EtherCAT Digital Output Demo (Stats)\n");
    std::printf("==========================================================\n\n");

    // ── Step 1: Register EtherCAT backend and discover channels ──────────────
    DynamicHardwareBuilder builder;
    builder.catalogPath("hardware.json")
           .mappingPath("ethercat_mappings.json")
           .enableBackend("EtherCAT", {{"cycleNs", "1000000"}});   // 1 ms cycle time (DC sync)

    if (!builder.discover()) {
        std::fprintf(stderr, "[Demo] Discovery failed\n"); return 1;
    }

    // ── Step 2: Build RT context from discovered data ────────────────────────
    auto ctx = builder.buildRT();
    if (!ctx) {
        std::fprintf(stderr, "[Demo] RT context build failed — is an EtherCAT master available?\n"); 
        return 1; 
    }

    // ── Step 3: Freeze — locks entries for RT operation ──────────────────────
    if (!ctx->freeze()) { std::fprintf(stderr, "[Demo] Context freeze failed\n"); return 1; }

    std::printf("[Demo] Health check — backends: %zu, entries: %zu, healthy: %s\n\n",
                ctx->backendCount(), ctx->entryCount(),
                ctx->allBackendsHealthy() ? "yes" : "no");

    // ── Step 4: Collect all BoolOutput channels and cache DHDOEntry pointers ──
    std::vector<dhdo::DHDOEntry*> outputs;
    std::vector<std::string> channel_names;

    const auto& catalog = ctx->catalogEntries();
    for (const auto& entry : catalog) {
        if (!entry.isOutput || entry.channelType != "DigitalOutput") continue;

        dhdo::DHDOEntry* p = ctx->lookupByUuid(entry.uuid);
        if (p) {
            outputs.push_back(p);
            channel_names.push_back(entry.name);  // e.g., "EtherCAT Drive1 DO.0"
        }
    }

    if (outputs.empty()) {
        std::fprintf(stderr, "[Demo] No BoolOutput channels found on the EtherCAT bus.\n"); return 1;
    }

    std::printf("[Demo] Found %u digital output channel(s):\n", static_cast<unsigned>(outputs.size()));
    for (size_t i = 0; i < outputs.size(); ++i)
        std::printf("  [%zu] %s\n", i, channel_names[i].c_str());
    std::printf("\n[Demo] Stats will print every 5 seconds. Ctrl+C to stop.\n\n");

    // ── Step 5: Launch stats thread + RT chaser loop ─────────────────────────
    constexpr long long kTargetPeriodNs = 1'000'000LL;   // 1 ms target in ns (1ms cycle)
    
    SpscRing<int64_t, 8192> timing_ring;
    SpscRing<unsigned, 256> event_ring;
    StatsCollector collector(timing_ring, event_ring, kTargetPeriodNs, &channel_names);

    try {

        runChaserLoop(outputs, timing_ring, event_ring,
                      kTargetPeriodNs, static_cast<unsigned>(1000 / 1));
    } catch (...) {
        std::fprintf(stderr, "[Demo] Unexpected exception — shutting down.\n");
    }

    ctx->shutdown();
    std::printf("\n[Demo] Complete — context shut down cleanly.\n");
    return 0;
}
