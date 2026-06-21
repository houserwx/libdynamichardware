# Implementation Plan — Complete SOLID Refactoring (Final Approved Version)

## Execution Strategy (Improved)

- **Phased approach** with **compile-at-every-step** checkpoints — every phase produces a compilable intermediate state
- **Compatibility layer** using type aliases + dual-inheritance where needed — zero breakage until Phase 9
- **More focused interfaces** — HardwareDescriptor extracted into its own header for clarity
- **Split orchestration responsibilities** — DynamicHardwareBuilder handles fluent API; internal helper classes handle coordination
- **Discovery-to-mapping flow decoupled** — scan results flow through pure data structs (`HardwareDescriptor`) that the orchestrator converts to catalog entries without tight coupling to scanner internals
- **Total new files**: 11 (more focused interfaces)
- **Total modified files**: ~28  
- **Total deleted files**: 4 old interfaces (final phase only)

---

## Phase 1: Create New Small Interfaces (Pure Additions)

**Goal**: All new abstractions exist alongside the old ones. Nothing is removed or changed yet. Pure additions only.

### 1A. `include/dynamichardware/dhdo/HardwareDescriptor.h` (NEW FILE)

Extracted from IBackendScanner for cleaner separation and avoid circular includes.

```cpp
#pragma once
#include "dynamichardware/dhdo/HardwareCatalog.h"
#include <string>
#include <vector>

namespace dynamichardware::dhdo {

/// Pure data result from a hardware scan — no side effects, no catalog mutation.
struct HardwareDescriptor {
    std::string uuid;                    ///< Deterministic UUID from backend hash
    std::string channelType;             ///< "DigitalInput", "FloatOutput", etc.
    std::string name;                    ///< Human-readable display name
    bool        isOutput{false};
    
    BackendSpecificData backendData;     ///< Structured per-backend fields
    BackendType         backend{BackendType::UNKNOWN};
};

} // namespace dynamichardware::dhdo
```

### 1B. `include/dynamichardware/dhdo/IBackendScanner.h` (NEW FILE)

One-shot hardware scanner interface — returns pure data, never mutates catalog directly.

```cpp
#pragma once
#include "dynamichardware/dhdo/HardwareDescriptor.h"
#include <vector>

namespace dynamichardware::dhdo {

class IBackendScanner {
public:
    virtual ~IBackendScanner() = default;
    [[nodiscard]] virtual std::vector<HardwareDescriptor> scan() = 0;

protected:
    IBackendScanner() = default;
};

} // namespace dynamichardware::dhdo
```

### 1C. `include/dynamichardware/dhdo/IDHDOBuilder.h` (NEW FILE)

Construct DHDO objects from mapped channels — eliminates registerLine/registerDevice pattern entirely.

```cpp
#pragma once
#include "dynamichardware/dhdo/DHDO.h"
#include <memory>
#include <string>
#include <vector>

namespace dynamichardware::dhdo {

struct MappedChannel {
    std::string uuid;       ///< Catalog entry UUID (sole identity)
    EntryType   type;       ///< Consumer-specified direction + value format
    std::string name;       ///< Human-readable display name (optional override)
};

class IDHDOBuilder {
public:
    virtual ~IDHDOBuilder() = default;
    [[nodiscard]] virtual bool build(const std::vector<MappedChannel>& channels) = 0;
    [[nodiscard]] virtual const std::vector<DHDO>& getDHDOS() const noexcept = 0;

protected:
    IDHDOBuilder() = default;
};

} // namespace dynamichardware::dhdo
```

### 1D. `include/dynamichardware/dhdo/IRuntimeAdapter.h` (NEW FILE)

Runtime lifecycle interface — inherits IDHDOBuilder, adds RT cycle hooks and dhdos_ member. Replaces IRTBackend.

