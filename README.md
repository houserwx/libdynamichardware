# libDynamicHardware

Hardware abstraction layer for dynamic I/O channels with deterministic real-time operation. Provides a zero-vtable process data object system with pluggable backend adapters for EtherCAT, GPIO, I2C, SPI, and simulated devices.

Designed to be consumed as a **CMake subdirectory** or **git submodule** from host projects.

## Architecture Overview

```
include/dynamichardware/
├── DynamicHardwareBuilder.h       # High-level fluent API (primary entry point)
├── HardwareOrchestrator.h         # Internal phase coordination & backend dispatch
├── DynamicHardwareContextObject.h # RT runtime context (freeze / readAll / writeAll)
├── SimulatedDefinitionBuilder.h   # Test fixture definition generator
│
├── dhdo/                          # Core process-image types (zero vtable)
│   ├── DHDO.h                     # DHDOEntry + DHDO — typed accessors & image buffers
│   ├── DHDOFactory.h              # Static factory helpers for entry creation
│   ├── HardwareDescriptor.h       # Pure-data scan result struct
│   ├── HardwareCatalog.h          # JSON-persisted channel metadata store
│   ├── HardwareRegistry.h         # Owns backends, orchestrates RT cycle
│   ├── IBackendScanner.h          # One-shot hardware scanner interface
│   ├── IDHDOBuilder.h             # Configuration → build(channels) interface
│   └── IRuntimeAdapter.h          # Canonical RT lifecycle interface
│
├── config/                        # Phase management & backend registry
│   ├── PhaseManager.h             # Lifecycle state machine enforcement
│   └── BackendRegistry.h          # Abstract backend type catalog
│
├── rt/                            # Real-time utilities
│   ├── SignalProcess.h            # Global timestamp cache (vDSO clock_gettime)
│   ├── VectorBuffer.h             # Lock-free SPSC ring buffer
│   └── IChannelProcessor.h        # Optional per-channel processing pipeline
│
└── backends/                      # Pluggable hardware transports
    ├── ethercat/                  # EtherCAT slave communication & PDO mapping
    ├── gpio/                      # GPIO pin management (libgpiod v2 / stub mode)
    ├── i2c/                       # I2C device abstraction (stub)
    ├── spi/                       # SPI device abstraction (stub)
    └── simulated/                 # Virtual/simulated hardware for testing
```

## Key Design Principles

- **Zero-vtable RT path** — `DHDOEntry` is a concrete struct; all hot-path methods are inlineable with no virtual dispatch. Exactly 2 virtual calls per backend per cycle (`onBeforeReadInputs` / `onAfterWriteOutputs`).
- **Discovery-first architecture** — Backends scan all available hardware and populate a central catalog before any channels are activated. Consumers select from the catalog.
- **Phase-enforced lifecycle** — Discovery → Channel Mapping → Build RT → Freeze → RT Loop. Transitions are enforced by `PhaseManager`; illegal state transitions throw at runtime.
- **SOLID-compliant interfaces** — Small, focused contracts: `IBackendScanner` (discovery), `IDHDOBuilder` (configuration), `IRuntimeAdapter` (RT lifecycle). No god classes or monolithic interfaces.

## API Surface

| Component | Responsibility |
|---|---|
| **DynamicHardwareBuilder** | Fluent API: enable backends, discover, map channels, build RT context |
| **DHDOEntry** | Concrete typed I/O channel — zero vtable, cached values, type-safe accessors |
| **DHDO** | Owns image buffer + entry vector; freezes for immutable RT operation |
| **IRuntimeAdapter** | Canonical RT interface inheriting IDHDOBuilder; adds RT cycle hooks |
| **IBackendScanner** | One-shot scanner returning pure data vectors without mutating shared state |
| **HardwareRegistry** | Owns backend vector, orchestrates readAll/writeAll RT cycle |
| **HardwareCatalog** | JSON-persisted metadata store with stable UUIDs across restarts |
| **Backends** | EtherCAT, GPIO, I2C, SPI, Simulated — each split into Discovery + RTBackend |

## Quick Start

```cpp
#include "dynamichardware/DynamicHardwareBuilder.h"
using namespace dynamichardware;

int main() {
    // 1. Configure & discover hardware
    DynamicHardwareBuilder builder;
    builder.catalogPath("hardware.json")
           .enableBackend("GPIO");

    if (!builder.discover()) return 1;

    // 2. Map discovered channels to typed entries (optional for some backends)
    builder.mapChannel("GPIO|00|17", dhdo::EntryType::BoolOutput, "LED-A");

    // 3. Build real-time context from discovered + mapped data
    auto ctx = builder.buildRT();
    if (!ctx || !ctx->freeze()) return 1;

    // 4. Cache entry pointers at init time (NOT in the RT loop!)
    auto* led_a = ctx->lookupByUuid("GPIO|00|17");

    // 5. RT loop: read → process → write
    bool state = false;
    while (running) {
        ctx->readAll();            // Pull inputs from all backends
        led_a->setBool(state);     // Application logic
        ctx->writeAll();           // Push outputs to physical hardware

        state = !state;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    ctx->shutdown();
}
```

See [Getting Started Guide](doc/GettingStarted.md), [Architecture](doc/Architecture.md), and the [examples/](examples/) directory for complete working programs.

## Usage as a Dependency

### As a git submodule

```bash
git submodule add https://github.com/houserwx/libdynamichardware thirdparty/libdynamichardware
```

In your `CMakeLists.txt`:

```cmake
add_subdirectory(thirdparty/libdynamichardware)
target_link_libraries(your_target PRIVATE DynamicHardware::dynamichardware)
```

### After installation

```cmake
find_package(DynamicHardware REQUIRED)
target_link_libraries(your_target PRIVATE DynamicHardware::dynamichardware)
```

## Dependencies

| Dependency | Required | Used For |
|---|---|---|
| CMake ≥ 3.20 | Yes | Build system |
| C++20 compiler | Yes | Language standard (constexpr, modules-ready features) |
| nlohmann_json | Bundled | JSON config/catalog loading |
| libethercat | Optional | Real EtherCAT communication (stub mode without it) |
| libgpiod | Optional | Real GPIO access (stub mode without it) |

When optional dependencies are missing, the library builds in **stub mode** with `#ifdef` guards — all interfaces compile but return no hardware.

### Optional CMake Flags

| Variable | Description |
|---|---|
| `ETHERCAT_LIB` | Path to libethercat.so for cross-compilation |
| `LIBGPIOD_LIB` | Path to libgpiod.so for cross-compilation |
| `LIBGPIOD_INCLUDE_DIR` | Path to libgpiod headers for cross-compilation |

## Include Conventions

All public headers use the `dynamichardware/` prefix:

```cpp
// Primary entry point — includes everything a consumer needs
#include "dynamichardware/DynamicHardwareBuilder.h"

// DHDO layer — process image types and registry
#include "dynamichardware/dhdo/DHDO.h"
#include "dynamichardware/dhdo/HardwareRegistry.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"

// Backend interfaces (for implementing custom backends)
#include "dynamichardware/dhdo/IBackendScanner.h"
#include "dynamichardware/dhdo/IRuntimeAdapter.h"

// RT utilities
#include "dynamichardware/rt/SignalProcess.h"
#include "dynamichardware/rt/VectorBuffer.h"

// Concrete backends (include only what you need)
#include "dynamichardware/backends/gpio/GPIORTBackend.h"
#include "dynamichardware/backends/simulated/SimulatedRTBackend.h"
```
