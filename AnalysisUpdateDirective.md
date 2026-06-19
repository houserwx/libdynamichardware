# Analysis Update Directive — libdynamichardware

**Purpose:** This document instructs Copilot agents on exactly how and when to
update `doc/CurrentAnalysis.md` so the architecture analysis always reflects
the current committed state of `libdynamichardware`.

This is a **library**, not an application.  It provides the RT-safe hardware
abstraction primitives that consumers (e.g. CIVControl-ARM) build on.  The
scoring criteria reflect library concerns: clean API surface, correct RT
guarantees in the hot path, proper layering, and consumer safety.

---

## When to Update CurrentAnalysis.md

Update the analysis after any commit that does one or more of the following:

- Adds, removes, or restructures a source/header in `src/` or `include/`
- Changes the RT cycle path in `HardwareRegistry.cpp` (`readAll`/`writeAll`)
- Changes `PDO.cpp` (`PDOEntry::read`/`write`, `PDO::freeze`)
- Modifies `IHardwareAdapter` interface or any concrete adapter
- Changes `DynamicHardwareContext` facade (lifecycle, builder, public methods)
- Changes `PDOFactory` or `HardwareCatalog` (entry creation, UUID management)
- Changes RT utilities (`SignalProcess.h`, `VectorBuffer.h`)
- Changes namespace structure or public include paths (affects consumers)
- Adds or removes a file from `CMakeLists.txt`

Do **not** update for documentation-only commits, comment changes, formatting,
or build/CI script changes unless they reveal a design issue.

---

## Library Layers

| Layer | Files | Responsibility |
|---|---|---|
| **Facade** | `DynamicHardwareContext.h/.cpp`, `SimulatedDefinitionBuilder` | Single public entry point. Builder pattern (`withEthercat`, `withGPIO`, etc.), lifecycle state machine (`PRE_BUILD→BUILT→FROZEN→SHUTDOWN`). `registry()` and `catalog()` are **private** — consumers interact only through facade methods. |
| **PDO** | `PDO.h/.cpp`, `PDOFactory.h/.cpp` | `PDOEntry` — concrete struct, no vtable. Typed accessors (`getBool/setBool`, `getCount`, `getRawAdc/setRawAdc`, IMU/GPS/float). `PDO` — owns image buffer + entry vector. `PDOFactory` — static factory for PDOEntry creation from catalog entries. |
| **Registry** | `HardwareRegistry.h/.cpp` | Owns backend vector, orchestrates RT cycle (`readAll`/`writeAll` with entry type filtering), UUID→PDOEntry* lookup map, `freezeForRt` coordinator. |
| **Catalog** | `HardwareCatalog.h/.cpp` | Backend-agnostic channel metadata. Stable UUIDs, JSON persistence (`load`/`save`), discovery metadata (key format supports EC/I2C/SPI/GPIO/GRPC). |
| **Adapters** | `backends/{ethercat,gpio,i2c,spi,simulated}/` | `IHardwareAdapter` — abstract interface. Concrete backends implement `initialize()`, `onBeforeReadInputs()`, `onAfterWriteOutputs()`. Only 2 virtual calls per backend per cycle. |
| **RT Utils** | `rt/SignalProcess.h`, `rt/VectorBuffer.h` | `signalProcessTickNow` — cached CLOCK_MONOTONIC timestamp. `PulseMachine`, `DebounceMachine` — one-shot pulse and debouncing state machines. `VectorBuffer` — lock-free SPSC ring buffer for cross-thread comms. |

---

## How to Evaluate — Criteria and Scoring

### Dimension 1: RT Determinism (weighted 55%)

Score 0–10. Each row evaluated independently.

