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
- Changes `DHDO.cpp` (`DHDOEntry::read`/`write`, `DHDO::freeze`)
- Modifies `IBackendScanner`, `IDHDOBuilder`, `IRuntimeAdapter`, or any concrete backend
- Changes `DynamicHardwareBuilder`, `HardwareOrchestrator`, or `DynamicHardwareContextObject` (lifecycle, builder, public methods)
- Changes `DHDOFactory` or `HardwareCatalog` (entry creation, UUID management)
- Changes RT utilities (`SignalProcess.h`, `VectorBuffer.h`)
- Changes namespace structure or public include paths (affects consumers)
- Adds or removes a file from `CMakeLists.txt`

Do **not** update for documentation-only commits, comment changes, formatting,
or build/CI script changes unless they reveal a design issue.

---

## Library Layers

| Layer | Files | Responsibility |
|---|---|---|
| **Builder API** | `DynamicHardwareBuilder.h/.cpp`, `DynamicHardwareContextFactory.h/.cpp` | Consumer-facing fluent API. New pattern: `.enableBackend()/.mapChannel()/discover()/buildRT()`. Legacy factory still coexists for migration compatibility. |
| **Orchestrator** | `HardwareOrchestrator.h/.cpp`, `config/PhaseManager.h/.cpp` | Internal phase coordination. Enforces DISCOVERY→MAPPING→BUILD_RT ordering via PhaseManager. Dispatches scan() and build(channels) to concrete backends. |
| **Runtime Context** | `DynamicHardwareContextObject.h/.cpp`, `SimulatedDefinitionBuilder` | Pure RT lifecycle object returned by buildRT(). State machine (ACTIVE→FROZEN→SHUTDOWN). `registry()` and `catalog()` are **private** inside Impl struct — consumers interact only through facade methods. |
| **DHDO** | `dhdo/DHDO.h/.cpp`, `dhdo/DHDOFactory.h/.cpp` | `DHDOEntry` — concrete struct, no vtable. Typed accessors (`getBool/setBool`, `getInt32/getInt16`, `getFloat/setFloat`). Composable bitmask EntryType system with constexpr extractors. `DHDO` — owns image buffer + entry vector with freeze semantics. `DHDOFactory` — static helpers for string↔EntryType conversion and UUID generation. |
| **Registry** | `dhdo/HardwareRegistry.h/.cpp` | Owns backend vector of unique_ptr<IRuntimeAdapter>, orchestrates RT cycle (`readAll`/`writeAll` with bitmask-based entry type filtering), UUID→DHDOEntry* lookup map, `freezeForRt` coordinator. |
| **Catalog** | `dhdo/HardwareCatalog.h/.cpp` | Backend-agnostic channel metadata. Stable deterministic UUIDs from SHA-256 hash of backend-specific canonical strings (variant-typed: EthercatBackendData/GpioBackendData/etc.). JSON persistence via nlohmann/json. Discovery purge cycle removes stale entries on hardware changes. |
| **Interfaces** | `dhdo/IBackendScanner.h`, `dhdo/IDHDOBuilder.h`, `dhdo/IRuntimeAdapter.h` | Three-interface split (Phase 9 ISP fix): Scanner returns pure data vectors without mutating state; Builder constructs DHDO objects from parameter-passed MappedChannel list; RuntimeAdapter inherits builder surface + adds RT lifecycle hooks (`onBeforeReadInputs`/`onAfterWriteOutputs`). |
| **Backends** | `backends/{ethercat,gpio,i2c,spi,simulated}/` | Per-transport two-class pattern: `{Transport}Discovery` implements IBackendScanner; `{Transport}RTBackend` implements IRuntimeAdapter. Discovery discarded after build phase; only RT adapters survive into frozen mode. Only 2 virtual calls per backend per cycle (`onBeforeReadInputs` / `onAfterWriteOutputs`). |
| **RT Utils** | `rt/SignalProcess.h`, `rt/VectorBuffer.h`, `rt/IChannelProcessor.h` | `signalProcessTickNow` — cached CLOCK_MONOTONIC timestamp (~10ns vDSO). `PulseMachine`, `DebounceMachine` — one-shot pulse and debouncing state machines as zero-allocation value types. `VectorBuffer` — lock-free SPSC ring buffer for cross-thread comms. |

---

## How to Evaluate — Criteria and Scoring

### Dimension 1: RT Determinism (weighted 55%)

Score 0–10. Each row evaluated independently.

