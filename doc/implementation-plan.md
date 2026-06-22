# Implementation Plan — CurrentAnalysis Findings Remediation (Post Phases 1–7)

| Field       | Value                                                                 |
|-------------|-----------------------------------------------------------------------|
| **Source**  | \`doc/CurrentAnalysis.md\` (commit bff8ddb)                            |
| **Date**    | 2026-06-22                                                            |
| **Status**  | ACTIVE — addresses remaining open items after Phases 1–7 completion   |

---

## Overview

Phases 1–7 from the original implementation plan have been implemented, tested, and merged. This document addresses the **remaining unresolved open items** identified in the fresh architectural analysis at commit bff8ddb.

### Completed Background (Phases 1–7)

| Phase | Commit(s)         | Description | Status |
|-------|-------------------|-------------|--------|
| P7   | f721ebc           | constexpr annotations + InternalState rename + SignalProcess docs | ✅ MERGED |
| P1   | 683e670           | Inline bitmask dispatch in DHDOEntry; Int8*/Int32Output functional via memcpy path | ✅ MERGED |
| P2+3 | 99721d2           | Orchestrator OCP rewrite: enabledBackends map replaces boolean flags; Builder pure delegation | ✅ MERGED (partial) |
| P4   | 99721d2           | PhaseManager explicit error handling; resetToDiscovery() added | ✅ MERGED |
| P5   | 99721d2           | Simulated backend reinterpret_cast → memcpy consistency fix | ✅ MERGED |
| P6   | 99721d2           | entryTypeToString returns std::string — thread-safe | ✅ MERGED |

### Remaining Open Items from CurrentAnalysis

| ID | Severity | Layer       | Summary                                                                                          | Status        |
|----|----------|-------------|--------------------------------------------------------------------------------------------------|---------------|
| OI-01  | High     | Orchestrator    | Hardcoded if-blocks in \`runDiscoveryScan()\` and \`buildRT()\` still require source edits per new transport. BackendRegistry exists but is not wired into production flow. Deferred: plugin-style factory pattern.                      | ⚠️ PARTIAL    |
| OI-03  | Low      | DHDO              | No frozen_ flag inside DHDO struct itself — relies on HardwareRegistry's frozen_. Post-freeze push_back corrupts image pointers silently.                                    | ⚠️ DEFERRED   |
| OI-09  | Low      | RT Utilities       | signalProcessTickNow POSIX dependency without Windows fallback; no runtime assertion for multi-threaded misuse of gSignalProcessNowNs.                                            | ⚠️ PARTIAL    |
| **OI-10**  | **Medium**  | **ContextObject**     | \`getCandidates(uint8_t)\` declared \`const noexcept\` but calls \`push_back\`; \`lookupByName(string_view)\` allocates temporary std::string inside noexcept body. Incorrect contracts → std::terminate risk on allocation failure during init phase.                             | ⚠️ **NEW**    |

### Items Requiring Implementation

Two items require code changes:

| Phase | Title                                          | OI Addressed          | Risk Level | Effort     |
|-------|------------------------------------------------|-----------------------|------------|------------|
| P8    | Wire BackendRegistry into orchestrator dispatch loops (full OCP) | OI-01 ✅               | Medium     | High       |
| P9    | Fix incorrect noexcept contracts on ContextObject diagnostic methods                  | OI-10 ✅              | Very Low   | Small      |

### Items Deferred / Accepted

| Item   | Rationale                                                                                                                                                        |
|--------|------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| OI-03  | Defense-in-depth already exists at HardwareRegistry level (frozen_ flag throws logic_error on post-freeze addBackend). Library consumers cannot access raw DHDO objects after freeze through any public API — adding per-DHDO guard is belt-and-suspenders that adds no user value today. **Reconsideration trigger**: If external plugin backends become common and the multi-backend-per-registry pattern emerges, revisit with a lightweight guard (simple bool flag with debug assert in \`#ifndef NDEBUG\` builds to avoid atomic overhead in release). The current enforcement chain (HardwareRegistry.frozen_ → facade restrictions) is sufficient for built-in-only usage.                |
| OI-09  | POSIX dependency (\`<time.h>\`, CLOCK_MONOTONIC) accepted as target-platform constraint (Linux/ARM exclusively). Single-thread invariant documented extensively in header comments above gSignalProcessNowNs declaration since Phase 7. Adding runtime check requires atomics that defeat the ~10ns cost goal; adding Windows fallback adds dead code for a platform never targeted by this library. Current documentation expansion is sufficient closure for a single-platform embedded library.        |

---

## Phase 8 — Wire BackendRegistry into orchestrator dispatch loops

### Problem

After Phases 2+3, \`OrchestratorState\` uses an \`enabledBackends\` map and Builder.enableBackend() delegates purely to the orchestrator state — **state struct OCP compliance achieved**. However both \`runDiscoveryScan()\` and \`buildRT()\` still contain **hardcoded if-else chains** that require source edits for each new transport:

```cpp
// In runDiscoveryScan():
if (name == "EtherCAT")  { /* construct EthercatDiscovery */ }
else if (name == "GPIO")  { /* construct GPIODiscovery     */ }
else if (name == "I2C")   { /* ...                         */ }
// ... five hardcoded branches per method, ×2 methods = 10 total edit sites
```

BackendRegistry exists with full API (\`registerBackend(name, creator)\`, \`getAll()\`, \`getCreator(name)\`) but ships as dead weight — zero backends call \`registerBackend()\` at static init time and the orchestrator never queries it. This prevents external plugin backends from registering without library recompilation.

### Solution

Two-sided wiring: **(A) backends self-register**, **(B) orchestrator dispatches through registry lookup instead of if-else chains**.

#### Step 8A: Add self-registration to each backend module

Create a shared helper header \`include/dynamichardware/backends/registration.h\` with a macro that reduces copy-paste boilerplate for built-in and external backend authors:

```cpp
// include/dynamichardware/backends/registration.h
#pragma once
#include "dynamichardware/config/BackendRegistry.h"
#include <mutex>