| Criterion | Pass condition | Failure condition | Weight |
|---|---|---|---|
| **No allocation after freeze** | No `new`, `make_unique`, `push_back`, `resize` reachable from `readAll()`, `writeAll()` | Any allocation in RT hot path | High |
| **`noexcept` on all hot-path methods** | `readAll()`, `writeAll()`, `PDOEntry::read()`, `PDOEntry::write()`, and all accessors are `noexcept` | Non-`noexcept` in RT chain | High |
| **Zero virtual calls per entry in sweep** | `readAll`/`writeAll` call exactly 2 virtual methods per backend (cycle hooks); per-entry loop calls only concrete `PDOEntry::read/write` | Any virtual or `std::function` inside per-entry loop | Very High |
| **Bounded O(1) lookup** | `lookupByUuid` uses hash map; entry iteration is contiguous vector scan | Linear scan for lookup; unbounded loop | High |
| **No syscalls in hot path** | No `clock_gettime`, file I/O, socket calls in `readAll`/`writeAll`. Consumer calls `signalProcessTickNow()` once per cycle before `readAll()` | Any syscall per cycle in library hot path | High |
| **No locks in hot path** | No `std::mutex`, `std::atomic`, or spinlock in `readAll`/`writeAll` paths | Any locking in RT sweep | Very High |
| **Contiguous iteration** | Entries as `std::vector<PDOEntry>`; backends as `std::vector<unique_ptr>` | Linked list or map iteration in hot path | Medium |
| **Entry type filtering** | `readAll()` only reads input types (DI/Encoder/AI); `writeAll()` only writes output types (DO/AO) | Blind read/write on all entry types | Medium |

**Scoring guide:**
- All pass → 9.5–10.0
- One High fails → −0.4–0.6
- One Very High fails → −0.8–1.2
- Two+ failures → <8.5; document urgently

---

### Dimension 2: SOLID Principles (weighted 45%)

Score each principle 0–10. Final = average.

#### S — Single Responsibility

Every class has one reason to change:
- `PDOEntry` = data access only (delegates pulse/debounce to composed state machines → OK)
- `DynamicHardwareContext` = lifecycle + facade only (no transport logic → OK)
- `HardwareCatalog` = metadata persistence only (no RT logic → OK)
- `HardwareRegistry` = cycle orchestration only (no transport protocol → OK)

Excellent (9–10): Every class has one clearly stated responsibility.
Acceptable (7–8): Minor dual purpose, bounded and documented.
Concern (< 7): Class owns both policy and mechanism at same layer.

#### O — Open/Closed

New backend, channel type, or entry type should be **additive only**:
- New `IHardwareAdapter` subclass → no changes needed to existing code
- New `EntryType` enum value → may require updating type filter in `readAll`/`writeAll` (document as known trade-off)
- New catalog key format → handled by string-based key system

Excellent: New feature = new file or enum case only.
Concern: New feature requires editing existing switch/type-filter code.

#### L — Liskov Substitution

All `IHardwareAdapter` subclasses must be drop-in substitutable:
- No subclass throws where base promises `noexcept`
- No subclass has stronger preconditions than base
- `initialize()` returning false is the expected failure path (not exception)

#### I — Interface Segregation

No consumer forced to depend on unused methods:
- `IHardwareAdapter` has minimal surface: `initialize`, `onBeforeReadInputs`, `onAfterWriteOutputs`, `getPDOs`
- `PDOEntry` exposes all typed accessors regardless of `EntryType` (minor — type field is truth; consumer checks type)
- Facade does not expose internal layers (`registry()`/`catalog()` are private)

#### D — Dependency Inversion

High-level code depends on abstractions, not concrete types:
- `DynamicHardwareContext.h` should only include `PDO.h`, `HardwareRegistry.h`, `HardwareCatalog.h` (no backend-specific headers)
- `HardwareRegistry.h` should not include `EthercatAdapter.h`
- `IHardwareAdapter.h` should not include any concrete backend
- Consumer application code should only `#include "dynamichardware/DynamicHardwareContext.h"`

---

## Structure of CurrentAnalysis.md

```
1. Header block (project, date, branch, commit hash, evaluator)
2. Overall Score (facade · pdo · registry · adapters · rt_utils composites)
3. Layer-by-Layer Analysis
   3a. Facade (DynamicHardwareContext, SimulatedDefinitionBuilder)
   3b. PDO Layer (PDOEntry, PDO, PDOFactory)
   3c. Registry Layer (HardwareRegistry)
   3d. Catalog Layer (HardwareCatalog)
   3e. Adapter Layer (IHardwareAdapter + ethercat/gpio/i2c/spi/simulated)
   3f. RT Utilities (SignalProcess, VectorBuffer)
4. RT Hot-Path Profile (call graph from readAll→writeAll, virtual call count,
   allocations per cycle, syscalls per cycle)
5. SOLID Summary table
6. Open Items table
7. Score Summary table
8. Verdict paragraph
```

