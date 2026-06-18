# Copilot Agent Instructions — EtherCatDrone

**Purpose:** This document instructs Copilot agents on the architectural principles, coding patterns, and RT determinism requirements for the EtherCatDrone project. Read this before making any changes to source code.

---

## 1.0 RT Determinism — Non-Negotiable Rules

The RT hot path (from `Application::run()` through `rtCycle()`, `readAll()`, `writeAll()`, and any `tick()` method) must satisfy these criteria:

### Red Lines (DO NOT VIOLATE)
1. **No heap allocation** reachable from `run()` after init completes.
2. **No `std::unordered_map`** in any RT-cycle method.
3. **No `std::mutex`, `std::lock_guard`, or `std::condition_variable`** inside the RT thread.
4. **No `virtual` call** inside a per-entry loop (PDOEntry sweep or FunctionState sweep).
5. **No blocking syscall** (file, socket, sleep) inside `rtCycle()`.
6. **No direct shared-memory write** to RT-owned data from a non-RT thread without `VectorBuffer`.
7. **`SCHED_FIFO` priority** must not be dropped or CPU affinity removed from `Threadrunner`.

### Scoring Criteria
| Criterion | Requirement |
|-----------|-------------|
| No allocation after freeze | All `std::vector` capacity reserved before RT loop; no `push_back`, `resize`, or `new` in RT path |
| `noexcept` on hot path | Every method in the call graph from `run()` through `tick()` is marked `noexcept` |
| Zero virtual calls per entry | `readAll()`/`writeAll()` perform exactly 2 virtual calls per backend (the two cycle hooks); `tick()` performs zero virtual calls |
| Bounded O(1) access | Wrapper pool access uses array index (O(1)); no linear scan in hot path |
| No system calls in hot path | Exactly one `clock_gettime` per cycle (via `signalProcessTickNow()`); no file I/O, socket, or OS call in `rtCycle()` |
| Single RT thread owner | All RT data accessed only on RT thread; cross-thread writes use `VectorBuffer` only |
| Contiguous iteration | Entries iterated as `std::vector<PDOEntry>` or `std::array`; no pointer-chasing through a map |

---

## 2.0 Architecture Layers (Top-Down)

```
┌─────────────────────────────────────────────────────────────┐
│  WrapperPool — per-segment typed wrapper vocabulary         │
│  (DigitalInputWrapper, IMUWrapper, etc.)                    │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│  Wrappers — thin, inline-friendly accessors                 │
│  (no virtuals, no inheritance, final classes)               │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│  PDOEntry / PDO — raw memory image + type-safe accessors    │
│  (EntryType enum + switch dispatch, no vtable)              │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│  Adapters — backend-specific (EtherCAT, I2C, SPI, GPIO)     │
│  (IHardwareAdapter interface, 2 virtual calls per cycle)    │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│  HardwareRegistry — owns adapters, builds UUID map          │
│  (init-time only; RT path uses direct pointers)             │
└─────────────────────────────────────────────────────────────┘
```

---

## 3.0 RT Cycle Flow

```
while (running_) {
    clock_nanosleep(...);              // Wait for next cycle
    signalProcessTickNow();            // Update global timestamp (1 clock_gettime/cycle)
    registry_.readAll();               // Phase 0: All backends read inputs into PDOEntry caches
    rtCycle();                         // Application logic (safety, control, evaluation)
    registry_.writeAll();              // Phase 4: All backends flush outputs from PDOEntry
}
```

### `readAll()` — Two-Phase Read
```cpp
// Phase 1: Each backend reads from physical bus into PDO image buffers
for (auto& backend : backends_) backend->onBeforeReadInputs();

// Phase 2: Each entry extracts typed values from image into cached fields
for (auto& backend : backends_)
    for (auto& pdo : backend->getPDOs())
        for (auto& entry : pdo.entries) entry.read();
```

### `writeAll()` — Two-Phase Write
```cpp
// Phase 1: Each entry copies cached values back to image
for (auto& backend : backends_)
    for (auto& pdo : backend->getPDOs())
        for (auto& entry : pdo.entries) entry.write();

// Phase 2: Each backend flushes image to physical bus
for (auto& backend : backends_) backend->onAfterWriteOutputs();
```