| Criterion | Pass condition | Failure condition | Weight |
|---|---|---|---|
| **No allocation after freeze** | No `new`, `make_unique`, `push_back`, `resize` reachable from `readAll()`, `writeAll()` | Any allocation in RT hot path | High |
| **`noexcept` on all hot-path methods** | `readAll()`, `writeAll()`, `DHDOEntry::read()`, `DHDOEntry::write()`, and all typed accessors are `noexcept` | Non-`noexcept` in RT chain | High |
| **Zero virtual calls per entry in sweep** | `readAll`/`writeAll` call exactly 2 virtual methods per backend (IRuntimeAdapter hooks); per-entry loop calls only concrete `DHDOEntry::read/write` struct methods | Any virtual or `std::function` inside per-entry loop | Very High |
| **Bounded O(1) lookup** | `lookupByUuid` uses unordered_map; entry iteration is contiguous vector scan — init-time only, never called during RT cycle | Linear scan for lookup; map accessed in RT loop | High |
| **No syscalls in hot path** | No `clock_gettime`, file I/O, socket calls in `readAll`/`writeAll`. Consumer calls `signalProcessTickNow()` once per cycle before `readAll()` (~10ns vDSO) | Any syscall per cycle in library hot path | High |
| **No locks in hot path** | No `std::mutex`, `std::atomic`, or spinlock in `readAll`/`writeAll` paths. Non-atomic global timestamp relies on single-thread invariant discipline only. | Any locking primitive in RT sweep | Very High |
| **Contiguous iteration** | Entries as `std::vector<DHDOEntry>`; backends as `std::vector<unique_ptr<IRuntimeAdapter>>` | Linked list or map iteration in hot path | Medium |
| **Entry type filtering via bitmask** | `readAll()` uses constexpr `isInputEntryType()` checking direction bits (future-proof against new EntryType additions); `writeAll()` uses `isOutputEntryType()`. Message types excluded from both sweeps. | Blind read/write on all entry types; hardcoded switch requiring edits for new types | Medium |

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
- `DHDOEntry` = data access from image buffer only (delegates pulse/debounce to composed value-type state machines → OK)
- `DynamicHardwareBuilder` = fluent consumer API only (delegates coordination to HardwareOrchestrator → OK)
- `HardwareOrchestrator` = phase enforcement + backend dispatch only (deferred OCP fix: still has hardcoded if-blocks per backend type)
- `DynamicHardwareContextObject` = runtime lifecycle + facade only (no transport logic → OK)
- `HardwareCatalog` = metadata persistence only (no RT logic → OK)
- `HardwareRegistry` = cycle orchestration only (no transport protocol → OK)

Excellent (9–10): Every class has one clearly stated responsibility.
Acceptable (7–8): Minor dual purpose, bounded and documented.
Concern (< 7): Class owns both policy and mechanism at same layer.

#### O — Open/Closed

New backend, channel type, or entry type should be **additive only**:
- New transport implementing IBackendScanner + IRuntimeAdapter → interface contracts are closed; but orchestrator's hardcoded if-blocks require source edits for instantiation (**known deferred OCP violation**) — future fix: BackendRegistry plugin-style registration pattern
- New EntryType via composable bitmask flags (direction | signedness | base | size) → automatically swept by readAll/writeAll because filtering uses constexpr direction-bit extraction. No code changes needed for new combinations of existing flags.
- New catalog key format → handled by variant-typed BackendSpecificData and string-based canonical hash system

Excellent: New feature = new file or enum case only.
Concern: New feature requires editing existing switch/type-filter code.

#### L — Liskov Substitution

All IRuntimeAdapter subclasses must be drop-in substitutable:
- All concrete RT hooks (`onBeforeReadInputs`, `onAfterWriteOutputs`) honor noexcept contract from pure-virtual base declarations
- No backend has stronger preconditions than IRuntimeAdapter base interface
- `build(channels)` returning false is the expected failure path (not exception)
- Copy/move operations deleted on IRuntimeAdapter prevent accidental copies in registry vectors

#### I — Interface Segregation (✓ Resolved in Phase 9 refactoring)

Three focused contracts replace old monolithic design:
- **IBackendScanner**: pure-data discovery only. `scan()` returns std::vector<HardwareDescriptor> without mutating shared state. Discarded after build phase.
- **IDHDOBuilder**: configuration via parameter passing. `build(channels)` constructs DHDO objects from MappedChannel list. No global mutation during virtual call.
- **IRuntimeAdapter**: inherits IDHDOBuilder surface + adds exactly 2 pure-virtual RT hooks (`onBeforeReadInputs`/`onAfterWriteOutputs`). Owns frozen PDO vector as protected member accessible to HardwareRegistry via friendship.
- Discovery objects discarded after scan phase; only RT adapters survive into frozen mode
- Minor: DHDOEntry exposes all typed accessors regardless of EntryType bitmask value (type field is truth; consumers check type before calling)

