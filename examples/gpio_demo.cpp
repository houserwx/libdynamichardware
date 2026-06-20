// ============================================================================
// gpio_demo.cpp — Example consumer program for libdynamichardware.
//
// Demonstrates:
//   1. Building a DynamicHardwareContext with the GPIO backend enabled
//   2. Inspecting discovered GPIO pins from the catalog
//   3. Defining channels via DHDOFactory (.defineChannel()) to map specific
//      pins as outputs before buildRT()
//   4. Walking through each output (chaser / "running lights" pattern):
//      turn on one light for 3 seconds, then move to the next
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
    std::printf("  libdynamichardware — GPIO Digital Output Demo\n");
    std::printf("==========================================================\n\n");

    // ------------------------------------------------------------------
    // Step 1: Discover hardware using the factory API.
    //         This scans available GPIO lines and populates the catalog.
    //         Catalog entries are bidirectional ("DigitalIO") at this stage.
    // ------------------------------------------------------------------
    DynamicHardwareContextFactory factory;
    factory.catalogPath("hardware.json").withGPIO();

    if (!factory.discover()) {
        std::fprintf(stderr, "[Demo] Discovery failed\n");
        return 1;
    }

    // ------------------------------------------------------------------
    // Step 2: Consumer inspects discovered catalog entries and defines which
    //         channels to map, with what direction/type (via DHDOFactory).
    //
    // EtherCAT autobuilds from EEPROM as an idiosyncrasy of that backend.
    // For GPIO (and I2C/SPI), the consumer explicitly defines each channel.
    // ------------------------------------------------------------------

    struct GpioCandidate {
        std::string key;       ///< e.g., "GPIO|00|17"
        uint32_t lineNum{0};   ///< Extracted GPIO offset
    };

    std::vector<GpioCandidate> outputs;

    for (const auto& entry : factory.catalog().entries()) {
        if (entry.key.substr(0, 5) != "GPIO|" || entry.channelType != "DigitalIO") continue;

        // Select a subset of GPIO lines to use as outputs.
        // Pick pins commonly available on Raspberry Pi header:
        //   GPIO17, GPIO27, GPIO22, GPIO23, GPIO24, GPIO25
        uint32_t gpioOffset = 0;
        try {
            size_t lastSep = entry.key.rfind('|');
            if (lastSep != std::string::npos && lastSep + 1 < entry.key.size()) {
                gpioOffset = static_cast<uint32_t>(std::stoul(entry.key.substr(lastSep + 1)));
            }
        } catch (...) { continue; }

        constexpr int kOutputPins[] = {17, 27, 22, 23, 24, 25};
        bool isDesiredPin = false;
        for (int p : kOutputPins) {
            if (static_cast<int>(gpioOffset) == p) { isDesiredPin = true; break; }
        }

        if (!isDesiredPin) continue;

        // Define this pin as an output channel — adds to factory's internal list.
        factory.defineChannel(entry.key, dhdo::EntryType::BoolOutput);
        outputs.push_back({entry.key, gpioOffset});
    }

    if (outputs.empty()) {
        std::fprintf(stderr, "[Demo] No suitable GPIO pins found for demo.\n");
        return 1;
    }

    std::printf("[Demo] Defined %u GPIO output channels:\n", static_cast<unsigned>(outputs.size()));
    for (size_t i = 0; i < outputs.size(); ++i) {
        std::printf("  [%zu] GPIO%u (key: %s)\n", i, outputs[i].lineNum, outputs[i].key.c_str());
    }

    // ------------------------------------------------------------------
    // Step 3: Build RT context from discovered data + consumer definitions.
    //         GPIORTBackend reads the definition list and creates DHDOEntries.
    // ------------------------------------------------------------------
    auto ctx = factory.buildRT();
    if (!ctx) {
        std::fprintf(stderr, "[Demo] RT context build failed\n");
        return 1;
    }

    if (!ctx->freeze()) {
        std::fprintf(stderr, "[Demo] Context freeze failed\n");
        return 1;
    }

    std::printf("\n[Demo] Health check — backends: %zu, entries: %zu, healthy: %s\n\n",
                ctx->backendCount(),
                ctx->entryCount(),
                ctx->allBackendsHealthy() ? "yes" : "no");

    // ------------------------------------------------------------------
    // Step 4: Cache live DHDOEntry pointers by resolving keys at init-time.
    //         These pointers are safe to use in the RT loop after freeze().
    // ------------------------------------------------------------------
    struct OutputInfo {
        dhdo::DHDOEntry* ptr{nullptr};
        uint32_t lineNum{0};
    };

    std::vector<OutputInfo> gpioOuts;

    for (const auto& out : outputs) {
        dhdo::DHDOEntry* ptr = ctx->lookupByUuid(out.key);
        if (ptr) {
            gpioOuts.push_back({ptr, out.lineNum});
        } else {
            std::fprintf(stderr, "[Demo] Warning: lookupByUuid(%s) returned null — skipping\n", out.key.c_str());
        }
    }

    if (gpioOuts.empty()) {
        std::fprintf(stderr, "[Demo] No GPIO output channels resolved.\n");
        return 1;
    }

    std::printf("[Demo] Running chaser pattern — each light ON for 3 seconds...\n");
    std::printf("[Demo] Press Ctrl+C to stop early.\n\n");

    // ------------------------------------------------------------------
    // Step 5: Chaser loop — walk through all GPIO outputs one at a time.
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
            for (size_t i = 0; i < gpioOuts.size(); ++i) {
                gpioOuts[i].ptr->setBool(i == currentLight);
            }

            // -- WRITE phase -------------------------------------------------
            ctx->writeAll();

            // Advance to next light after the desired duration elapses.
            ++cyclesSinceSwitch;
            if (cyclesSinceSwitch >= static_cast<unsigned>(kCyclesPerLight)) {
                cyclesSinceSwitch = 0;

                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - startTime).count();

                std::printf("[%lus] Lighting: GPIO%u\n",
                            elapsed, gpioOuts[currentLight].lineNum);

                currentLight = (currentLight + 1) % gpioOuts.size();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(kCycleTimeMs));
        }
    } catch (...) {
        std::fprintf(stderr, "[Demo] Unexpected exception — shutting down.\n");
    }

    // ------------------------------------------------------------------
    // Step 6: Graceful shutdown.
    // ------------------------------------------------------------------
    ctx->shutdown();
    std::printf("\n[Demo] Complete — context shut down cleanly.\n");
    return 0;
}