### `rtCycle()` — Application Logic
```cpp
stateMachine_.tick(estopActive, nowNs);
if (stateMachine_.haltActive()) {
    for (auto& q : queues_) q->safeState();
    return;
}
for (auto& q : queues_) q->tick(cycleCount_, nowNs);
```

---

## 4.0 HardwareCatalog — Channel-Level Identity

### Key Design Principle
The catalog tracks **channels** (individual PDOEntries), not modules. Each channel has a stable UUID derived from backend + location + model.

### UUID Key Format (Backend-Specific)
```
EtherCAT: EC|{vendor_id:08X}|{product_code:08X}|REV{revision:08X}|POS{pos:04X}|{pdo_idx:04X}:{pdo_sub:02X}
I2C:      I2C|{bus:02X}|{addr:02X}|{channel:02X}
SPI:      SPI|{bus:02X}|{cs:02X}|{channel:02X}
GPIO:     GPIO|{chip:02X}|{line:04X}
```

### Why This Matters
- Same model in same slot → same key → same UUID → plug-and-play replacement
- Same model in different slot → different key → new catalog entry
- Different model in same slot → different key → new catalog entry
- Device moves between backends → UUID changes → config remap required (no code changes)

### Catalog Entry Structure
```cpp
struct CatalogEntry {
    std::string key;           // Stable identity key (backend|location|model|channel)
    std::string uuid;          // RFC-4122 v4 UUID, generated once + persisted
    std::string channelType;   // "DigitalInput" | "IMU_GyroX" | etc.
    std::string name;          // Human-readable: "MPU6050[0x68] GyroX"
    std::string slaveName;     // Short model name: "MPU6050", "EL3632"
    uint16_t    slavePos{0};   // Backend-specific position (EtherCAT slot, I2C bus, etc.)
    uint32_t    productCode{0};
    uint32_t    revisionNumber{0};
    uint16_t    pdoIndex{0};
    uint8_t     pdoSubindex{0};
    bool        isOutput{false};
};
```

---

## 5.0 Backend Adapter Pattern

### IHardwareAdapter Interface
```cpp
class IHardwareAdapter {
public:
    virtual ~IHardwareAdapter() = default;
    virtual bool initialize() = 0;
    virtual void onBeforeReadInputs()  noexcept {}  // Read from bus → PDO image
    virtual void onAfterWriteOutputs() noexcept {}  // PDO image → Write to bus
    [[nodiscard]] std::vector<PDO>& getPDOs() noexcept { return pdos_; }
protected:
    std::vector<PDO> pdos_;
};
```

### Backend Responsibilities
1. **Discover** physically present devices during `initialize()`.
2. **Register** discovered channels into the central `HardwareCatalog`.
3. **Create PDOs** with entries for each channel.
4. **Fill/drain** PDO image buffers in `onBeforeReadInputs()` / `onAfterWriteOutputs()`.
5. **Own** the PDO image memory (PDOEntry::image pointers point into backend-owned buffers).

### Backend Types
| Backend | Bus | Key Format | Phase |
|---------|-----|------------|-------|
| EthercatAdapter | EtherCAT | EC|vendor|product|REV|POS|pdo:sub | Existing |
| I2CAdapter | I2C | I2C|bus|addr|channel | Stub |
| SPIAdapter | SPI | SPI|bus|cs|channel | Stub |
| GrpcAdapter | gRPC | GRPC|channel | Existing |
| SimulatedAdapter | None | SIM|channel | Existing |

---

## 6.0 Wrapper Design Rules

### Strict Requirements
1. **No inheritance** — Each wrapper is a standalone concrete `final` class.
2. **No virtual methods** — Zero vtable overhead.
3. **Header-only** — All in `.h` files, no `.cpp`, maximum inlining.
4. **Reference semantics** — Wrappers hold `PDOEntry&` (or `PDOEntry*` for optional entries), resolved once at init.
5. **`noexcept` everywhere** — All RT-path methods are `noexcept`.
6. **Direct accessors** — `pool.output(idx)` returns reference, not wrapped in getter.
7. **Integer-indexed access** — `WrapperPool::addOutput()` returns stable `int` index.