/// Register a backend at static-init time using std::call_once for thread-safety.
/// Usage in any .cpp file:
///   REGISTER_BACKEND("EtherCAT", []() {
///       return std::make_pair(
///           std::make_unique<ethercat::EthercatDiscovery>(),
///           std::make_unique<ethercat::EthercatRTBackend>()
///       );
///   });
#define REGISTER_BACKEND(Name, CreatorLambda) \\
    namespace { \\
        struct Registrar_##__LINE__ { \\
            static std::once_flag sRegFlag; \\
            static void registerOnce() { \\
                config::BackendRegistry::registerBackend(Name, CreatorLambda); \\
            } \\
            Registrar_##__LINE__() { std::call_once(sRegFlag, &Registrar_##__LINE__::registerOnce); } \\
        }; \\
        static constexpr Registrar_##__LINE__ sRegistrar_##__LINE__{}; \\
    }
```

Each \`{Transport}Discovery.cpp\` uses the macro once at file scope:

```cpp
// In src/dynamichardware/backends/ethercat/EthercatDiscovery.cpp (bottom of file)
REGISTER_BACKEND("EtherCAT", []() {
    return std::make_pair(
        std::make_unique<ethercat::EthercatDiscovery>(),
        std::make_unique<ethercat::EthercatRTBackend>()
    );
});
```

Same pattern for all five backends (GPIO, I2C, SPI, Simulated). Each registers under its canonical name string ("GPIO", "I2C", "SPI", "Simulated"). The creator lambda captures no external state — construction parameters are passed post-construction via a dedicated configure hook on IRuntimeAdapter.

**Config passing design:** After creation, the orchestrator calls \`adapter->configure(configMap)\` with the per-backend config map from \`enabledBackends[name]\`. This enables parameterized backends without modifying the creator signature:
- EtherCAT: \`{"cycleNs": "500000"}\`
- I2C: \`{"busPath": "/dev/i2c-1"}\`
- Simulated: \`{"definitionsPath": "/path/to/defs.json"}\`

The configure method should be added to IRuntimeAdapter if not already present, accepting \`std::unordered_map<std::string, std::string>\`. Backends ignore unknown keys gracefully (forward-compatible).

**Self-registration safety note:** The call_once guard handles edge cases where multiple TUs might trigger registration. Static constexpr instance ensures initialization during C++ static init phase with exactly-once guarantee. External backend authors only need to include \`registration.h\` and invoke the macro once — no boilerplate duplication required.

#### Step 8B: Rewrite orchestrator dispatch to query registry

Replace both hardcoded if-block chains with registry-driven iteration:

**Current \`runDiscoveryScan()\` (~60 lines of if-else) → new version:**
```cpp
bool HardwareOrchestrator::runDiscoveryScan() {
    std::unordered_set<std::string> enabledNames(
        state_.enabledBackends.begin(), state_.enabledBackends.end());

    for (const auto& name : config::BackendRegistry::getAll()) {
        if (!enabledNames.count(name)) continue;

        const auto* creator = config::BackendRegistry::getCreator(name);
        if (!creator) {
            fprintf(stderr, "[Discover] Backend '%s' requested but not in registry\n", name.c_str());
            return false;
        }

        // Creator returns scanner+adapter pair — scanner used here, adapter stored for buildRT()
        auto [scanner, adapter] = (*creator)();
        
        // Discovery: scanner populates catalog through existing setCatalog/discover pattern
        scanner->setCatalog(&catalog_);   // TODO: verify IBackendScanner has this or use scan()->addEntry pattern
        bool found = scanner->discover();
        
        pendingAdapters_[name] = std::move(adapter);
        if (found) anyDiscovered = true;
    }
    
    return anyDiscovered;
}
```

**Current \`buildRT()\` (~70 lines of if-else) → new version:**
```cpp
std::unique_ptr<DynamicHardwareContextObject> HardwareOrchestrator::buildRT() {
    dhdo::HardwareRegistry registry;

    for (auto& [name, adapter] : pendingAdapters_) {
        // Extract mapped channels for this backend by filtering catalog entries by backend type
        MappedChannels channels = extractChannelsForBackend(name);
        
        if (!adapter->build(channels)) {
            fprintf(stderr, "[RtBuild] Backend '%s' build failed\n", name.c_str());
            continue;  // Non-fatal: other backends may still succeed
        }

        registry.addBackend(std::move(adapter));
    }

    if (registry.backendCount() == 0) {
        fprintf(stderr, "[RtBuild] No backends successfully built — context will be empty\n");
    }

    DynamicHardwareContextObject::InternalState ctxState{
        std::move(registry), std::move(catalog_), {}
    };
    for (const auto& entry : ctxState.catalog.entries()) {
        if (!entry.name.empty()) ctxState.nameToUuid[entry.name] = entry.uuid;
    }

    return std::make_unique<DynamicHardwareContextObject>(std::move(ctxState));
}
```

**Orchestrator header changes:** Add private member to hold adapters between scan and build phases:
```cpp
// In HardwareOrchestrator.h
private:
    std::unordered_map<std::string, std::unique_ptr<dhdo::IRuntimeAdapter>> pendingAdapters_;
