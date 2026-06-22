// ============================================================================
// simulated_io_demo.cpp — Example consumer program for libdynamichardware.
//
// Demonstrates:
//   1. Building simulated adapter definitions with SimulatedDefinitionBuilder
//   2. Three-phase workflow: Discover → Build RT → Freeze → Run cycles
//   3. Cached DHDOEntry pointer lookups before entering the RT loop
//   4. Walking lights chaser pattern across multiple BoolOutput channels
// ============================================================================

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <vector>

#include "dynamichardware/DynamicHardwareBuilder.h"
#include "dynamichardware/SimulatedDefinitionBuilder.h"

using namespace dynamichardware;

// Number of cycles to run before exiting (for demonstration purposes).
static constexpr int kCycleCount = 50000;

int main()
{
    std::printf("==========================================================\n");
    std::printf("  libdynamichardware — Simulated I/O Demo\n");
    std::printf("==========================================================\n\n");

    // ------------------------------------------------------------------
    // Step 1: Define simulated channels using the fluent builder API.
    //         This generates a JSON file consumed by the SimulatedAdapter.
    // ------------------------------------------------------------------
    auto defs = SimulatedDefinitionBuilder::create()
        .cycleTimeUs(1000)                       // 1 kHz cycle time

        // --- Inputs ---------------------------------------------------
        .boolInput("LimitSwitch-A",      "sim-limit-a")
            .togglePeriodMs(200).dutyCyclePercent(60.0f)     // Toggling square wave

        .int32Input("Encoder-Position",   "sim-encoder-pos")
            .incrementPerCycle(5)                                     // Linear ramp

        .floatInput("Temperature-Sensor", "sim-temp-sensor")
            .amplitude(3.0f).frequencyHz(0.25f).offset(24.5f) // Sinusoidal temp ~24.5°C

        // --- Outputs --------------------------------------------------
        .boolOutput("Relay-Pump",         "sim-relay-pump")

        .int16Output("DAC-Voltage",       "sim-dac-voltage")

	.floatOutput("PID-Speed-Setpoint", "sim-speed-sp")

	// --- Walking Lights LEDs (mimic GPIO/EtherCAT chaser demos) --------
	.boolOutput("LED-0",              "sim-led-0")
	.boolOutput("LED-1",              "sim-led-1")
	.boolOutput("LED-2",              "sim-led-2")
	.boolOutput("LED-3",              "sim-led-3")
	.boolOutput("LED-4",              "sim-led-4")
	.boolOutput("LED-5",              "sim-led-5");

    // ------------------------------------------------------------------
    // Step 2: Register backends and discover channels via fluent API.
    //         Builder.enableBackend() stores name + config map;
    //         orchestrator dispatches through BackendRegistry at runtime.
    // ------------------------------------------------------------------
    DynamicHardwareBuilder builder;
    builder.catalogPath("hardware.json")
           .enableBackend("Simulated", {{"definitionsPath", "SimulatedAdapterDefinitions.json"}});

    if (!builder.discover()) {
        std::fprintf(stderr, "[Demo] Discovery failed\n");
        return 1;
    }

    // ------------------------------------------------------------------
    // Step 3: Build RT context from discovered data.
    //         Orchestrator passes channel lists TO each backend's build()
    //         method; no public setup methods on backends are needed.
    // ------------------------------------------------------------------
    auto ctx = builder.buildRT();
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

    // Optional: print full internal state (debug)
    ctx->printState();

    // ------------------------------------------------------------------
    // Step 5: Cache entry pointers (init-time lookups before RT loop).
    //         In a real application you'd store these in your controller class.
    // ------------------------------------------------------------------
    dhdo::DHDOEntry* limitSwitch   = ctx->lookupByName("LimitSwitch-A");
    dhdo::DHDOEntry* encoderPos    = ctx->lookupByName("Encoder-Position");
    dhdo::DHDOEntry* tempSensor    = ctx->lookupByName("Temperature-Sensor");

    dhdo::DHDOEntry* relayPump     = ctx->lookupByName("Relay-Pump");
    dhdo::DHDOEntry* dacVoltage    = ctx->lookupByName("DAC-Voltage");
    dhdo::DHDOEntry* speedSetpoint = ctx->lookupByName("PID-Speed-Setpoint");

	// Walking lights LED entry pointers — cache before entering RT loop
	std::vector<dhdo::DHDOEntry*> leds;
	leds.reserve(6);
	for (int n = 0; n < 6; ++n) {
	    std::string ledName = "LED-" + std::to_string(n);
	    auto* p = ctx->lookupByName(ledName);
	    if (!p) {
	        std::fprintf(stderr, "[Demo] LED channel '%s' not found\n", ledName.c_str());
	        return 1;
	    }
	    leds.push_back(p);
	}

    std::printf("[Demo] All channel lookups succeeded (including %zu LEDs).\n",
	            leds.size());
    std::printf("[Demo] Running %d RT cycles at simulated 1 kHz...\n\n", kCycleCount);

	// Walking lights timing parameters
	constexpr int kCyclesPerLight = 50;  // Each LED stays ON for 50 cycles (~50ms @ 1kHz)
	unsigned currentLed = 0;

	// Print header
	std::printf("%-6s | %-8s | %-12s | %-14s | %-6s | %-9s | %-13s | %-6s\n",
	            "Cycle", "LimitSW", "Encoder.Pos", "Temp (°C)", "Relay", "DAC (mV)", "Speed SP", "LED");
	std::printf("------------------------------------------------------------------------------------\n");

    // ------------------------------------------------------------------
    // Step 5: RT read / process / write loop.
    //         In a real application this would run in an RT-priority thread.
    // ------------------------------------------------------------------
    for (int i = 0; i < kCycleCount; ++i) {
        // -- READ phase --------------------------------------------------
        ctx->readAll();

        // Read input values from cached pointers (branchless hot path).
        bool   limitSw     = limitSwitch->getBool();
        int32_t encPos     = encoderPos->getInt32();
        float  temperature = tempSensor->getFloat();

        // -- PROCESS phase -----------------------------------------------
        // Simple logic: relay on when temperature above threshold AND switch closed.
        bool pumpOn = temperature > 25.0f && !limitSw;

        // Compute DAC voltage proportional to encoder position (scaled).
        int16_t dacValue = static_cast<int16_t>(encPos * 2);

        // Speed setpoint: ramp up gradually, then hold at 1500 rpm.
        float speedSP = static_cast<float>(std::min(i, 30)) * 50.0f;

        // -- UPDATE WALKING LIGHTS -------------------------------------------
        // Set all LEDs OFF, then turn ON the current one in the chaser sequence.
        for (auto* led : leds) {
            led->setBool(false);
        }
        leds[currentLed]->setBool(true);

        // Advance to next LED after the dwell period elapses
        if ((i + 1) % kCyclesPerLight == 0) {
            currentLed = (currentLed + 1) % leds.size();
        }

        // -- WRITE phase -------------------------------------------------
        relayPump->setBool(pumpOn);
        dacVoltage->setInt16(dacValue);
        speedSetpoint->setFloat(speedSP);

        ctx->writeAll();

        // Print row every 5 cycles to keep output readable.
        if (i % 5 == 0 || i == kCycleCount - 1) {
            std::printf("%-6d | %-8s | %-12d | %-14.2f | %-6s | %-9d | %-13.0f | LED-%u\n",
                        i,
                        limitSw ? "CLOSED" : "open ",
                        encPos,
                        temperature,
                        pumpOn ? "ON " : "off",
                        dacValue,
                        speedSP,
                        currentLed);
        }

        // Simulate RT cycle timing (remove in production — use hardware timer).
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    std::printf("---------------------------------------------------------------------------\n");

    // ------------------------------------------------------------------
    // Step 6: Graceful shutdown.
    // ------------------------------------------------------------------
    ctx->shutdown();
    std::printf("\n[Demo] Complete — context shut down cleanly.\n");
    return 0;
}
