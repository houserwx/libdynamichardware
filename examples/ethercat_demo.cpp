// ============================================================================
// ethercat_demo.cpp — Example consumer program for libdynamichardware.
//
// Demonstrates:
//   1. Using DynamicHardwareBuilder with the EtherCAT backend (fixes C/D/E)
//   2. Discovering and enumerating digital output channels via unified interface 
//      (no more "autobuild idiosyncrasy" at consumer level — fixes G)
//   3. Walking through each output (chaser / "running lights" pattern):
//      turn on one light for 1 second, then move to the next
// ============================================================================

#include <cstdio>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "dynamichardware/DynamicHardwareBuilder.h"

using namespace dynamichardware;

int main()
{
    std::printf("==========================================================\n");
    std::printf("  libdynamichardware — EtherCAT Digital Output Demo\n");
    std::printf("==========================================================\n\n");

    // ------------------------------------------------------------------
    // Step 1: Discover hardware using the new builder API.
    //         BackendRegistry iteration replaces hardcoded if-blocks in factory.
    // ------------------------------------------------------------------
    DynamicHardwareBuilder builder;
    builder.catalogPath("hardware.json")
           .enableBackend("EtherCAT", {{"cycleNs", "1000000"}});              // 1 ms cycle time (DC sync)

    if (!builder.discover()) {
        std::fprintf(stderr, "[Demo] Discovery failed\n");
        return 1;
    }

    // ------------------------------------------------------------------
    // Step 2: Build RT context from discovered data.
    //         Orchestrator passes channels TO backends — no public setup methods (fixes A/F/H).
    // ------------------------------------------------------------------
    auto ctx = builder.buildRT();
    if (!ctx) {
        std::fprintf(stderr, "[Demo] RT context build failed — is an EtherCAT master available?\n");
        return 1;
    }

    // ------------------------------------------------------------------
    // Step 3: Freeze — locks entries for RT operation.
    // ------------------------------------------------------------------
    if (!ctx->freeze()) {
        std::fprintf(stderr, "[Demo] Context freeze failed\n");
        return 1;
    }

    std::printf("[Demo] Health check — backends: %zu, entries: %zu, healthy: %s\n\n",
                ctx->backendCount(),
                ctx->entryCount(),
                ctx->allBackendsHealthy() ? "yes" : "no");

    // ------------------------------------------------------------------
    // Step 3: Collect all digital output (BoolOutput) channels from the 
    //         catalog and cache their DHDOEntry pointers.
    //
    // Each catalog entry has a stable UUID derived from backend-specific fields.
    // We use getDetails() to expose common info without touching raw backend data.
    // ------------------------------------------------------------------
    struct OutputInfo {
        dhdo::DHDOEntry* ptr{nullptr};
        std::string name;          ///< Human-readable display name
    };

    const auto& catalog = ctx->catalogEntries();

    std::vector<OutputInfo> outputs;

    for (const auto& entry : catalog) {
        if (!entry.isOutput || entry.channelType != "DigitalOutput") continue;

        // Resolve live DHDOEntry pointer using the stable catalog UUID.
        dhdo::DHDOEntry* ptr = ctx->lookupByUuid(entry.uuid);
        if (ptr) {
            // Use the unified ChannelDetails view instead of raw backend fields.
            auto details = entry.getDetails();
            std::string displayName = details.name.empty() ? 
                details.uuid.substr(0, 8) : details.name;

            outputs.push_back({ptr, displayName});
        } else {
            // Stale catalog entry with no live PDO backing — skip silently.
            continue;
        }
    }

    if (outputs.empty()) {
        std::fprintf(stderr, "[Demo] No BoolOutput channels found on the EtherCAT bus.\n");
        return 1;
    }

    std::printf("[Demo] Found %u digital output channel(s):\n", static_cast<unsigned>(outputs.size()));

    for (size_t i = 0; i < outputs.size(); ++i) {
        std::printf("  [%2zu] %s\n", i, outputs[i].name.c_str());
    }

   std::printf("[Demo] Running chaser pattern — each light ON for 3 seconds...\n");
    std::printf("[Demo] Press Ctrl+C to stop early.\n\n");

    // ------------------------------------------------------------------
    // Step 4: Chaser loop — walk through all digital outputs one at a time.
    //         Each output stays HIGH for ~3 seconds while others are LOW.
    // ------------------------------------------------------------------
    constexpr int kCycleTimeMs = 1;           // RT cycle period (1 ms)
    constexpr int kOnDurationMs = 3000;       // Each light on for 3 seconds
    constexpr int kCyclesPerLight = kOnDurationMs / kCycleTimeMs;

    unsigned currentLight = 0;
    unsigned cyclesSinceSwitch = 0;

    auto startTime = std::chrono::steady_clock::now();

    try {
        while (true) {
            // -- READ phase --------------------------------------------------
            ctx->readAll();

            // -- PROCESS phase -----------------------------------------------
            // All outputs start OFF; set the current one ON.
            for (size_t i = 0; i < outputs.size(); ++i) {
                outputs[i].ptr->setBool(i == currentLight);
            }

            // -- WRITE phase -------------------------------------------------
            ctx->writeAll();

            // Advance to next light after the desired duration elapses.
            ++cyclesSinceSwitch;
            if (cyclesSinceSwitch >= static_cast<unsigned>(kCyclesPerLight)) {
                cyclesSinceSwitch = 0;

                // Print status when we switch lights.
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - startTime).count();

                 std::printf("[%lus] Lighting: %s\n",
                            static_cast<unsigned long>(elapsed),
                            outputs[currentLight].name.c_str());

                // Move to next output (wrap around).
                currentLight = (currentLight + 1) % outputs.size();
            }

            // Simulate RT cycle timing.
            std::this_thread::sleep_for(std::chrono::milliseconds(kCycleTimeMs));
        }
    } catch (...) {
        std::fprintf(stderr, "[Demo] Unexpected exception — shutting down.\n");
    }

    // ------------------------------------------------------------------
    // Step 5: Graceful shutdown.
    // ------------------------------------------------------------------
    ctx->shutdown();
    std::printf("\n[Demo] Complete — context shut down cleanly.\n");
    return 0;
}