```

### Files Modified

| File | Change |
|------|--------|
| **NEW** \`include/dynamichardware/backends/registration.h\` | Shared macro helper (REGISTER_BACKEND) reducing copy-paste boilerplate for self-registration. External backend authors include this single header instead of replicating call_once + static registrar pattern verbatim. |
| \`src/dynamichardware/backends/ethercat/EthercatDiscovery.cpp\` | Add REGISTER_BACKEND invocation at file scope; verify configure(configMap) hook on RTBackend accepts per-backend params |
| \`src/dynamichardware/backends/gpio/GPIODiscovery.cpp\` | Same pattern — REGISTER_BACKEND("GPIO", ...) |
| \`src/dynamichardware/backends/i2c/I2CDiscovery.cpp\` | Same pattern — REGISTER_BACKEND("I2C", ...) |
| \`src/dynamichardware/backends/spi/SPIDiscovery.cpp\` | Same pattern — REGISTER_BACKEND("SPI", ...) |
| \`src/dynamichardware/backends/simulated/SimulatedDiscovery.cpp\` | Same pattern — REGISTER_BACKEND("Simulated", ...) |
| \`include/dynamichardware/HardwareOrchestrator.h\` | Remove all backend-specific includes from header (currently transitively includes every Discovery+RTBackend header via explicit #include statements); add \`pendingAdapters_\` map member; forward-declare IRuntimeAdapter if needed for the map type. **Critical compile-time decoupling**: after this change, consumers including HardwareOrchestrator.h should not recompile when a new backend module is added to the build. |
| \`include/dynamichardware/dhdo/IRuntimeAdapter.h\` | Add \`virtual void configure(const std::unordered_map<std::string, std::string>& config) = 0;\` or equivalent pure-virtual configure hook to IRuntimeAdapter interface for per-backend parameter injection post-creation. Default implementation in derived classes ignores unknown keys gracefully (forward-compatible). |
| \`src/HardwareOrchestrator.cpp\` | Replace ~130 lines of hardcoded backend instantiation with registry-driven iteration using BackendRegistry::getAll() loop and creator function lookups; remove backend-specific config extraction logic from orchestrator entirely — delegate to adapter->configure(configMap) calls. Keep backend-specific includes only in .cpp files that implement self-registration (implementation detail that doesn't leak to library consumers through public headers).              |

### Tests Required

- Update existing \`tests/test_backend_registry.cpp\`: Verify all 5 built-in backends are registered after library loads (static init runs before test main; assert BackendRegistry::getAll() returns ≥ 5 entries with expected canonical names).
- **End-to-end dynamically-registered mock backend test**: Create a separate TU (compiled as an object file linked into the test binary only, not the library) that calls REGISTER_BACKEND("MockTest", ...) returning a scanner that produces one dummy descriptor and an adapter that passes through read/write cycles verbatim. Run full discover→build→freeze→readAll/writeAll cycle and verify the mock backend's data flows correctly end-to-end without any hardcoded if-block involvement. This proves external plugin extensibility works without recompiling the core library.
- Regression guard: All existing unit tests pass — behavior should be identical for consumers since the public API surface (DynamicHardwareBuilder fluent chain) is unchanged.
- Cross-compile validation: CMake configure succeeds when EtherCAT lib is absent (stub mode) and GPIO lib is absent to ensure conditional compilation paths remain intact.

### Risk Assessment

**Medium risk.** The orchestrator internals change significantly but the consumer-facing API (\`DynamicHardwareBuilder\`) does not. Static initialization order across TUs is well-defined within a single TU but there's no guarantee between TUs — mitigated by lazy lookup at runtime (registry is queried during \`discover()\` which always runs after static init completes). The \`std::call_once\` registration guard handles edge cases where multiple TUs might trigger registration simultaneously.

---

## Phase 9 — Fix incorrect noexcept contracts on ContextObject diagnostic methods

### Problem

Two public methods of DynamicHardwareContextObject are marked \`noexcept\` but can throw via STL allocation:

1. **getCandidates(uint8_t)** calls \`result.push_back(...)\` which throws \`std::bad_alloc\` on memory exhaustion — yet declared \`const noexcept\`. If this ever executes under OOM conditions, the program will call \`std::terminate()\` instead of allowing graceful error handling upstream.
2. **lookupByName(string_view)** constructs a temporary \`std::string{name}\` for the unordered_map lookup inside an \`noexcept\` body — same std::terminate risk if name string exceeds SBO capacity and heap allocation fails.

These are **init-time/diagnostic methods** never called from the RT hot path, so they don't violate any red-line rules or affect RT determinism scores. However, incorrect noexcept contracts represent undefined behavior in exception-safe code paths and cause abrupt termination rather than returning an error to callers who could handle it gracefully.

### Solution

Remove \`noexcept\` from both methods since they legitimately allocate:

```cpp
// Before (incorrect):
[[nodiscard]] std::vector<ChannelCandidate> getCandidates(uint8_t typeMask) const noexcept;
[[nodiscard]] dhdo::DHDOEntry* lookupByName(std::string_view name) noexcept;

