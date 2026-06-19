// ============================================================================
// simulated_io_demo.cpp — Example consumer program for libdynamichardware.
//
// Demonstrates:
//   1. Building simulated adapter definitions with SimulatedDefinitionBuilder
//   2. Creating and building a DynamicHardwareContext (lifecycle)
//   3. Freezing PDOs for RT operation
//   4. Running an RT read/process/write loop using cached entry pointers
// ============================================================================

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>

#include "dynamichardware/DynamicHardwareContext.h"

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

        .floatOutput("PID-Speed-Setpoint","sim-speed-sp");

    if (!defs.save("SimulatedAdapterDefinitions.json")) {
        std::fprintf(stderr, "[Demo] Failed to write simulated definitions\n");
        return 1;
    }

    // ------------------------------------------------------------------
    // Step 2: Build the DynamicHardwareContext with the simulation backend.
    // ------------------------------------------------------------------
    auto ctx = DynamicHardwareContext::builder()
        .catalogPath("hardware.json")
        .withSimulation("SimulatedAdapterDefinitions.json")
        .build();

    if (!ctx || !ctx->build()) {
        std::fprintf(stderr, "[Demo] Context build failed\n");
        return 1;
    }

    // ------------------------------------------------------------------
    // Step 3: Freeze PDOs — locks catalog for RT operation.
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
    // Step 4: Cache entry pointers (init-time lookups before RT loop).
    //         In a real application you'd store these in your controller class.
    // ------------------------------------------------------------------
    pdo::PDOEntry* limitSwitch   = ctx->lookupByName("LimitSwitch-A");
    pdo::PDOEntry* encoderPos    = ctx->lookupByName("Encoder-Position");
    pdo::PDOEntry* tempSensor    = ctx->lookupByName("Temperature-Sensor");

    pdo::PDOEntry* relayPump     = ctx->lookupByName("Relay-Pump");
    pdo::PDOEntry* dacVoltage    = ctx->lookupByName("DAC-Voltage");
    pdo::PDOEntry* speedSetpoint = ctx->lookupByName("PID-Speed-Setpoint");

    if (!limitSwitch || !encoderPos || !tempSensor ||
        !relayPump   || !dacVoltage || !speedSetpoint) {
        std::fprintf(stderr, "[Demo] One or more channels not found — check names\n");
        return 1;
    }

    std::printf("[Demo] All channel lookups succeeded.\n");
    std::printf("[Demo] Running %d RT cycles at simulated 1 kHz...\n\n", kCycleCount);

    // Print header
    std::printf("%-6s | %-8s | %-12s | %-14s | %-6s | %-9s | %-13s\n",
                "Cycle", "LimitSW", "Encoder.Pos", "Temp (°C)", "Relay", "DAC (mV)", "Speed SP");
    std::printf("---------------------------------------------------------------------------\n");

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

        // -- WRITE phase -------------------------------------------------
        relayPump->setBool(pumpOn);
        dacVoltage->setInt16(dacValue);
        speedSetpoint->setFloat(speedSP);

        ctx->writeAll();

        // Print row every 5 cycles to keep output readable.
        if (i % 5 == 0 || i == kCycleCount - 1) {
            std::printf("%-6d | %-8s | %-12d | %-14.2f | %-6s | %-9d | %-13.0f\n",
                        i,
                        limitSw ? "CLOSED" : "open ",
                        encPos,
                        temperature,
                        pumpOn ? "ON " : "off",
                        dacValue,
                        speedSP);
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
