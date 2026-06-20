# libdynamichardware — Getting Started Guide

This guide walks you through the standard lifecycle of a consumer program using libdynamichardware: discover hardware, define your channels, build the real-time context, freeze for RT operation, and run the read/process/write loop.

---

## Table of Contents

1. [Overview](#overview)
2. [Step 1 — Discover Hardware](#step-1--discover-hardware)
3. [Step 2 — Define DHDO Entries](#step-2--define-dhdo-entries)
4. [Step 3 — Build Real-Time Context](#step-3--build-real-time-context)
5. [Step 4 — Freeze for RT Operation](#step-4--freeze-for-rt-operation)
6. [Step 5 — The RT Loop](#step-5--the-rt-loop)
7. [Complete Minimal Example](#complete-minimal-example)
8. [API Quick Reference](#api-quick-reference)

---

## Overview

Every application follows this five-phase lifecycle:

```
Discover → Define Channels → Build RT → Freeze → RT Loop (read / process / write)
```

| Phase | What happens | Who does it |
|---|---|---|
| **Discover** | Backends scan hardware and populate a catalog of all available channels | Library backends |
| **Define** | Consumer selects which catalog entries to activate as inputs or outputs | **You** (except EtherCAT) |
| **Build RT** | Factory creates backend instances and registers `DHDOEntry` objects | Library factory |
| **Freeze** | Locks the entry set; no more additions; hardware resources are claimed | Library context |
| **RT Loop** | Deterministic read-process-write cycles at your target frequency | Your code |

### Include Header

All public APIs are accessible through a single forwarding header:

```cpp
#include "dynamichardware/DynamicHardwareContext.h"
using namespace dynamichardware;
```

---

## Step 1 — Discover Hardware

Create a `DynamicHardwareContextFactory`, register the backends you need, then call `discover()`. Discovery scans for physically present hardware and populates an internal catalog. It does **not** create any live I/O yet.

```cpp
DynamicHardwareContextFactory factory;

// Register backends — each .with*() method enables a different backend type.
factory.catalogPath("hardware.json")       // Persist catalog between runs
       .withGPIO();                         // GPIO lines (e.g., Raspberry Pi gpiod)
//   .withEthercat(1'000'000u);            // EtherCAT (cycle time in microseconds)
//   .withSimulation("defs.json");         // Simulated adapter for testing

if (!factory.discover()) {
    std::fprintf(stderr, "Discovery failed\n");
    return 1;
}
```

### Available Backends

| Method | Backend | Description |
|---|---|---|
| `.withGPIO()` | GPIO (`gpiod`) | Linux GPIO character device; discovers all available line offsets |
| `.withEthercat(cycleTimeUs)` | EtherCAT (IgH) | Scans EtherCAT bus; reads slave EEPROMs for PDO mapping |
| `.withSimulation(jsonFile)` | Simulated | Loads channel definitions from JSON; no real hardware required |

---

## Step 2 — Define DHDO Entries

> **Important:** This step is required for **all backends EXCEPT EtherCAT**.  
> EtherCAT auto-builds its entries from the slave EEPROM PDO mappings during discovery. For every other backend, you must explicitly tell the library which channels to activate and what type they are.

After `discover()`, inspect the catalog and use `factory.defineChannel()` to register each desired channel:

```cpp
for (const auto& entry : factory.catalog().entries()) {
    // Filter by key prefix or channelType as needed.
    if (entry.key.substr(0, 5) != "GPIO|") continue;

    // Define this discovered pin as a BoolOutput.
    factory.defineChannel(entry.key, dhdo::EntryType::BoolOutput);
}
```

### Available Entry Types

| `dhdo::EntryType` | Description | Accessor Methods on `DHDOEntry` |
|---|---|---|
| `BoolInput` | Digital input (true/false) | `.getBool()` after read phase |
| `BoolOutput` | Digital output (true/false) | `.setBool(value)` before write phase |
| `Int16Input` / `Int16Output` | 16-bit signed integer | `.getInt16()` / `.setInt16(value)` |
| `Int32Input` / `Int32Output` | 32-bit signed integer | `.getInt32()` / `.setInt32(value)` |
| `FloatInput` / `FloatOutput` | IEEE-754 single precision float | `.getFloat()` / `.setFloat(value)` |

### EtherCAT Exception

EtherCAT is the only backend that skips this step entirely. During discovery it reads each slave's EEPROM and auto-registers every PDO subindex as an active entry — you cannot cherry-pick because the process data image is a contiguous DMA region managed by the IgH domain objects.

---

## Step 3 — Build Real-Time Context

Call `buildRT()` to construct all backend instances, create live `DHDOEntry` objects from your definitions, and return a runtime context:

```cpp
auto ctx = factory.buildRT();
if (!ctx) {
    std::fprintf(stderr, "RT context build failed\n");
    return 1;
}
```

The returned smart pointer owns the entire hardware registry including backends and entries.

---

## Step 4 — Freeze for RT Operation

Freezing locks the entry set for real-time operation. After freeze:

- No new entries can be added or removed.
- Hardware resources are claimed (e.g., GPIO lines are requested).
- The system becomes immutable and safe for deterministic cycling.

```cpp
if (!ctx->freeze()) {
    std::fprintf(stderr, "Context freeze failed\n");
    return 1;
}
```

### Cache Entry Pointers at Init Time

Before entering the RT loop, resolve and cache pointers to the entries you'll use. Lookups are inexpensive outside the loop but should not block inside it:

```cpp
// Resolve once after freeze() — these pointers remain valid until shutdown.
dhdo::DHDOEntry* pumpRelay   = ctx->lookupByUuid("sim-relay-pump");
dhdo::DHDOEntry* limitSwitch = ctx->lookupByUuid("sim-limit-a");
```

---

## Step 5 — The RT Loop

Your application's main loop follows a strict three-phase pattern every cycle:

```
┌──────────────────────────────────────┐
│  READ phase                          │
│  ctx->readAll();                     │
│  ← All inputs updated from hardware  │
├──────────────────────────────────────┤
│  PROCESS phase                       │
│  Read values, run logic, set outputs │
│  entry->getBool(), entry->setBool()  │
├──────────────────────────────────────┤
│  WRITE phase                         │
│  ctx->writeAll();                    │
│  → All outputs pushed to hardware    │
└──────────────────────────────────────┘
         ↓ sleep for cycle period
        (loop back)
```

### Phase Details

**READ** — Pulls fresh data from all backends into the process image:
```cpp
ctx->readAll();
```

**PROCESS** — Your application logic. Access cached `DHDOEntry` pointers directly:
```cpp
// Read an input value (populated by readAll())
bool limitHit = limitSwitch->getBool();

// Set output values (will be applied by writeAll())
pumpRelay->setBool(!limitHit);
motorSpeed->setFloat(targetRpm * 0.1f);
```

**WRITE** — Pushes output registers from the process image to physical hardware:
```cpp
ctx->writeAll();
```

### Full Loop Example

```cpp
constexpr int kCycleTimeMs = 1;  // 1 kHz RT cycle

while (running) {
    // --- READ ---
    ctx->readAll();

    // --- PROCESS ---
    bool sensorValue = limitSwitch->getBool();
    pumpRelay->setBool(sensorValue);

    // --- WRITE ---
    ctx->writeAll();

    std::this_thread::sleep_for(std::chrono::milliseconds(kCycleTimeMs));
}
```

### Shutdown

When done, call shutdown to release all resources cleanly:
```cpp
ctx->shutdown();
```

---

## Complete Minimal Example

Here is a minimal program that discovers GPIO hardware, defines two pins as digital outputs, and toggles them in an RT loop:

```cpp
#include <cstdio>
#include <thread>
#include "dynamichardware/DynamicHardwareContext.h"

using namespace dynamichardware;

int main()
{
    // ---- Step 1: Discover ----
    DynamicHardwareContextFactory factory;
    factory.catalogPath("hardware.json").withGPIO();

    if (!factory.discover()) {
        std::fprintf(stderr, "Discovery failed\n");
        return 1;
    }

    // ---- Step 2: Define channels (NOT needed for EtherCAT) ----
    factory.defineChannel("GPIO|00|17", dhdo::EntryType::BoolOutput);
    factory.defineChannel("GPIO|00|27", dhdo::EntryType::BoolOutput);

    // ---- Step 3: Build RT context ----
    auto ctx = factory.buildRT();
    if (!ctx || !ctx->freeze()) {
        std::fprintf(stderr, "Build/freeze failed\n");
        return 1;
    }

    // Cache entry pointers once after freeze.
    auto* ledA = ctx->lookupByUuid("GPIO|00|17");
    auto* ledB = ctx->lookupByUuid("GPIO|00|27");

    // ---- Step 4: RT loop ----
    bool state = false;
    while (true) {
        ctx->readAll();                          // READ phase
        ledA->setBool(state);                    // PROCESS phase
        ledB->setBool(!state);
        ctx->writeAll();                         // WRITE phase

        state = !state;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    ctx->shutdown();                             // Graceful cleanup
}
```

---

## API Quick Reference

### DynamicHardwareContextFactory

| Method | Description |
|---|---|
| `.catalogPath(path)` | Set path for persistent hardware catalog JSON file |
| `.withGPIO()` | Enable GPIO backend discovery |
| `.withEthercat(cycleTimeUs)` | Enable EtherCAT backend with specified cycle time (µs) |
| `.withSimulation(jsonFile)` | Enable simulated backend from definition file |
| `.discover()` | Scan all enabled backends → populate catalog. Returns `bool`. |
| `.defineChannel(key, type)` | Register a discovered channel as an active DHDO entry |
| `.buildRT()` | Build real-time context. Returns `std::unique_ptr<DynamicHardwareContextObject>`. |

### DynamicHardwareContextObject

| Method | Description |
|---|---|
| `.freeze()` | Lock entries for RT operation. Must be called before first read/write cycle. |
| `.readAll()` | Read all inputs from all backends into process image |
| `.writeAll()` | Write all outputs from process image to physical hardware |
| `.lookupByUuid(uuid)` | Resolve `DHDOEntry*` by key/UUID string — cache result after freeze() |
| `.shutdown()` | Stop backends and release resources |
| `.backendCount()` | Number of registered backends |
| `.entryCount()` | Total number of active DHDO entries |
| `.allBackendsHealthy()` | Health check: true if all backends report healthy communication |
