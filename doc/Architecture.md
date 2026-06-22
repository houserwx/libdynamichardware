# libdynamichardware — Architecture Reference

**Version:** 2.0 (Post SOLID Refactoring — June 2026)  
**Status:** Current implementation after Phases 1–9 complete

---

## 1. Design Goals

- **Deterministic real-time control** — Zero heap allocation, zero virtual dispatch per entry in the RT hot path
- **Pluggable backends** — EtherCAT, GPIO, I2C, SPI, Simulated; each split into Discovery + RTBackend classes
- **Discovery-first architecture** — Backends scan all available hardware and populate a central catalog before any channels are activated
- **SOLID-compliant interfaces** — Small, focused contracts; no god classes or monolithic interfaces
- **Phase-enforced lifecycle** — Strict ordering: Discover → Map → Build RT → Freeze → Run → Shutdown

---

## 2. Initialization Flow

```
┌─────────────────────────────────────────────────────────────────┐
│  PHASE 1: DISCOVERY                                             │
│  DynamicHardwareBuilder.enableBackend()                         │
│    → HardwareOrchestrator creates scanner+adapter pairs          │
│      → Each backend's IBackendScanner.scan() returns pure data   │
│        → HardwareCatalog absorbs descriptors (stable UUIDs)      │
└──────────────────────────────┬──────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│  PHASE 2: CHANNEL MAPPING                                       │
│  Consumer calls builder.mapChannel(uuid, EntryType, name)       │
│    → Orchestrator accumulates ChannelDefinition list             │
│    (EtherCAT skips this step — auto-maps from EEPROM PDO data)   │
└──────────────────────────────┬──────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│  PHASE 3: BUILD RT                                              │
│  builder.buildRT() delegates to orchestrator                    │
│    → PhaseManager advances DISCOVERY→MAPPING→BUILD_RT           │
│    → For each enabled backend:                                   │
│      - Filter channel defs by backend type                       │
│      - Call IRuntimeAdapter::build(filtered_channels)            │
│        → Backend creates DHDOEntry objects, pushes into dhdos_   │
│      - Transfer ownership via registry.addBackend(backend_ptr)   │
│    → Returns unique_ptr<DynamicHardwareContextObject>            │
└──────────────────────────────┬──────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│  PHASE 4: FREEZE                                                │
│  ctx->freeze()                                                   │
│    → HardwareRegistry.freezeForRt():                             │
│      - Rebuilds UUID → DHDOEntry* lookup map                     │
│      - Calls DHDO::freeze() on all backends (shrink_to_fit +     │
│        re-base image pointers; skips EtherCAT which owns memory)  │
│      - Sets frozen_ = true; addBackend() now throws              │
│    → State transitions ACTIVE → FROZEN                           │
└──────────────────────────────┬──────────────────────────────────┘
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│  PHASE 5: RT LOOP                                               │
│  while(running):                                                │
│    signalProcessTickNow();  // vDSO clock_gettime (~10ns)       │
│    ctx->readAll();         // All inputs from hardware           │
│    /* application logic */                                       │
│    ctx->writeAll();        // All outputs to hardware            │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. Component Responsibilities

| Component | Responsibility | Key Interface / Methods |
|---|---|---|
| **DynamicHardwareBuilder** | High-level fluent API; consumer-facing entry point | `enableBackend()`, `mapChannel()`, `discover()`, `buildRT()` |
| **HardwareOrchestrator** | Internal phase coordination, backend dispatch, catalog lifecycle | `runDiscoveryScan()`, delegates to scanners/adapters via interfaces |
| **PhaseManager** | Lifecycle state machine enforcement (strict forward-only transitions) | `advance(target_phase)` — throws on illegal transition |
| **IBackendScanner** | One-shot discovery scanner; returns pure data vectors | `scan()` → `vector<HardwareDescriptor>` |
| **IRuntimeAdapter** | Canonical RT lifecycle interface; inherits IDHDOBuilder | `build(channels)`, `onBeforeReadInputs()`, `onAfterWriteOutputs()` |
| **IDHDOBuilder** | Configuration → process image construction contract | Pure virtual for adapter build pattern |
| **HardwareRegistry** | Owns backend vector, orchestrates RT cycle, UUID lookup map | `addBackend()`, `readAll()`, `writeAll()`, `lookupByUuid()` |
| **DHDOEntry** | Concrete typed I/O channel — zero vtable, cached values, type-safe accessors | `read()/write()`, `getBool/setBool()`, `getInt16/setInt16()`, etc. |
| **DHDO** | Owns image buffer + entry vector; freezes for immutable RT operation | `freeze()` shrinks storage and re-bases pointers |
| **HardwareCatalog** | JSON-persisted metadata store with stable UUIDs across restarts | `load()`, `save()`, entries indexed by key and UUID |
| **DynamicHardwareContextObject** | Runtime context (freeze/read/write/shutdown lifecycle) | `freeze()`, `readAll()`, `writeAll()`, `shutdown()` |

---

## 4. Interface Design (Post-Refactoring)

### Discovery Layer — IBackendScanner

```cpp
class IBackendScanner {
public:
    virtual ~IBackendScanner() = default;
    [[nodiscard]] virtual std::vector<HardwareDescriptor> scan() { return {}; }
protected:
    IBackendScanner() = default;
};
```

Each backend's Discovery class inherits this and returns pure data without mutating any shared state. The orchestrator absorbs the results into the catalog. This eliminates the old pattern where `discover()` directly wrote into a shared `catalog_` pointer, coupling scanner internals to catalog internals.

### Configuration Layer — IDHDOBuilder

```cpp
// Abstract contract: given a set of channels, build DHDO process image objects
class IDHDOBuilder {
public:
    virtual ~IDHDOBuilder() = default;
    virtual void build(const std::vector<ChannelDefinition>& channels) = 0;
    [[nodiscard]] virtual const std::vector<DHDO>& getDHDOS() const noexcept = 0;
};
```

### Runtime Layer — IRuntimeAdapter

```cpp
class IRuntimeAdapter : public IDHDOBuilder {
public:
    virtual ~IRuntimeAdapter() = default;
    virtual void initialize() noexcept {}
    virtual void onBeforeReadInputs()  noexcept = 0;
    virtual void onAfterWriteOutputs() noexcept = 0;
protected:
    friend class HardwareRegistry;
    std::vector<DHDO> dhdos_;   // Via friendship with HardwareRegistry for RT sweep access
public:
    [[nodiscard]] const std::vector<DHDO>& getDHDOS() const noexcept override { return dhdos_; }
};
```

Each runtime backend (GPIORTBackend, SimulatedRTBackend, etc.) inherits `IRuntimeAdapter` only. The dual-inheritance compatibility layer from Phases 3–8 has been removed in Phase 9.

---

## 5. Backend Structure Pattern

Each transport type is split into two classes following the discovery/RT separation:

| Transport | Discovery Class (`IBackendScanner`) | RT Backend Class (`IRuntimeAdapter`) |
|---|---|---|
| GPIO | `GPIODiscovery` | `GPIORTBackend` |
| EtherCAT | `EthercatDiscovery` | `EthercatRTBackend` |
| I2C | `I2CDiscovery` | `I2CRTBackend` |
| SPI | `SPIDiscovery` | `SPIRTBackend` |
| Simulated | `SimulatedDiscovery` | `SimulatedRTBackend` |

**Self-registration:** Each backend module registers itself at static init time via `BackendRegistry::registerBackend(name, creator_function)`. This satisfies OCP — adding a new backend requires zero changes to factory/orchestrator code.

---

## 6. RT Hot-Path Profile

### Call Graph: readAll() → writeAll()

```
signalProcessTickNow()           // Consumer calls once per cycle (vDSO, ~10ns)
  └─ clock_gettime(CLOCK_MONOTON)  // vDSO path — no actual syscall