#### D — Dependency Inversion

High-level code depends on abstractions, not concrete types:
- `DynamicHardwareBuilder.h` and `DynamicHardwareContextObject.h` should include only dhdo/ layer headers — no backend-specific includes in public API surfaces
- `HardwareRegistry.h` includes only IRuntimeAdapter.h (no EthercatRTBackend or other concrete adapter headers)
- Interface header files (IBackendScanner, IDHDOBuilder, IRuntimeAdapter) are self-contained with zero knowledge of concrete backends
- Orchestrator.cpp includes all concrete Discovery+RTBackend headers for instantiation; this is acceptable as implementation-only dependency that doesn't leak to consumers
- Consumer applications depend solely on forwarding header: `#include "dynamichardware/DynamicHardwareContext.h"`

---

## Structure of CurrentAnalysis.md

```
1. Header block (project, date, branch, commit hash, evaluator)
2. Overall Score (builder+orchestrator · dhdo · registry · catalog · backends · rt_utils composites)
3. Layer-by-Layer Analysis
   3a. Builder + Orchestrator (DynamicHardwareBuilder, HardwareOrchestrator, PhaseManager)
   3b. DHDO Layer (DHDOEntry, DHDO, DHDOFactory — note: renamed from PDO terminology in Phase 9)
   3c. Runtime Context (DynamicHardwareContextObject lifecycle and facade methods)
   3d. Registry Layer (HardwareRegistry — RT sweep orchestration + UUID lookup)
   3e. Catalog Layer (HardwareCatalog — metadata persistence with variant-typed backend data)
   3f. Backend Interfaces (IBackendScanner / IDHDOBuilder / IRuntimeAdapter three-interface split)
   3g. Concrete Backends (ethercat/gpio/i2c/spi/simulated Discovery+RTBackend pairs)
   3h. RT Utilities (SignalProcess, VectorBuffer, PulseMachine/DebounceMachine value types)
4. RT Hot-Path Profile (call graph from readAll→writeAll, virtual call count,
   allocations per cycle, syscalls per cycle)
5. SOLID Summary table (S/O/L/I/D scores with assessment text)
6. Open Items table (ID/severity/layer/description/status columns)
7. Score Summary table (dimension scores × weights → composite; criterion breakdown pass/fail grid)
8. Red-Line Rules Check (7 rules with status and evidence columns)
9. Verdict paragraph (SHIP-READY or DO-NOT-SHIP with justification)
```

---

## How to Read the Codebase Before Scoring

Run these searches. Each answers a scoring question:

```bash
# 1. Allocations in RT hot-path files
grep -rn "new \|make_unique\|make_shared\|push_back\|resize\|emplace_back" \
     src/dynamichardware/dhdo/HardwareRegistry.cpp \
     src/dynamichardware/dhdo/DHDO.cpp

# 2. Non-noexcept methods in RT chain
grep -n "void\|bool\|int\|float" \
     include/dynamichardware/dhdo/DHDO.h \
     include/dynamichardware/dhdo/HardwareRegistry.h \
     include/dynamichardware/dhdo/IRuntimeAdapter.h \
     | grep -v noexcept

# 3. Virtual calls in RT sweep (check all three interfaces)
grep -rn "virtual\|override" \
     include/dynamichardware/dhdo/IBackendScanner.h \
     include/dynamichardware/dhdo/IDHDOBuilder.h \
     include/dynamichardware/dhdo/IRuntimeAdapter.h \
     include/dynamichardware/backends/*/

# 4. unordered_map in RT files (acceptable for UUID lookup; flag if in per-entry loop)
grep -rn "unordered_map\|unordered_set" src/dynamichardware/

# 5. Freeze pattern integrity
grep -rn "freeze\|shrink_to_fit\|frozen_" src/dynamichardware/

# 6. Entry type filtering in readAll/writeAll — verify bitmask-based constexpr checks
grep -A10 "void.*readAll\|void.*writeAll" src/dynamichardware/dhdo/HardwareRegistry.cpp

# 7. Facade exposes internals? (registry/catalog should be inside private Impl struct)
grep -n "Impl\|friend class" include/dynamichardware/DynamicHardwareContextObject.h

# 8. Include graph — does Builder header include backend headers?
grep "#include" include/dynamichardware/DynamicHardwareBuilder.h
```