```cpp
#pragma once
#include "dynamichardware/dhdo/IDHDOBuilder.h"
#include <cstddef>
#include <memory>
#include <vector>

namespace dynamichardware::dhdo {

class IRuntimeAdapter : public IDHDOBuilder {
public:
    virtual ~IRuntimeAdapter() = default;
    IRuntimeAdapter(const IRuntimeAdapter&)            = delete;
    IRuntimeAdapter& operator=(const IRuntimeAdapter&) = delete;
    IRuntimeAdapter(IRuntimeAdapter&&)                 = delete;
    IRuntimeAdapter& operator=(IRuntimeAdapter&&)      = delete;

    virtual void initialize() noexcept {}
    virtual void onBeforeReadInputs()  noexcept = 0;
    virtual void onAfterWriteOutputs() noexcept = 0;

protected:
    IRuntimeAdapter() = default;
    std::vector<DHDO> dhdos_;
    
    friend class HardwareRegistry;

public:
    [[nodiscard]] const std::vector<DHDO>& getDHDOS() const noexcept override { return dhdos_; }
};

} // namespace dynamichardware::dhdo
```

### 1E. `include/dynamichardware/rt/IChannelProcessor.h` (NEW FILE)

Pluggable signal processor for RT read/write pipeline. Makes debounce/pulse/filtering extensible without modifying DHDOEntry.

```cpp
#pragma once
#include "dynamichardware/dhdo/DHDO.h"

namespace dynamichardware::rt {

class IChannelProcessor {
public:
    virtual ~IChannelProcessor() = default;
    virtual void processOnRead(DHDOEntry& entry) noexcept = 0;
    virtual void processOnWrite(DHDOEntry& entry) noexcept = 0;
};

} // namespace dynamichardware::rt
```

### 1F. `include/dynamichardware/config/PhaseManager.h` (NEW FILE — header-only, inline implementation)

Explicit state machine enforcing DISCOVERY → MAPPING → BUILD_RT → RUNNING → SHUTDOWN ordering.

### 1G. `include/dynamichardware/config/BackendRegistry.h` + `src/config/BackendRegistry.cpp` (NEW FILES)

Global backend module registration system — each backend self-registers at static init time via creator function returning `(scanner, adapter)` pairs. Eliminates hardcoded if-blocks in factory code.

---

## Phase 2: HardwareCatalog Write Lock + Phase Enforcement (Issue B)

**Changes to HardwareCatalog.h/.cpp**:
- Add `void endDiscovery()` — locks catalog against mutation after discovery ends
- Add `[[nodiscard]] bool isWritable() const noexcept` — returns true while catalog accepts writes  
- Guard existing `addEntry()` and `registerEcChannel()` with write lock check (early return if locked)
- Update `beginDiscovery()` to clear the write lock at start of method

---

## Phase 3: Dual-Inheritance Compatibility + Type Aliases (No API Breakage)

All concrete backends temporarily implement BOTH old and new interfaces for zero-breakage transition.

**Type aliases added across the codebase:**
```cpp
// In DynamicHardwareContextObject.h:
using RuntimeContext = DynamicHardwareContextObject;

// In IDHDOBuilder.h:
using MappedChannels = std::vector<MappedChannel>;

// In BackendRegistry.h:
using BackendCreators = std::unordered_map<std::string, BackendCreator>;
```

**Dual-inheritance pattern for all discovery classes:**
```cpp
class GPIODiscovery final 
    : public IDiscoveryBackend,           // Old interface (kept alive through Phase 9)
      public IBackendScanner              // New canonical interface
{ /* ... */ };
```

Same pattern for ALL runtime backends (`IRTBackend` + `IRuntimeAdapter`).

---

## Phase 4: Refactor Discovery Backends — scan() Returns Pure Data

Every discovery class implements `scan()` that returns `vector<HardwareDescriptor>`. The existing `discover()` becomes a thin wrapper: calls `scan()` → feeds results into catalog_ pointer. This decouples scanner internals from catalog internals while preserving backward compatibility.