readAll() noexcept               // HardwareRegistry
  └─ for each backend in backends_:
      │
      ├─ onBeforeReadInputs()     // VIRTUAL CALL #1 per backend
      │   └─ (backend fills DHDO image buffers from hardware)
      │
      └─ for each dhdo in backend->dhdos_:
          └─ for each entry e in dhdo.entries:
              ├─ type check (input types only)
              └─ e.read() noexcept  // CONCRETE — no virtual dispatch
                  └─ switch(type):
                      ├─ BoolInput: bit extract + debounce.filter()
                      ├─ Int8/16/32Input: memcpy N bytes into cache
                      └─ FloatInput: memcpy 4 bytes

writeAll() noexcept              // HardwareRegistry
  └─ for each backend in backends_:
      │
      ├─ for each dhdo in backend->dhdos_:
      │   └─ for each entry e in dhdo.entries:
      │       ├─ type check (output types only)
      │       └─ e.write() noexcept  // CONCRETE — no virtual dispatch
      │           └─ switch(type):
      │               ├─ BoolOutput: pulse.tick() + bit set/clear
      │               ├─ Int8/16Output: memcpy N bytes to image
      │               └─ FloatOutput: memcpy 4 bytes to image
      │
      └─ onAfterWriteOutputs()    // VIRTUAL CALL #2 per backend
          └─ (backend flushes DHDO images to physical hardware)