---

## Scoring Anchors (reference baselines)

| Layer | RT baseline | SOLID baseline | Justification |
|---|---|---|---|
| Builder + Orchestrator | N/A | 9.2 | Thin consumer facade delegates to internal phase coordinator. OCP deduction: orchestrator has hardcoded if-blocks per backend type requiring source edits for new transports (deferred fix: BackendRegistry plugin registration). Legacy DynamicHardwareContextFactory duplicates functionality during migration period. |
| DHDO (Entry/DHDO/Factory) | 9.7 | 9.5 | Concrete struct with composable bitmask EntryType system constexpr extractors; all accessors noexcept and compiler-inlineable read/write methods; factory decouples creation from backends via static helpers |
| Runtime Context (DynamicHardwareContextObject) | N/A | 9.0 | Pure RT lifecycle object with private Impl struct containing registry/catalog/nameToUuid map; constructor restricted via friend declarations only; clean ACTIVE→FROZEN→SHUTDOWN state machine |
| Registry | 9.6 | 9.3 | Exactly 2 virtual calls per backend per cycle via IRuntimeAdapter hooks; zero-virtual entry sweep iterating concrete struct methods; freeze pattern prevents post-freeze structural mutations; contiguous vector iteration with friendship-based direct dhdos_ access eliminating const-accessor overhead |
| Catalog | 8.5 | 9.0 | JSON persistence with variant-typed backend data structs providing unified ChannelDetails view without runtime casts; stable deterministic UUIDs from SHA-256 hash of canonical strings survive hardware restarts at same bus position; allocates on save/load but never called in hot path |
| Interfaces + Backends | 9.2 | 9.3 | Three-interface ISP-compliant split: Scanner returns pure data vectors without shared-state mutation; Builder constructs DHDO objects from parameter-passed channel lists; RuntimeAdapter inherits builder surface plus exactly 2 pure-virtual noexcept RT hooks owned by friendly HardwareRegistry for frozen PDO vector access |
| RT Utilities | 9.8 | 9.5 | vDSO clock_gettime timestamp cached as non-atomic global (~10ns userspace-only load); PulseMachine and DebounceMachine are zero-allocation trivially-copyable value-type state machines; VectorBuffer provides lock-free SPSC ring buffer with correct acquire/release memory ordering using power-of-two mask-based modulo arithmetic avoiding division operations

---

## Red-Line Rules

If any red line is violated the verdict must read
**"DO NOT SHIP — RT regression"** and implementation stops until resolved.

1. Any heap allocation reachable from `readAll()` or `writeAll()` after
   `freezeForRt()` completes.
2. Any `std::mutex`, `std::lock_guard`, or `std::condition_variable` in
   `readAll()` or `writeAll()`.
3. Any `virtual` or `std::function` call inside a per-entry loop in
   `readAll()` or `writeAll()` (exactly 2 IRuntimeAdapter hooks per backend per cycle are the allowed exception at adapter boundary — NOT inside entry iteration).
4. Any blocking syscall (file, socket, `sleep`, `clock_gettime`) inside
   `readAll()` or `writeAll()`. Consumer's signalProcessTickNow uses vDSO (~10ns userspace-only) which is acceptable outside library hot path scope.
5. DynamicHardwareContextObject exposes registry/catalog/nameToUuid as **public**
   (breaks facade invariant — consumers bypass lifecycle guards and phase enforcement). All internal state must remain in private Impl struct with restricted construction via friend declarations only.
6. DHDO::freeze() not called before RT loop starts (image pointers may be
   stale after vector reallocation; PhaseManager enforces DISCOVERY→MAPPING→BUILD_RT ordering then explicit ctx->freeze() transitions ACTIVE→FROZEN where post-freeze addBackend throws logic_error preventing structural mutations during operation phase).
7. DHDOEntry::read() called on output types or DHDOEntry::write() called on
   input types (data corruption risk prevented by constexpr bitmask-based isInputEntryType/isOutputEntryType filtering checking direction bits AND excluding message types from both sweeps handled exclusively by adapter hooks via IRuntimeAdapter lifecycle instead).

---

## Commit the Updated Analysis

After updating `doc/CurrentAnalysis.md`:

```
docs(analysis): update CurrentAnalysis for [brief change description]

- [bullet: what changed architecturally]
- [bullet: which score changed and why]
- [bullet: any new open items added or closed]
```