### WrapperPool Pattern
```cpp
class WrapperPool final {
    // Separate typed vectors — no base class, no polymorphism
    std::vector<DigitalInputWrapper>  inputs_;
    std::vector<DigitalOutputWrapper> outputs_;
    std::vector<IMUWrapper>           imus_;
    // ... etc

    // Registration (init-time, before freeze)
    [[nodiscard]] int addOutput(std::string name, PDOEntry& entry);
    [[nodiscard]] int addIMU(std::string name, PDOEntry& gyroX, ...);

    // RT accessors (O(1) direct index, noexcept)
    [[nodiscard]] DigitalOutputWrapper& output(int idx) noexcept;
    [[nodiscard]] IMUWrapper& imu(int idx) noexcept;

    // Freeze pattern
    void freeze() { outputs_.shrink_to_fit(); inputs_.shrink_to_fit(); ... }
};
```

### Existing Wrapper Types
| Wrapper | Purpose | Entry Types |
|---------|---------|-------------|
| DigitalInputWrapper | Boolean input | DigitalInput |
| DigitalOutputWrapper | Boolean output | DigitalOutput |
| AnalogInputWrapper | Raw ADC input | AnalogInput |
| AnalogOutputWrapper | Raw ADC output | AnalogOutput |
| MessageInWrapper | Inbound gRPC message | MessageIn |
| MessageOutWrapper | Outbound gRPC message | MessageOut |
| IMUWrapper | Gyro + Accel (6 axes) | IMU_GyroX/Y/Z, IMU_AccelX/Y/Z |
| MagnetometerWrapper | Magnetic field (3 axes) | MagnetometerX/Y/Z |
| BarometerWrapper | Pressure + Altitude | Barometer |
| GPSWrapper | GPS position, altitude, heading, fix quality | GPS_Latitude/Longitude/Altitude/Heading/FixQuality |

---

## 7.0 PDOEntry & EntryType

### EntryType Enum
```cpp
enum class EntryType : uint8_t {
    DigitalInput  = 0,
    DigitalOutput = 1,
    Encoder       = 2,
    AnalogInput   = 3,
    AnalogOutput  = 4,
    MessageOut    = 5,
    MessageIn     = 6,
    IMU_GyroX     = 7,
    IMU_GyroY     = 8,
    IMU_GyroZ     = 9,
    IMU_AccelX    = 10,
    IMU_AccelY    = 11,
    IMU_AccelZ    = 12,
    MagnetometerX = 13,
    MagnetometerY = 14,
    MagnetometerZ = 15,
    Barometer     = 16,
    GPS_Latitude  = 17,
    GPS_Longitude = 18,
    GPS_Altitude  = 19,
    GPS_Heading   = 20,
    GPS_FixQuality = 21,
};
```

### PDOEntry Structure
- **Concrete struct** (no vtable, no inheritance).
- **Cached values** — `read()` copies from image into cached fields; `write()` copies from cached fields back to image.
- **Type-safe accessors** — `getBool()`, `getCount()`, `getRawAdc()`, `getGyroX()`, etc.
- **RT hot path** — `read()` / `write()` use `switch(type)` with no virtual dispatch.

---

## 8.0 Lifecycle & Freeze Pattern

### Init Phase (Before RT Thread Starts)
1. Construct backends and register with `HardwareRegistry::addBackend()`.
2. Call `backend->initialize()` — discovers devices, creates PDOs, registers catalog entries.
3. Call `HardwareRegistry::buildUuidMap()` — builds string→PDOEntry* map.
4. Load JSON config and resolve UUIDs to PDOEntry pointers.
5. Construct wrappers with resolved PDOEntry references.
6. Register wrappers with `WrapperPool::addOutput()`, `addIMU()`, etc.
7. Call `WrapperPool::freeze()` — `shrink_to_fit()` on all vectors.
8. Call `HardwareRegistry::freezeForRt()` — `pdo.freeze()` on all PDOs.
9. Start RT thread.

