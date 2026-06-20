// ============================================================================
// ethercat_demo.cpp — Example consumer program for libdynamichardware.
//
// Demonstrates:
//   1. Building a DynamicHardwareContext with the EtherCAT backend
//   2. Discovering and enumerating digital output channels
//   3. Walking through each output (chaser / "running lights" pattern):
//      turn on one light for 1 second, then move to the next
// ============================================================================

#include <cstdio>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "dynamichardware/DynamicHardwareContext.h"

using namespace dynamichardware;

int main()
{
    std::printf("==========================================================\n");
    std::printf("  libdynamichardware — EtherCAT Digital Output Demo\n");
    std::printf("==========================================================\n\n");

    // ------------------------------------------------------------------
    // Step 1: Discover hardware using the factory API.
    // ------------------------------------------------------------------
    DynamicHardwareContextFactory factory;
    factory.catalogPath("hardware.json")
           .withEthercat(1'000'000u);              // 1 ms cycle time (DC sync)

    if (!factory.discover()) {
        std::fprintf(stderr, "[Demo] Discovery failed\n");
        return 1;
    }

    // ------------------------------------------------------------------
    // Step 2: Build RT context from discovered data.
    // ------------------------------------------------------------------
    auto ctx = factory.buildRT();
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
    // NOTE: EtherCAT entries use structured keys as identifiers stored in 
    //       DHDOEntry::uuid (e.g., "EC|2|157888594|POS3|28672:1"). The 
    //       CatalogEntry::uuid field is a random GUID used only for persistence.
    //       We match on entry.key to resolve live DHDOEntry pointers.
    // ------------------------------------------------------------------
    struct OutputInfo {
        dhdo::DHDOEntry* ptr{nullptr};
        std::string name;          ///< Human-readable display name
        uint16_t slavePos{0};      ///< Slave position on bus
        uint16_t pdoSubindex{0};   ///< PDO subindex (channel/pin number)
    };

    const auto& catalog = ctx->catalogEntries();

    std::vector<OutputInfo> outputs;

    for (const auto& entry : catalog) {
        if (!entry.isOutput || entry.channelType != "DigitalOutput") continue;

        // Use the structured key — this is what DHDOEntry::uuid holds at runtime.
        dhdo::DHDOEntry* ptr = ctx->lookupByUuid(entry.key);
        if (ptr) {
            // Build a human-friendly name from the structured key fields.
            std::string displayName = entry.name.empty() ? entry.slaveName + "[ch" +
                std::to_string(entry.pdoSubindex) + "]" : entry.name;

            outputs.push_back({ptr, displayName, entry.slavePos, entry.pdoSubindex});
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
        std::printf("  [%2zu] Slave %u ch%u — %s\n",
                    i, outputs[i].slavePos, outputs[i].pdoSubindex, outputs[i].name.c_str());
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

                std::printf("[%lus] Lighting: Slave %u ch%u — %s\n",
                            elapsed, outputs[currentLight].slavePos,
                            outputs[currentLight].pdoSubindex,
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