**Backends modified (same pattern across all 5):**
- GPIO: `GPIODiscovery` — dual-inherit, implement `scan()`, refactor `discover()` to delegate
- EtherCAT: `EthercatDiscovery` — same pattern  
- I2C: `I2CDiscovery` — same pattern (stub returns minimal descriptors)
- SPI: `SPIDiscovery` — same pattern (stub returns minimal descriptors)
- Simulated: `SimulatedDiscovery` — **also fixes Issue I**: parsed JSON data flows as structured descriptors instead of being re-parsed by RT backend later

---

## Phase 5: Refactor Runtime Backends — Remove Public Setup Methods (Issues A, F, H)

**All public setup methods removed:**
- DELETE `registerLine()` from GPIORTBackend
- DELETE `registerDevice()` from I2CRTBackend  
- DELETE `registerDevice()` from SPIRTBackend

**All configuration now flows through `build(const vector<MappedChannel>&)` only.** Each backend looks up its own catalog entries using UUIDs and constructs internal state without the factory knowing HOW. The orchestrator passes `{uuid, type}` pairs; no backend-specific types leak into the orchestrator layer.

**EtherCAT special case (Issue G fix):** If channels list is empty → auto-discover all PDOs from EEPROM (current behavior). If non-empty → filter to those UUIDs. ALL backends behave identically through their interfaces at the consumer level.

---

## Phase 6: New DynamicHardwareBuilder + HardwareOrchestrator Split (Issue E)

The god class (`DynamicHardwareContextFactory`) is split into focused components with clear separation:

| Class | Responsibility | Size Target |
|-------|---------------|-------------|
| **`DynamicHardwareBuilder`** | High-level fluent API ONLY (~80 lines in header) | Small |
| **`HardwareOrchestrator`** | Internal state machine + phase coordination (~150 lines) | Medium |
| **`PhaseManager`** | Lifecycle ordering enforcement (~40 lines, header-only) | Tiny |
| **`BackendRegistry`** | Backend module registration (~30 lines header + ~25 lines cpp) | Tiny |
| *(existing)* `HardwareCatalog` | Catalog persistence load/save/deletion/locking | Unchanged responsibility |

**Key architectural change:** The builder's public methods delegate to an internal `HardwareOrchestrator` that handles:
- Iterating over `BackendRegistry::getAll()` instead of hardcoded if-blocks (fixes C/D — OCP satisfied)
- Calling `scanner->scan()` → feeding results into catalog during DISCOVERY only  
- Locking catalog after discovery via `endDiscovery()` (fixes B)
- Filtering mapped channels per-backend and calling `adapter->build(channels)` (fixes F — DIP restored)

Old factory (`DynamicHardwareContextFactory`) keeps existing public API but delegates internally where possible. Full consumer migration happens in Phase 8.

---

## Phase 7: Update Tests + Add New Test Files

All unit tests updated for dual-inheritance transition period:
- `test_hardware_registry.cpp`: TestAdapter inherits both old+new interfaces; test methods use new `build()` API
- `test_hardware_catalog.cpp`: Add tests for `endDiscovery()` write lock behavior
- **NEW** `test_phase_manager.cpp`: Valid/invalid phase transitions, skip-ahead behavior
- **NEW** `test_backend_registry.cpp`: Registration lookup, creator function invocation

---

## Phase 8: Migrate All Examples to Builder API

Consumer-facing examples migrate from factory pattern to builder pattern:
```cpp
// Before: DynamicHardwareContextFactory factory.withGPIO();
// After:  DynamicHardwareBuilder builder.enableBackend("GPIO");
```

EtherCAT no longer has special "autobuild idiosyncrasy" at the consumer level — all backends behave identically through unified interface.

---

## Phase 9: Cleanup — Remove Deprecated Interfaces (Breaking Change Point)

Final cleanup when everything compiles and tests pass:
1. DELETE `IDiscoveryBackend.h` → replaced by `IBackendScanner.h` + orchestrator pipeline
2. DELETE `IRTBackend.h` → replaced by `IRuntimeAdapter.h` (inherits IDHDOBuilder)  
3. Remove dual-inheritance from all concrete backends — keep only new canonical base classes
4. Optionally rename `DynamicHardwareBuilder` files to replace old factory names (or keep both with delegation)