// After (correct — removed noexcept where STL allocation can legitimately throw):
[[nodiscard]] std::vector<ChannelCandidate> getCandidates(uint8_t typeMask) const;   ///< May throw std::bad_alloc from vector::push_back during init/diagnostic phase
[[nodiscard]] dhdo::DHDOEntry* lookupByName(std::string_view name);                   ///< May throw std::bad_alloc from temporary std::string construction if name exceeds SBO capacity
```

**C++20 heterogeneous lookup optimization**: If the unordered_map key type is changed to \`std::string_view\` with a transparent hasher and key_equal comparator, \`lookupByName(string_view)\` can perform direct lookup without constructing a temporary \`std::string{name}\`. This eliminates the allocation risk entirely for names within SBO range. However, this requires careful handling of lifetime semantics (stored keys must outlive lookups), so the straightforward approach is removing \`noexcept\` and documenting that these methods may allocate:

```cpp
// Header documentation note added above method declarations:
/// @note The following diagnostic/query methods are NOT marked noexcept because they
/// legitimately allocate via STL containers or string construction.
/// These are init-time/convenience methods never called from RT cycle paths.
```

If C++20 heterogeneous lookup is adopted in a future pass, the \`lookupByName\` method could be conditionally annotated with \`noexcept(noexcept(...))\` — but this adds complexity disproportionate to benefit for an init-only path.

The convenience wrappers (\`getBoolInputCandidates()\`, etc.) call \`getCandidates()\` internally — they should also lose their \`noexcept\` annotation for consistency:

```cpp
// These all delegate to getCandidates() which can now throw:
[[nodiscard]] std::vector<ChannelCandidate> getBoolInputCandidates() const;
[[nodiscard]] std::vector<ChannelCandidate> getBoolOutputCandidates() const;
[[nodiscard]] std::vector<ChannelCandidate> getFloatInputCandidates() const;
[[nodiscard]] std::vector<ChannelCandidate> getFloatOutputCandidates() const;
```

### Files Modified

| File | Change |
|------|--------|
| \`include/dynamichardware/DynamicHardwareContextObject.h\` | Remove \`noexcept\` from getCandidates, lookupByName, and all four typed candidate query methods        |
| \`src/DynamicHardwareContextObject.cpp\` | Update declarations to match (remove noexcept from definitions)       |

### Tests Required

No new tests needed — this is a pure contract correction with no behavioral change when memory is available. Existing test suite validates that these methods return correct results regardless of noexcept specification. The risk scenario (OOM during init phase) is untestable in unit tests without mocking the allocator.

### Risk Assessment

**Very low risk.** Removing noexcept from init-time diagnostic methods has zero impact on RT cycle performance or correctness. No callers depend on the noexcept guarantee for exception-safety optimization since these are convenience methods used exclusively during configuration/debug phases. Consumer code calling these methods will see identical behavior under normal operation; only the OOM failure path changes from std::terminate() to propagating bad_alloc (which is the CORRECT behavior).

---

## Dependency Graph and Execution Order

```
Phase 8 ─────────> Phase 9   (independent — can run in parallel if desired)
```

P8 and P9 touch completely disjoint subsystems: orchestrator dispatch internals vs ContextObject header annotations. Either order works; recommended sequence below puts P8 first as it's the larger change requiring more integration testing.

### Recommended Merge Sequence

1. **Merge P8 first** — BackendRegistry wiring touches five backend source files plus orchestrator header/implementation. Larger blast radius requires thorough integration validation before adding further changes.          |
2. **Merge P9 second** — Pure annotation fix with minimal diff surface area; safe to land after P8 stabilizes.        |

---

## Score Impact Projection

After both phases complete, expected score changes:

| Layer                          | Dimension    | Current | After Fix | Change | Reason                                                                                                          |
|--------------------------------|--------------|---------|-----------|--------|-----------------------------------------------------------------------------------------------------------------|
| Builder + Orchestrator         | SOLID (OCP)  | 8.8     | **9.3**   | ⬆️ +0.5  | Full OCP compliance: zero hardcoded branches required for new transports. External plugin backends register through BackendRegistry without any library source edits.             |
| Runtime Context                | SOLID        | 8.8     | **9.0**   | ⬆️ +0.2  | Correct noexcept contracts on all diagnostic methods eliminates std::terminate risk and improves exception-safety guarantees for init-time callers.              |

### Projected New Composite After All Phases Complete

| Layer                          | RT Determinism (55%) | SOLID (45%)   | Composite     |
|--------------------------------|---------------------:|:-------------:|:-------------:|
| Builder + Orchestrator         | N/A                  | **9.3**       | **9.3**       |
| DHDO                           | 9.7                  | 9.5           | 9.61          |
| Runtime Context                | N/A                  | **9.0**       | **9.0**       |
| Registry                       | 9.7                  | 9.4           | 9.57          |
| Catalog                        | 8.5                  | 9.0           | 8.73          |
| Interfaces + Backends          | 9.3                  | 9.1           | 9.21          |
| RT Utilities                   | 9.8                  | 9.5           | 9.67          |

> **Final composite average across all layers: ~9.30 / 10**  
> *(Up from current ~9.18 — gains from OCP completion and noexcept contract correction)*

---

## Open Items Tracking Matrix

| OI ID    | Phase Addressed   | Status After Plan Complete                                                                                              |
|----------|-------------------|-------------------------------------------------------------------------------------------------------------------------|
| OI-01    (OCP in orchestrator)      | P8 ✅              | RESOLVED — BackendRegistry wired with self-registration; orchestrator uses registry-driven iteration replacing all hardcoded if-block chains; external plugin backends fully supported without library recompilation.             |
| OI-02    (Int8/Int32Output dead code)       | P1 (pre-existing)     | ✅ Already resolved by Phases 1–7 — bitmask dispatch handles ALL composed EntryType values automatically via entryValueFormat() + entryBitSize().            |
| OI-03    (No frozen_ in DHDO)         | Not addressed        | ⚠️ DEFERRED — Defense-in-depth exists at HardwareRegistry level with frozen_ flag and logic_error on post-freeze addBackend(). Adding per-DHDO guard is belt-and-suspenders that adds no user value given the existing enforcement chain and consumer-facing facade restrictions. Revisit if multi-backend-per-registry pattern emerges or external backend authors need finer-grained safety guarantees.                   |
| OI-04    (Static buffer in factory)   | P6 (pre-existing)     | ✅ Already resolved by Phases 1–7 — returns std::string instead of const char* to static buffer. Thread-safe for concurrent init-time calls.                |
| OI-05    (constexpr missing)          | P7 (pre-existing)     | ✅ Already resolved by Phases 1–7 — isInputEntryType/isOutputEntryType marked constexpr enabling template constraint usage.           |
| OI-06    (PhaseManager exception swallowing)       | P4 (pre-existing)      | ✅ Already resolved by Phases 1–7 — explicit error handling returns false/nullptr on illegal transitions; resetToDiscovery() added for intentional re-scanning scenarios.             |
| OI-07    (Impl naming misleading)     | P7 (pre-existing)     | ✅ Already resolved by Phases 1–7 — renamed to InternalState with matching member variable name change.              |
| OI-08    (reinterpret_cast aliasing)  | P5 (pre-existing)     | ✅ Already resolved by Phases 1–7 — all simulated backend casts replaced with memcpy matching DHDO layer anti-aliasing pattern.            |
| OI-09    (POSIX dependency / debug assert)        | Not addressed        | ⚠️ PARTIAL — Accepted as target-platform constraint (Linux/ARM exclusively). Documentation expanded in Phase 7 with single-thread invariant note and platform constraint statement. No runtime check or Windows fallback added; both would add dead code or atomic overhead defeating the ~10ns cost goal. Current documentation is sufficient closure for a library targeting a specific embedded platform.                    |
| **OI-10** (**noexcept contract violation**)   | **P9 ✅**          | RESOLVED — Removed noexcept from getCandidates, lookupByName, and all four typed candidate query methods that legitimately allocate via STL containers or string construction. Eliminates std::terminate risk on allocation failure during init phase while preserving correct exception propagation semantics.             |

---

## Versioning & Changelog Impact

### After Phase 8 Merge (BackendRegistry wiring)
- **Internal-only change:** Orchestrator dispatch implementation changes but consumer-facing API (\`DynamicHardwareBuilder\`) surface is unchanged — no breaking change to public headers.
- **Header hygiene improvement:** HardwareOrchestrator.h no longer transitively includes every backend-specific header. Compile-time decoupling achieved: adding a new backend module does not force recompilation of consumers that include the orchestrator header.
- **New capability:** External backends can now call \`REGISTER_BACKEND(name, creator_lambda)\` after including \`backends/registration.h\`, then use \`.enableBackend(name)\` through the fluent builder without any library source edits or recompilation. Per-backend configuration flows through the adapter->configure(configMap) hook for parameterized transports.
- **Changelog entry:** "Completed full OCP compliance: wired BackendRegistry into orchestrator dispatch loops replacing all hardcoded backend if-block chains. Added REGISTER_BACKEND macro helper in backends/registration.h for zero-boilerplate self-registration. All five built-in backends self-register at load time; external plugin transports fully supported via dynamic registration with per-backend configure hooks."

### After Phase 9 Merge (noexcept fix)
- **API change:** Five public DynamicHardwareContextObject methods lose \`noexcept\` specification. This is technically a source-compatible ABI change in some compilers (may affect generated code for callers that rely on noexcept for optimization), but functionally identical under normal operation since these are init-time diagnostic methods never called from RT paths.
- **Correctness improvement:** Eliminates std::terminate risk when getCandidates() or lookupByName() execute under memory pressure during init phase. Callers can now catch \`std::bad_alloc\` and handle gracefully instead of receiving abrupt termination.
- **Changelog entry:** "Fixed incorrect noexcept contracts on DynamicHardwareContextObject diagnostic methods: getCandidates(), lookupByName(), and typed candidate query wrappers now correctly propagate allocation exceptions rather than calling std::terminate(). No behavioral change under normal memory conditions."

---

## Verdict

**SHIP-READY after Phases 8–9 complete.** The implementation plan resolves every remaining open item from the architectural analysis with a projected final composite score of ~9.30 / 10 (up from current ~9.18). Priority order: P8 → P9.

### Post-Merge Deliverables Checklist

After both phases land, complete the following:

| Item | Description |
|------|-------------|
| **Cycle-time benchmarking** | Run full integration pass on target ARM hardware (EtherCAT + GPIO setups) with performance profiling. Registry lookup is init-only — zero expected RT impact — but confirm sub-100µs cycle times are maintained through the new dispatch path. Compare readAll/writeAll/scan timings before vs. after P8 merge. |
| **CHANGELOG.md entry** | Add top-level \`CHANGELOG.md\` (if not already present) or update existing one with summary: "Completed full OCP compliance via BackendRegistry self-registration; fixed incorrect noexcept contracts on ContextObject diagnostic methods; added REGISTER_BACKEND macro helper for external backend authors." |
| **New Backend Guide** | Create \`doc/NewBackendGuide.md\` covering the end-to-end process of adding a custom transport: include \`registration.h\`, implement IBackendScanner + IRuntimeAdapter, invoke REGISTER_BACKEND macro once at file scope, document configure(configMap) hook contract and per-backend parameter conventions. This cements the "growing library" goal by making extension frictionless. |
| **Examples & dh-discover regression guards** | Verify all existing examples in \`examples/\` still compile and run correctly; verify \`dh-discover\` tool discovers all registered backends without hardcoded fallback paths. |

### Next Steps After All Phases Complete

After post-merge deliverables are complete, consider these optional follow-ups ranked by impact:
1. **C++20 heterogeneous lookup** in nameToUuid map (eliminates temporary string allocation in \`lookupByName\`; requires careful lifetime auditing)
2. **Per-DHDO frozen_ guard** if external plugin ecosystem grows beyond built-in backends (OI-03 reconsideration trigger)
3. **Plugin-style dynamic loading** via dlopen/dlclose for backends compiled as shared libraries loaded at runtime (beyond static self-registration model)
