// ============================================================================
// gpio_correct_demo.cpp — Example consumer program for libdynamichardware.
//
// Demonstrates the CORRECT pattern for non-EtherCAT backends:
//   1. Build a DynamicHardwareContext with the GPIO backend enabled
//   2. Discover hardware — populates catalog with all available GPIO lines
//   3. Consumer inspects catalog entries and selectively defines channels
//      using entry UUIDs (NOT by parsing raw key strings)
//   4. Build RT context from discovered data + consumer definitions
//   5. Freeze — locks entries for RT operation
//   6. Cache live DHDOEntry pointers by resolving through catalog metadata
//   7. Walk through each output (chaser / "running lights" pattern):
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
    //
    // CRITICAL: Use catalog entry metadata (entry.uuid) for defineChannel(),
    //           NOT string-parsed keys. The catalog is the source of truth.
    // ------------------------------------------------------------------

    struct GpioCandidate {
        std::string uuid;      ///< Stable UUID from catalog entry — used for lookup
        std::string name;      ///< Human-readable display name from catalog
    };

    std::vector<GpioCandidate> candidates;

    const auto& catalog = factory.catalog().entries();

    // NOTE: At discovery time GPIO entries are bidirectional (isOutput == false,
    //       channelType == "DigitalIO"). We filter on common fields only and let
    //       defineChannel() specify the desired direction. Backend details hidden.
    for (const auto& entry : catalog) {
        if (entry.channelType != "DigitalIO") continue;

        // Define all discovered DigitalIO channels as outputs.
        // The catalog gives us just {uuid, name} — that's all we need to map a channel.
        factory.defineChannel(entry.uuid, dhdo::EntryType::BoolOutput);

        candidates.push_back({entry.uuid, entry.name});
        std::printf("[DBG] Catalog candidate uuid=%s name=%s\n",
                    entry.uuid.c_str(), entry.name.c_str());
    }

    if (candidates.empty()) {
        std::fprintf(stderr, "[Demo] No suitable GPIO pins found for demo.\n");
        return 1;
    }

    std::printf("[Demo] Defined %u GPIO output channels:\n", static_cast<unsigned>(candidates.size()));
    for (size_t i = 0; i < candidates.size(); ++i) {
        std::printf("  [%zu] %s\n",
                    i, candidates[i].name.c_str());
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

    // ------------------------------------------------------------------
    // Step 4: Freeze — locks entries for RT operation.
    // ------------------------------------------------------------------
    if (!ctx->freeze()) {
        std::fprintf(stderr, "[Demo] Context freeze failed\n");
        return 1;
    }

    std::printf("\n[Demo] Health check — backends: %zu, entries: %zu, healthy: %s\n\n",
                ctx->backendCount(),
                ctx->entryCount(),
                ctx->allBackendsHealthy() ? "yes" : "no");

    // ------------------------------------------------------------------
    // Step 5: Cache live DHDOEntry pointers by resolving catalog UUIDs at
    //         init-time. These pointers are safe to use in the RT loop after
    //         freeze(). All backend-specific info is hidden behind the UUID lookup.
    // ------------------------------------------------------------------
    struct OutputInfo {
        dhdo::DHDOEntry* ptr{nullptr};   ///< Writable RT entry (set value here)
        std::string name;                 ///< Human-readable display name from catalog
    };

    std::vector<OutputInfo> gpioOuts;

    // Resolve DHDOEntry pointers using the candidates we already defined.
    // After freeze() all mapped UUIDs are live and ready for RT access.
    for (size_t c = 0; c < candidates.size(); ++c) {
        const auto& candidate = candidates[c];
        dhdo::DHDOEntry* ptr = ctx->lookupByUuid(candidate.uuid);
        std::printf("[DBG] lookupByUuid(%s) -> %s\n",
                    candidate.uuid.c_str(), ptr ? "FOUND" : "NULL");
        if (!ptr) {  // Shouldn't happen after successful buildRT/freeze
            if (c == 0 && candidates.size() > 1) {
                // Try looking up by partial match to see what's actually registered
                std::printf("[DBG] First mismatch — catalog uuid=%s length=%zu\n",
                            candidate.uuid.c_str(), candidate.uuid.length());
            }
            continue;
        }

        gpioOuts.push_back({ptr, candidate.name});
    }

    if (gpioOuts.empty()) {
        std::fprintf(stderr, "[Demo] No GPIO output channels resolved.\n");
        return 1;
    }

    std::printf("[Demo] Resolved %u GPIO output channel(s):\n", static_cast<unsigned>(gpioOuts.size()));
    for (size_t i = 0; i < gpioOuts.size(); ++i) {
        std::printf("  [%2zu] %s\n",
                    i, gpioOuts[i].name.c_str());
    }

    std::printf("\n[Demo] Running chaser pattern — each light ON for 3 seconds...\n");
    std::printf("[Demo] Press Ctrl+C to stop early.\n\n");

    // ------------------------------------------------------------------
    // Step 6: Chaser loop — walk through all GPIO outputs one at a time.
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

                std::printf("[%lus] [%zu] %s\n",
                            elapsed, currentLight,
                            gpioOuts[currentLight].name.c_str());

                currentLight = (currentLight + 1) % gpioOuts.size();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(kCycleTimeMs));
        }
    } catch (...) {
        std::fprintf(stderr, "[Demo] Unexpected exception — shutting down.\n");
    }

    // ------------------------------------------------------------------
    // Step 7: Graceful shutdown.
    // ------------------------------------------------------------------
    ctx->shutdown();
    std::printf("\n[Demo] Complete — context shut down cleanly.\n");
    return 0;
}