### RT Phase (After Freeze)
- **No allocation** — all vectors are frozen, no `push_back`, no `resize`.
- **No UUID lookup** — all PDOEntry pointers resolved at init.
- **No virtual dispatch** — direct member access or switch-on-enum.
- **No system calls** — only `clock_gettime` via `signalProcessTickNow()`.

---

## 9.0 Coding Conventions

### File Organization
```
src/flight_controller/
├── include/fc/
│   ├── pdo/              # PDOEntry, PDO, IHardwareAdapter, HardwareRegistry
│   ├── ethercat/         # EthercatAdapter, HardwareCatalog
│   ├── i2c/              # I2CAdapter
│   ├── spi/              # SPIAdapter
│   ├── sensor/           # SensorCatalog (if separate from HardwareCatalog)
│   ├── wrapper/          # All wrapper classes (header-only)
│   ├── app/              # Application, Queue, WrapperPool
│   ├── safety/           # AlwaysOnEval, LineMonitor, MachineStateController
│   ├── motor/            # MotorController
│   ├── grpc/             # GrpcAdapter
│   ├── services/         # GrpcService, gRPC implementations
│   └── mission/          # MissionQueue, MissionEvaluator, MissionStatePDO, FlightLeg
└── src/                  # Corresponding .cpp files (wrappers are header-only)
```

### Include Guards
- Use `#pragma once` for all headers.
- Forward declare when possible to reduce include depth.

### Namespace
- `fc::pdo` — PDO layer
- `fc::wrapper` — Wrapper classes
- `fc::app` — Application, Queue, WrapperPool
- `fc::safety` — Safety evaluators
- `fc::ethercat`, `fc::i2c`, `fc::spi` — Backend adapters
- `fc::sensor` — Sensor catalog
- `fc::mission` — Mission planning (MissionQueue, MissionEvaluator, FlightLeg)
- `common::rt` — Real-time utilities (Threadrunner, SignalProcess)
- `common::math` — Math types (Vec3f, etc.)
- `common::log` — Logging
- `imu` — IMU calibration and types (libimu)

### Naming
- Classes: `PascalCase` (e.g., `DigitalInputWrapper`, `I2CAdapter`)
- Methods: `camelCase` (e.g., `isActive()`, `setActive()`)
- Members: `camelCase_` with trailing underscore (e.g., `name_`, `entry_`)
- Enums: `PascalCase` (e.g., `EntryType`, `LatchType`)
- Constants: `kPascalCase` (e.g., `kQuadTable`)

---

## 10.0 What NOT to Do

1. **Do NOT add virtual methods to wrappers** — they are `final` concrete classes.
2. **Do NOT use `std::unordered_map` in RT path** — use sorted `std::vector` with `lower_bound` or direct array index.
3. **Do NOT allocate memory in RT path** — reserve capacity at init, freeze before RT.
4. **Do NOT use Eigen** — use `common::math::Vec3f` (plain struct, no dependency).
5. **Do NOT add inheritance hierarchies** — composition over inheritance.
6. **Do NOT use `std::function` in RT path** — direct function calls or switch dispatch.
7. **Do NOT block in RT thread** — no file I/O, no socket, no sleep.
8. **Do NOT share memory between threads without `VectorBuffer`** — RT thread owns all RT data.

---

## 11.0 Reference Codebase

The CIVControl-ARM reference codebase at `externalReferences/20260615-182435/CIVControl-ARM/` is the authoritative source for architectural patterns. When in doubt, check the reference implementation.

### Key Reference Files
- `src/application/Wrappers/InputWrappers.h` — DigitalInputWrapper, EncoderWrapper, MessageInWrapper
- `src/application/Wrappers/OutputWrappers.h` — DigitalOutputWrapper, MessageOutWrapper
- `src/application/Wrappers/WrapperPool.h` — WrapperPool pattern
- `src/application/Wrappers/DetectWrapper.h` — DetectWrapper pattern
- `src/hardware/PDO.h` — PDOEntry, PDO, EntryType
- `src/hardware/HardwareRegistry.h` — HardwareRegistry pattern
- `src/hardware/IHardwareAdapter.h` — IHardwareAdapter interface
- `src/hardware/ethercat/HardwareCatalog.h` — CatalogEntry, UUID key format
- `src/application/Application.cpp` — RT cycle flow

