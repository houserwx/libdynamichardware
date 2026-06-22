# Copilot Agent Instructions — libdynamichardware

**Purpose:** This document instructs Copilot agents on the architectural principles, coding patterns, interface contracts, and RT determinism requirements for the libdynamichardware project. Read this before making any changes to source code.

---

## 0.1 Core Tenants

1. **RT Determinism is Paramount** — The RT hot path must be free of heap allocation, virtual calls per entry, blocking syscalls, and shared memory without proper buffering. Non-negotiable.
2. **Discovery-First Architecture** — Backends discover all available hardware via `scan()` returning pure data. Consumers select from the catalog to define active channels. No coupling between scanner internals and catalog internals.
3. **KEEP IT SIMPLE** — Respect SOLID principles but avoid unnecessary abstractions. Favor composition over inheritance, direct access over indirection. Every new interface must justify its existence with a clear SRP boundary.

---

## 1.0 RT Determinism — Non-Negotiable Rules

The RT hot path (from consumer's RT loop through `readAll()`, `writeAll()`, `DHDOEntry::read()/write()`) must satisfy:

### Red Lines (DO NOT VIOLATE)

1. **No heap allocation** reachable from `run()` after freeze completes. No `new`, `make_unique`, `push_back`, or `resize` in RT path.
2. **No `std::unordered_map`** in any RT-cycle method. UUID lookup map is init-time only; never touched during read/write cycle.
3. **No `std::mutex`, `std::lock_guard`, or `std::condition_variable`** inside the RT thread. Use `VectorBuffer` for cross-thread communication.
4. **No `virtual` call** inside a per-entry loop (`DHDOEntry` sweep). Exactly 2 virtual calls per backend per cycle total (`onBeforeReadInputs` / `onAfterWriteOutputs`).
5. **No blocking syscall** (file, socket, sleep) inside RT cycle. Only one `clock_gettime` via vDSO per cycle (`signalProcessTickNow()`).
6. **No direct shared-memory write** to RT-owned data from non-RT thread without `VectorBuffer`.
7. **Single RT thread owner** — All RT data accessed only on RT thread; cross-thread writes use `VectorBuffer` SPSC ring buffer only.

### Scoring Criteria

| Criterion | Requirement |
|---|---|
| No allocation after freeze | All `std::vector` capacity reserved before RT loop; no push_back/resize/new in RT path |
| `noexcept` on hot path | Every method in the call graph from RT loop through DHDOEntry accessors is marked `noexcept` |
| Zero virtual calls per entry | `readAll()/writeAll()` perform exactly 2 virtual calls per backend (cycle hooks); per-entry loop performs zero virtual dispatch |
| Bounded O(1) access | Wrapper pool access uses array index (O(1)); no linear scan or map lookup in hot path |
| Contiguous iteration | Entries iterated as `std::vector<DHDOEntry>` or `std::array`; backends as contiguous vector; no pointer-chasing through a map |

---

## 2.0 Architecture Layers (Top-Down)

```
┌─────────────────────────────────────────────────────────────┐
│  DynamicHardwareBuilder                                     │
│  Fluent API: enableBackend(), mapChannel(), discover(),     │
│  buildRT() — delegates to HardwareOrchestrator internally   │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│  HardwareOrchestrator                                       │
│  Phase coordination, catalog lifecycle, backend dispatch    │
│  Uses BackendRegistry for abstract creation                 │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│  IBackendScanner.scan() → pure data vectors                 │
│  Each Discovery class returns HardwareDescriptor[]          │
│  → absorbed into HardwareCatalog                            │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│  IRuntimeAdapter.build(channels) → creates DHDOEntry objects│
│  Inheriting IDHDOBuilder + adding RT cycle hooks            │
│  (onBeforeReadInputs / onAfterWriteOutputs)                 │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│  DHDOEntry / DHDO                                           │
│  Raw memory image + type-safe accessors                     │
│  EntryType bitmask enum + switch dispatch; no vtable        │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│  HardwareRegistry                                           │
│  Owns backend vector, builds UUID map                       │
│  readAll()/writeAll() — init-time lookup, RT-time iteration │
└─────────────────────────────────────────────────────────────┘
```

---

## 3.0 Backend Interface Contracts

### IBackendScanner (Discovery Layer)

```cpp
// Canonical one-shot scanner interface. Returns pure data without mutating shared state.
class IBackendScanner {
public:
    virtual ~IBackendScanner() = default;
    [[nodiscard]] virtual std::vector<HardwareDescriptor> scan() { return {}; }
protected:
    IBackendScanner() = default;
};
```

**Contract:** `scan()` returns structured descriptors of all physically present channels. Does NOT write into any catalog or registry. The orchestrator absorbs results. Default implementation returns empty vector for backward compat during transition period.

### IDHDOBuilder (Configuration → Build Layer)

```cpp
// Abstract contract: given a set of channel definitions, build DHDO process image objects
class IDHDOBuilder {
public:
    virtual ~IDHDOBuilder() = default;
    virtual void build(const std::vector<ChannelDefinition>& channels) = 0;
    [[nodiscard]] virtual const std::vector<DHDO>& getDHDOS() const noexcept = 0;
};
```

**Contract:** Called by orchestrator after discovery + mapping phases complete. Creates DHDOEntry objects and pushes them into the protected `dhdos_` vector inherited via IRuntimeAdapter friendship pattern.

### IRuntimeAdapter (Canonical RT Interface)

```cpp
// Inherits IDHDOBuilder (configuration flows through build(channels)), adds RT cycle hooks.
// Owns the frozen DHDO vector via friendship with HardwareRegistry.
class IRuntimeAdapter : public IDHDOBuilder {
public:
    virtual ~IRuntimeAdapter() = default;
    
    // Non-copyable, non-movable (owned by HardwareRegistry)
    IRuntimeAdapter(const IRuntimeAdapter&)            = delete;
    IRuntimeAdapter& operator=(const IHardwareAdapter&) = delete;
    
    /// Optional one-time init after adapter creation but before RT loop.
    virtual void initialize() noexcept {}
    
    /// Called in RT cycle BEFORE reading input channels — fill process image from bus.
    virtual void onBeforeReadInputs()  noexcept = 0;
    
    /// Called in RT cycle AFTER writing output channels — flush process image to bus.
    virtual void onAfterWriteOutputs() noexcept = 0;

protected:
    friend class HardwareRegistry;
    std::vector<DHDO> dhdos_;   // Via friendship for mutable RT sweep access

public:
    [[nodiscard]] const std::vector<DHDO>& getDHDOS() const noexcept override { return dhdos_; }
};
```

### Backend Responsibilities

1. **Discover** physically present devices during `scan()` phase — return pure data vectors.
2. **Build** DHDO entries from filtered channel definitions via `build(channels)`.
3. **Fill/drain** DHDO image buffers in `onBeforeReadInputs()` / `onAfterWriteOutputs()`.
4. **Own** the DHDO image memory (`DHDOEntry::image` pointers point into backend-owned buffers).

### Backend Types

| Backend | Discovery Class | RTBackend Class | Bus | Key Format | Phase |
|---|---|---|---|---|---|
| GPIO | GPIODiscovery | GPIORTBackend | Linux gpiod v2 | `GPIO\|chip\|line` | Active |
| EtherCAT | EthercatDiscovery | EthercatRTBackend | IgH stack | `EC\|vendor\|product\|REV\|POS\|pdo:sub` | Active |
| I2C | I2CDiscovery | I2CRTBackend | `/dev/i2c-*` | `I2C\|bus\|addr\|channel` | Stub runtime |
| SPI | SPIDiscovery | SPIRTBackend | `/dev/spidev*` | `SPI\|bus\|cs\|channel` | Stub runtime |
| Simulated | SimulatedDiscovery | SimulatedRTBackend | None (JSON defs) | `SIM\|channel_id` | Active (test fixture) |

Each backend self-registers at static init time via `BackendRegistry::registerBackend(name, creator_function)` — satisfies OCP.

---

## 4.0 RT Cycle Flow

```cpp
while (running_) {
    signalProcessTickNow();            // Update global timestamp (1 clock_gettime/cycle via vDSO)
    ctx->readAll();                    // Phase 1: All backends read inputs into DHDOEntry caches
    /* application logic here */       // Read values, run control logic, set outputs
    ctx->writeAll();                   // Phase 2: All backends flush outputs from DHDOEntry to bus
}
```

### readAll() — Two-Phase Read

```cpp
// Phase 1: Each backend reads from physical bus into DHDO image buffers
for (auto& backend : backends_) backend->onBeforeReadInputs();   // VIRTUAL CALL #N

// Phase 2: Each entry extracts typed values from image into cached fields
for (auto& backend : backends_)
    for (const auto& dhdo : backend->dhdos_)      // CONCRETE iteration, no virtual dispatch
        for (auto& entry : dhdo.entries)           // CONCRETE struct access
            if (entryIsInput(entry.type))          // constexpr bitmask check
                entry.read();                       // CONCRETE method, switch(enum) internally
```

### writeAll() — Two-Phase Write

```cpp
// Phase 1: Each entry copies cached/desired values back to image
for (auto& backend : backends_)
    for (auto& dhdo : backend->dhdos_)
        for (auto& entry : dhdo.entries)
            if (entryIsOutput(entry.type))         // constexpr bitmask check
                entry.write();                      // CONCRETE method, switch(enum) internally

// Phase 2: Each backend flushes image to physical bus
for (auto& backend : backends_) backend->onAfterWriteOutputs();   // VIRTUAL CALL #N
```

---

## 5.0 HardwareCatalog — Channel-Level Identity

### Key Design Principle

The catalog tracks **channels** (individual DHDOEntries), not modules. Each channel has a stable UUID derived from backend + location + model.

### UUID Key Format (Backend-Specific)

```
EtherCAT: EC|{vendor_id:08X}|{product_code:08X}|REV{revision:08X}|POS{pos:04X}|{pdo_idx:04X}:{pdo_sub:02X}
I2C:      I2C|{bus:02X}|{addr:02X}|{channel:02X}
SPI:      SPI|{bus:02X}|{cs:02X}|{channel:02X}
GPIO:     GPIO|{chip:02X}|{line:04X}
Simulated: SIM|{channel_id}
```

### Why This Matters

- Same model in same slot → same key → same UUID → plug-and-play replacement
- Same model in different slot → different key → new catalog entry
- Different model in same slot → different key → new catalog entry
- Device moves between backends → UUID changes → config remap required (no code changes)

---

## 6.0 DHDOEntry & EntryType Bitmask System

### EntryType — Composable Bitmask Flags

EntryType is a composable bitmask, not an exhaustive enum list. Any combination of direction | signedness | base_type | size is valid:

```cpp
// Direction (bits 0-1)
DIR_INPUT   = 0x01
DIR_OUTPUT  = 0x02

// Signedness (bit 2)
SIGNED      = 0x04

// Base type (bits 3-4)
BASE_BOOL   = 0x00
BASE_INT    = 0x08
BASE_FLOAT  = 0x10
BASE_MSG    = 0x18

// Bit size (bits 5-6)
SZ_1        = 0x00
SZ_8        = 0x20
SZ_16       = 0x40
SZ_32       = 0x60
```

**Pre-composed convenience constants:** `BoolInput`, `BoolOutput`, `Int8/16/32Input/Output`, `FloatInput/Output`, `MessageIn/Out`.

**Constexpr extractors:** `entryIsInput()`, `entryIsOutput()`, `entryValueFormat()`, `entryBitSize()` — all inlineable, zero-cost.

### DHDOEntry Structure

- **Concrete struct** (no vtable, no inheritance). All methods are compiler-inlineable.
- **Cached values** — `read()` copies from image into cached fields (`boolVal_`, `int16Val_`, etc.); `write()` copies from desired state back to image.
- **Type-safe accessors** — `getBool/setBool()`, `getInt16/setInt16()`, `getFloat/setFloat()`.
- **RT hot path** — `read()/write()` use `switch(type)` with no virtual dispatch. Compiler generates jump tables or optimized branch chains.
- **PulseMachine / DebounceMachine** composed as value members (not inherited) — single responsibility for each state machine.

---

## 7.0 Lifecycle & Freeze Pattern

### Init Phase (Before RT Thread Starts)

```
1. DynamicHardwareBuilder.enableBackend("name", config_map)
   → Orchestrator creates scanner+adapter pairs via BackendRegistry
   
2. builder.discover()
   → Each backend's scan() returns pure data vectors
   → HardwareCatalog absorbs descriptors (stable UUIDs generated/preserved)
   
3. Consumer calls builder.mapChannel(uuid, EntryType, friendly_name)
   → For non-EtherCAT backends; EtherCAT auto-maps from EEPROM PDO data
   
4. builder.buildRT()
   → PhaseManager advances DISCOVERY→MAPPING→BUILD_RT
   → Orchestrator filters channel defs per-backend, calls build(channels)
      → Each IRuntimeAdapter creates DHDOEntry objects into dhdos_ vector
      → Transfers ownership to HardwareRegistry via addBackend(unique_ptr<IRuntimeAdapter>)
   → Returns unique_ptr<DynamicHardwareContextObject>
   
5. ctx->freeze()
   → HardwareRegistry.freezeForRt(): rebuilds UUID map, freezes all DHDOs
   → State transitions ACTIVE → FROZEN
   → No structural changes allowed after this point
   
6. Cache entry pointers: ctx->lookupByUuid("uuid-string") — init-time only!
```

### RT Phase (After Freeze)

- **No allocation** — all vectors frozen, no push_back/resize/new
- **No UUID lookup** — all DHDOEntry pointers resolved at init time and cached as raw pointers
- **No virtual dispatch per entry** — direct member access or switch-on-enum for type filtering
- **No system calls** — only `clock_gettime` via `signalProcessTickNow()` (~10ns vDSO)

---

## 8.0 Coding Conventions

### File Organization

```
include/dynamichardware/
├── DynamicHardwareBuilder.h       # Primary public API entry point
├── HardwareOrchestrator.h         # Internal phase coordination
├── DynamicHardwareContextObject.h # Runtime context (freeze/read/write lifecycle)
├── dhdo/                          # Process image types + registry + interfaces
├── config/                        # PhaseManager, BackendRegistry
├── rt/                            # SignalProcess, VectorBuffer, IChannelProcessor
└── backends/{ethercat,gpio,i2c,spi,simulated}/  # Transport implementations
```

### Include Guards & Namespaces

- Use `#pragma once` for all headers.
- Forward declare when possible to reduce include depth.
- Namespace: `dynamichardware::dhdo` for process-image types; `dynamichardware::config` for phase/backend registry; `dynamichardware::rt` for RT utilities.

### Naming

| Element | Convention | Examples |
|---|---|---|
| Classes | PascalCase | `DynamicHardwareBuilder`, `GPIORTBackend` |
| Methods | camelCase | `isActive()`, `setActive()` |
| Members | camelCase_ with trailing underscore | `name_`, `entry_`, `backends_` |
| Enums | PascalCase | `EntryType`, `HardwarePhase` |
| Constants | kPascalCase or constexpr lowercase_with_underscores | `kQuadTable`, `DIR_INPUT` |

---

## 9.0 What NOT to Do

1. **Do NOT add virtual methods** to DHDOEntry — it is a concrete struct (no vtable). Type dispatch uses switch(enum) only.
2. **Do NOT use std::unordered_map in RT path** — UUID lookup map is init-time only. Use sorted std::vector with lower_bound or direct array index in hot path.
3. **Do NOT allocate memory in RT path** — reserve capacity at init, freeze before RT loop starts.
4. **Do NOT add inheritance hierarchies** — composition over inheritance everywhere except IRuntimeAdapter base class.
5. **Do NOT use std::function in RT path** — direct function calls or switch dispatch only.
6. **Do NOT block in RT thread** — no file I/O, socket operations, or sleep inside readAll/writeAll cycle.
7. **Do NOT share memory between threads without VectorBuffer** — RT thread owns all RT data; cross-thread writes MUST use SPSC ring buffer.
8. **Do NOT call lookupByUuid() from the RT loop** — resolve pointers once after freeze(), cache as raw DHDOEntry*.
9. **Do NOT modify catalog during RT phase** — HardwareCatalog is locked after BUILD_RT phase via PhaseManager enforcement.

---

## 10.0 Reference Files

### Core Interfaces (read these first when unsure about contracts)

- `include/dynamichardware/dhdo/IRuntimeAdapter.h` — Canonical RT lifecycle interface
- `include/dynamichardware/dhdo/IBackendScanner.h` — One-shot scanner contract  
- `include/dynamichardware/dhdo/DHDO.h` — Process image types (DHDOEntry struct, EntryType bitmask)
- `include/dynamichardware/dhdo/HardwareRegistry.h` — Backend ownership + RT sweep orchestration

### Public API

- `include/dynamichardware/DynamicHardwareBuilder.h` — Fluent consumer-facing entry point
- `include/dynamichardware/HardwareOrchestrator.h` — Internal coordination layer

### Working Examples

- `examples/simulated_io_demo.cpp` — Full lifecycle with simulated backend (no hardware needed)
- `examples/gpio_correct_demo.cpp` — GPIO discovery → mapping → build → freeze → RT loop
- `examples/ethercat_demo.cpp` — EtherCAT-specific flow (auto-mapping from EEPROM)