```

### Virtual Call Budget Per Cycle

| Operation | Count | Notes |
|---|---|---|
| `onBeforeReadInputs()` | **N** (one per backend) | Backend fills process image from bus |
| `onAfterWriteOutputs()` | **N** (one per backend) | Backend flushes process image to bus |
| `DHDOEntry::read()` / `write()` | **0** (concrete, inlineable) | Zero vtable overhead per entry; switch-on-enum only |
| **Total virtual calls per cycle** | **2 × backend_count** | Typically 2–10 in practice |

---

## 7. RT Determinism Guarantees

| Criterion | Status | Evidence |
|---|---|---|
| No heap allocation after freeze | ✅ PASS | No `new`, `make_unique`, `push_back`, or `resize` reachable from RT path |
| `noexcept` on all hot-path methods | ✅ PASS | `readAll()`, `writeAll()`, `DHDOEntry::read()/write()`, all accessors are `noexcept` |
| Zero virtual dispatch per entry | ✅ PASS | Per-entry loop calls concrete `DHDOEntry::read/write` only; type filter is constexpr bitmask check + switch(enum) |
| Bounded O(1) lookup at init time | ✅ PASS | UUID → DHDOEntry* uses `std::unordered_map` (init-time only; never touched during RT cycle) |
| Contiguous iteration | ✅ PASS | Entries as `std::vector<DHDOEntry>`; backends as `std::vector<unique_ptr<IRuntimeAdapter>>` — cache-friendly sequential access |
| Single syscall per cycle | ✅ PASS | Exactly one `clock_gettime(CLOCK_MONOTON)` via vDSO (~10ns userspace-only); no file I/O, socket, or OS call in RT path |

---

## 8. SOLID Compliance Assessment

| Principle | Score | Notes |
|---|---|---|
| **S — Single Responsibility** | 9.0/10 | Builder = fluent API, Orchestrator = phase coordination, Registry = RT sweep, Catalog = metadata storage. Minor exception: each backend class owns both discovery and RT logic (accepted trade-off — I/O handles must be shared between phases). |
| **O — Open/Closed** | 8.5/10 | New backend = new scanner+adapter pair registered via BackendRegistry (zero factory changes). New EntryType requires updating type filter switches in readAll/writeAll (known trade-off for zero-vtable design). |
| **L — Liskov Substitution** | 9.5/10 | All IRuntimeAdapter subclasses are drop-in substitutable. RT hooks are `noexcept` on all implementations. PhaseManager enforces lifecycle at runtime. |
| **I — Interface Segregation** | 9.0/10 | Small focused interfaces: IBackendScanner (scan→data), IDHDOBuilder (build→image), IRuntimeAdapter (cycle hooks + dhdos_). No fat interfaces. DHDOEntry exposes all typed accessors regardless of EntryType (minor — type field is truth; consumers check type at init time). |
| **D — Dependency Inversion** | 9.0/10 | HardwareRegistry depends on IRuntimeAdapter only (no concrete backends). Orchestrator uses BackendRegistry for abstract creation. Builder header includes only dhdo/config headers (no backend-specific types). |

---

## 9. Terminology Note: "DHDO" vs Transport-Specific PDOs

The term **PDO** means different things at different layers:

| Layer | Meaning | Contiguous? | Selection model |
|---|---|---|---|
| **EtherCAT transport** | Process Data Object — hardware-mapped DMA buffer, defined by slave EEPROM | **Yes** — one flat memory block across all slaves | Register ALL entries; can't cherry-pick without breaking layout |
| **This library** (`dhdo::DHDO`) | Abstract container holding `entries[]` + `image[]`, used by every backend | Depends on backend implementation | Consumer maps channels explicitly (except EtherCAT) |

When reading code, "building DHDOs" refers to our library-level abstraction. The underlying transport may or may not use actual PDO mappings (EtherCAT does, everything else doesn't).
