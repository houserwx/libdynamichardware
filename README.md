# libDynamicHardware

Hardware abstraction layer for dynamic I/O channels. Provides a hardware-agnostic PDO (Process Data Object) system with pluggable backend adapters for EtherCAT, GPIO, I2C, SPI, gRPC, and simulated devices.

Designed to be consumed as a **CMake subdirectory** or **git submodule** from host projects.

## Architecture

```
include/
├── backends/
│   ├── pdo/            # Core abstraction: PDOEntry, PDO, IHardwareAdapter, HardwareRegistry
│   ├── ethercat/       # EtherCAT slave communication & hardware catalog
│   ├── gpio/           # GPIO pin management (libgpiod v2)
│   ├── i2c/            # I2C device abstraction
│   ├── spi/            # SPI device abstraction
│   ├── grpc/           # gRPC message-passing backend
│   ├── motor/          # Motor control logic
│   └── simulated/      # Virtual/simulated hardware for testing
└── dynamichardware/
    ├── rt/             # RT utilities: SignalProcess, VectorBuffer
    ├── config/         # JSON-based hardware configuration
    └── math/           # Fixed-point math helpers
```

## API Surface

The library exposes:

- **PDO System** — Typed, zero-vtable I/O channels with read/write, debounce, and pulse machines
- **IHardwareAdapter** — Interface that all backends implement (initialize, onBeforeReadInputs, onAfterWriteOutputs)
- **HardwareRegistry** — Central registry mapping adapter names → PDO entries
- **HardwareCatalog** — JSON-driven mapping of physical devices to logical channels
- **Backends** — EtherCAT, GPIO, I2C, SPI, gRPC, Motor, Simulated

## Usage

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
| C++20 compiler | Yes | Language standard |
| nlohmann_json | Recommended | JSON config/catalog loading |
| libethercat | Optional | Real EtherCAT communication |
| libgpiod | Optional | Real GPIO access |

When optional dependencies are missing, the library builds in stub mode with `#ifdef` guards.

## Optional CMake Flags

| Variable | Description |
|---|---|
| `ETHERCAT_LIB` | Path to libethercat.so for cross-compilation |
| `LIBGPIOD_LIB` | Path to libgpiod.so for cross-compilation |
| `LIBGPIOD_INCLUDE_DIR` | Path to libgpiod headers for cross-compilation |

## Include Conventions

All headers use the `backends/` prefix:

```cpp
#include "backends/pdo/PDO.h"
#include "backends/pdo/IHardwareAdapter.h"
#include "backends/pdo/HardwareRegistry.h"
#include "backends/ethercat/EthercatAdapter.h"
#include "backends/ethercat/HardwareCatalog.h"
#include "backends/gpio/GPIOAdapter.h"
#include "backends/simulated/SimulatedAdapter.h"
```

RT utilities use the `dynamichardware/` prefix:

```cpp
#include "dynamichardware/rt/SignalProcess.h"
#include "dynamichardware/rt/VectorBuffer.h"
#include "dynamichardware/config/Config.h"
```