---

## File Inventory Summary

### New Files Created (11 total)

| # | File Path | Purpose | Phase |
|---|-----------|---------|-------|
| 1 | `include/.../dhdo/HardwareDescriptor.h` | HardwareDescriptor struct (extracted for clarity) | 1A |
| 2 | `include/.../dhdo/IBackendScanner.h` | Scanner interface — pure data scan() result | 1B |
| 3 | `include/.../dhdo/IDHDOBuilder.h` | Builder interface + MappedChannel struct | 1C |
| 4 | `include/.../dhdo/IRuntimeAdapter.h` | RT adapter interface (inherits IDHDOBuilder) | 1D |
| 5 | `include/.../rt/IChannelProcessor.h` | Signal processing pipeline interface | 1E |
| 6 | `include/.../config/PhaseManager.h` | Lifecycle state machine enforcement | 1F |
| 7 | `include/.../config/BackendRegistry.h` | Backend module registration system header | 1G-a |
| 8 | `src/config/BackendRegistry.cpp` | BackendRegistry implementation | 1G-b |
| 9 | `include/dynamichardware/DynamicHardwareBuilder.h` | High-level fluent API entry point (~80 lines) | Phase 6a |
| 10 | `include/dynamichardware/HardwareOrchestrator.h` | Internal coordination: registry iteration, phase management (~150 lines) | Phase 6b-header |
| 11 | `src/HardwareOrchestrator.cpp` | HardwareOrchestrator implementation — discover()/buildRT() logic | Phase 6b-cpp |

### Modified Files (~28 total)

| File Path | Changes Summary | Phase(s) |
|-----------|-----------------|----------|
| `CMakeLists.txt` | Add ALL new source files to LD_SOURCES; add test executables for new tests | Phases 1,6,7 |
| `include/.../dhdo/HardwareCatalog.h` | Add endDiscovery(), isWritable(), writeLocked_ member | Phase 2A |
| `src/.../dhdo/HardwareCatalog.cpp` | Guard addEntry/registerEcChannel with lock; impl endDiscovery() | Phase 2B |
| `include/.../backends/gpio/GPIODiscovery.h/.cpp` | Dual-inherit IBackenScanner; implement scan(); discover() delegates to scan()→catalog | Phase 4A |
| `include/.../backends/gpio/GPIORTBackend.h/.cpp` | Remove registerLine(); dual-inherit IRuntimeAdapter; implement build(channels) | Phase 5A |
| `include/.../backends/ethercat/EthercatDiscovery.h/.cpp` | Same pattern as GPIO discovery (Phase 4B) | Phase 4B |
| `include/.../backends/ethercat/EthercatRTBackend.h/.cpp` | Implement build(channels); filter by UUID or auto-discover if empty (fixes Issue G) | Phase 5D |
| `include/.../backends/i2c/I2CDiscovery.h/.cpp` | Same pattern as GPIO discovery (Phase 4C) | Phase 4C |
| `include/.../backends/i2c/I2CRTBackend.h/.cpp` | Remove registerDevice(); implement build(channels) (Phase 5B) | Phase 5B |
| `include/.../backends/spi/SPIDiscovery.h/.cpp` | Same pattern as GPIO discovery (Phase 4D) | Phase 4D |
| `include/.../backends/spi/SPIRTBackend.h/.cpp` | Remove registerDevice(); implement build(channels) (Phase 5C) | Phase 5C |
| `include/.../backends/simulated/SimulatedDiscovery.h/.cpp` | scan() returns structured data; fixes duplicate parsing (Issue I) | Phase 4E |
| `include/.../backends/simulated/SimulatedRTBackend.h/.cpp` | build(channels) receives parsed data instead of re-parsing JSON (Issue I) | Phase 5E |
| `include/dynamichardware/DynamicHardwareContextFactory.h` | Add type alias RuntimeContext; keep existing API for backward compat through Phase 8 | Phase 3,6 |
| `tests/test_hardware_registry.cpp` | TestAdapter inherits both old+new interfaces; update test methods for build() API | Phase 7A |
| `tests/test_hardware_catalog.cpp` | Add tests for endDiscovery/writeLock behavior | Phase 7B |
| `examples/gpio_correct_demo.cpp` | Replace factory with builder; use enableBackend("GPIO") instead of withGPIO() | Phase 8A |
| `examples/simulated_io_demo.cpp` | Same migration pattern as gpio demo | Phase 8B |
| `examples/ethercat_demo.cpp` | Same migration — no more "autobuild idiosyncrasy" at consumer level | Phase 8C |