---

## 12.0 Build System

- **CMake 3.20+** with C++20 standard.
- **Build presets** in `CMakePresets.json` for debug/release configurations.
- **Superbuild** pattern: root CMakeLists.txt includes subdirectories in order: `common` → `imu` → `flight_controller` → `navi` → `main`.
- **Library dependencies:**
  - `common` — no external deps (rt on Linux)
  - `imu` — depends on `common`
  - `fc` — depends on `common`, `imu`
  - `navi` — depends on `fc`
  - `drone_app` — depends on `fc`, `navi`
  - `bench_test` — depends on `fc` (simulated bench test)
  - `mission_bench_test` — depends on `fc` (mission simulation)

---

## 13.0 Mission Planning Architecture

### Conceptual Mapping
The mission planning system reuses the manufacturing control architecture:
- **Conveyor Line** → Flight Path / Mission Route
- **Station** → Waypoint / Leg / Mission Phase
- **Product in Queue** → Drone itself (mission state)
- **Station Trigger** → Reach waypoint or enter geofence
- **Station Result (gRPC)** → Vision check, payload action, sensor confirmation
- **Rules Engine (AlwaysOnEval)** → Safety, contingency, leg transition rules

### MissionQueue Lifecycle
1. `MissionQueue::loadFromJson(path)` — parses mission JSON, populates `FlightLeg` vector
2. `mission->freeze()` — shrinks storage, marks RT-ready
3. RT path: `mission->currentLegIndex()`, `mission->leg(idx)`, `mission->advanceLeg()` — all `noexcept`, O(1)

### FlightLeg Data Model
```cpp
struct FlightLeg {
    std::string   name;
    std::string   uuid;
    common::math::Vec3f targetPosition;  // GPS or local ENU
    float         targetAltitude;        // MSL altitude (m)
    float         targetHeading;         // Degrees true
    float         maxSpeed;              // m/s
    LegCriterion  criterion;             // PositionReached, AltitudeReached, TimeElapsed, ImageMatch, ManualAck, Always
    float         arrivalRadius;         // meters
    float         dwellTimeSeconds;      // minimum hold time
    uint64_t      timeoutNs;             // max time for this leg
    bool          hasAction;             // gRPC action at waypoint
    int           msgOutIdx, msgInIdx;   // WrapperPool message indices
};
```

### MissionEvaluator RT Tick Pattern
```cpp
// Every RT cycle:
evaluator.tick(currentPosition, currentAltitude, nowNs);

if (evaluator.shouldAdvanceLeg(mission->currentLegIndex())) {
    mission->advanceLeg();
}
```

- Uses `switch(LegCriterion)` dispatch (no virtual calls)
- Position comparison: horizontal distance via `sqrt(dx*dx + dy*dy)`
- Dwell time: `nowNs - enterTimeNs > dwellSeconds * 1e9`
- Timeout fallback: advance on timeout (never hang)

### GPS Integration Pattern
- GPS is a **device on the other side of a backend** (UART NMEA, SPI, or I2C), NOT a backend itself
- GPS data flows: Backend reads raw data → writes into PDO image → `GPSWrapper` provides typed access
- `GPSWrapper` holds 5 `PDOEntry&` references (latitude, longitude, altitude, heading, fixQuality)
- Entry types: `GPS_Latitude`, `GPS_Longitude`, `GPS_Altitude`, `GPS_Heading`, `GPS_FixQuality`

### Mission State Machine
Extended `MachineState` enum includes mission states:
`Idle → Arming → MissionReady → Flying → Holding → RTL → Landing → Landed → MissionComplete`

### Mission gRPC Service
`mission_service.proto` defines:
- `LoadMission`, `StartMission`, `PauseMission`, `ResumeMission`, `CancelMission`
- `GetMissionStatus`, `StreamMissionProgress` (server-streaming)