---

## How to Read the Codebase Before Scoring

Run these searches. Each answers a scoring question:

```bash
# 1. Allocations in RT hot-path files
grep -rn "new \|make_unique\|make_shared\|push_back\|resize\|emplace_back" \
     src/dynamichardware/pdo/HardwareRegistry.cpp \
     src/dynamichardware/pdo/PDO.cpp

# 2. Non-noexcept methods in RT chain
grep -n "void\|bool\|int\|float" \
     include/dynamichardware/pdo/PDO.h \
     include/dynamichardware/pdo/HardwareRegistry.h \
     include/dynamichardware/backends/*/IHardwareAdapter.h \
     | grep -v noexcept

# 3. Virtual calls in RT sweep
grep -rn "virtual\|override" \
     include/dynamichardware/pdo/IHardwareAdapter.h \
     include/dynamichardware/backends/*/

# 4. unordered_map in RT files (acceptable for UUID lookup; flag if in per-entry loop)
grep -rn "unordered_map\|unordered_set" src/dynamichardware/

# 5. Freeze pattern integrity
grep -rn "freeze\|shrink_to_fit\|frozen_" src/dynamichardware/

# 6. Entry type filtering in readAll/writeAll
grep -A10 "void.*readAll\|void.*writeAll" src/dynamichardware/pdo/HardwareRegistry.cpp

# 7. Facade exposes internals? (registry/catalog should be private)
grep -n "registry()\|catalog()" include/dynamichardware/DynamicHardwareContext.h

# 8. Include graph — does facade include backend headers?
grep "#include" include/dynamichardware/DynamicHardwareContext.h
```

---

## Scoring Anchors (reference baselines)

| Layer | RT baseline | SOLID baseline | Justification |
|---|---|---|---|
| Facade | N/A | 9.0 | Single public entry point, builder pattern, private internals, clean lifecycle. Scored SOLID only — no own RT path. |
| PDO (Entry/PDO/Factory) | 9.7 | 9.5 | Concrete struct (no vtable), all accessors noexcept, inlineable read/write, factory decouples creation |
| Registry | 9.5 | 9.3 | 2 virtual/backend cycle, zero-virtual entry sweep, freeze pattern, contiguous iteration |
| Catalog | 8.5 | 9.0 | JSON persistence, stable UUIDs, backend-agnostic keys. RT score lower — not on hot path, but allocates on save/load |
| Adapters | 9.0 | 8.8 | Virtual hooks at backend level only. Score lowered — some adapters have init/RT dual concerns in same class |
| RT Utilities | 9.8 | 9.5 | vDSO timestamp, no allocation, no lock, lock-free SPSC buffer, state machines are pure value types |

---

## Red-Line Rules

If any red line is violated the verdict must read
**"DO NOT SHIP — RT regression"** and implementation stops until resolved.

1. Any heap allocation reachable from `readAll()` or `writeAll()` after
   `freezeForRt()` completes.
2. Any `std::mutex`, `std::lock_guard`, or `std::condition_variable` in
   `readAll()` or `writeAll()`.
3. Any `virtual` or `std::function` call inside a per-entry loop in
   `readAll()` or `writeAll()` (the 2 backend hooks are the allowed exception).
4. Any blocking syscall (file, socket, `sleep`, `clock_gettime`) inside
   `readAll()` or `writeAll()`.
5. `DynamicHardwareContext` exposes `registry()` or `catalog()` as **public**
   (breaks facade invariant — consumers bypass lifecycle guards).
6. `PDO::freeze()` not called before RT loop starts (image pointers may be
   stale after vector reallocation).
7. `PDOEntry::read()` called on output types or `PDOEntry::write()` called on
   input types (data corruption risk).

---

## Commit the Updated Analysis

After updating `doc/CurrentAnalysis.md`:

```
docs(analysis): update CurrentAnalysis for [brief change description]

- [bullet: what changed architecturally]
- [bullet: which score changed and why]
- [bullet: any new open items added or closed]
```