### Files Deleted in Phase 9 (Breaking Change)

| File Path | Replaced By |
|-----------|-------------|
| `include/.../dhdo/IDiscoveryBackend.h` | `IBackendScanner.h` + orchestrator pipeline |
| `include/.../dhdo/IRTBackend.h` | `IRuntimeAdapter.h` (inherits IDHDOBuilder) |

---

## Issue-to-Phase Mapping

| Issue | Fixed In Phase(s) | Mechanism |
|-------|-------------------|-----------|
| **A** registerLine() publicly callable | Phase 5A (remove method), Phase 1C (build(channels) replaces it) | Method deleted from public API; all config flows through build() parameter only |
| **B** No phase enforcement / catalog write lockdown | Phase 2 (HardwareCatalog.endDiscovery()), Phase 1F (PhaseManager) | Catalog locked after discovery; PhaseManager enforces ordering in builder/orchestrator |
| **C** Factory hardcodes backend types in buildRT() | Phase 6 (iterate BackendRegistry instead of if blocks via HardwareOrchestrator) | OCP satisfied — adding backends requires zero factory changes |
| **D** Factory hardcodes backend types in discover() | Phase 6 (same fix as C, both methods replaced by registry iteration in orchestrator) | Same mechanism as C |
| **E** God class SRP violation | Phase 6 (split into Builder ~80 lines + Orchestrator ~150 lines + PhaseManager ~40 lines + BackendRegistry ~55 lines total) | Each responsibility has its own focused class under 200 lines |
| **F** Factory pushes data INTO backend instead of passing TO it | Phase 5 (build(channels) passes configuration AS PARAMETERS via orchestrator) | DIP restored — orchestrator depends on abstractions only |
| **G** EtherCAT autobuild vs manual define inconsistency | Phase 5D (all backends use same build(channels) interface; autobuild encapsulated inside backend) | LSP consistency — uniform interface across all transport types at consumer level |
| **H** registerLine() returns internal index | Phase 5A (method deleted entirely) | No longer exists as public method anywhere |
| **I** SimulatedBackend duplicate JSON parsing | Phase 4E/5E (scan() returns structured HardwareDescriptor → build(channels) receives parsed results) | Single source of truth flows through orchestrator pipeline; no re-parsing |

---

## Build Verification Checkpoints

After each phase, the project MUST compile and tests must pass:

1. **Phase 1 complete**: `cmake .. && make` compiles (new interfaces are pure additions with no callers yet)
2. **Phase 2 complete**: `make test` passes (catalog write guards don't break existing code since endDiscovery() isn't called yet by old factory)  
3. **Phases 3-4 complete**: Discovery backends dual-inherit IBackenScanner; both old discover() and new scan() paths work; `make test` passes
4. **Phase 5 complete**: Runtime backends implement build(channels); old buildRT() still works via wrapper; public setup methods removed from API but factory calls replaced before breaking; `make test` passes
5. **Phase 6 complete**: DynamicHardwareBuilder + HardwareOrchestrator usable alongside old factory; examples can start migrating; `make` + `make test` pass
6. **Phases 7-8 complete**: All tests updated to new APIs, all examples migrated to builder pattern; full test suite passes
7. **Phase 9 complete**: Old interfaces deleted; only new canonical names remain; clean compilation with zero deprecation warnings
