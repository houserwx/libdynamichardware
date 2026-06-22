# Implementation Plan — CurrentAnalysis Findings Remediation

| Field       | Value                                                                 |
|-------------|-----------------------------------------------------------------------|
| **Source**  | `doc/CurrentAnalysis.md` (commit b348cd0)                            |
| **Date**    | 2026-06-22                                                            |
| **Status**  | REVIEWED — approved with improvements incorporated                    |

---

## Overview

This plan addresses all open items from the architectural analysis, organized by priority and dependency graph. Each phase produces a compilable intermediate state with passing tests.

### Phase Summary

| Phase | Title | OI Items Addressed | Risk Level | Estimated Effort |
|-------|-------|-------------------|------------|-----------------|
| P1 | DHDO read/write → composable bitmask dispatch | OI-02 ✅ | Medium | High (core RT path refactor) |
| P2 | Wire BackendRegistry + self-registration | OI-01 ✅ | Low-Medium | High (orchestrator rewrite) |
| P3 | Builder.enableBackend → registry-driven lookup | OI-01 ✅ (continued) | Low | Medium |
| P4 | PhaseManager: enforce transitions instead of swallowing exceptions | OI-06 ✅ | Low | Small |
| P5 | Simulated backend: reinterpret_cast → memcpy consistency | OI-08 ✅ | Very Low | Small |
| P6 | DHDOFactory: thread-safe entryTypeToString() | OI-04 ✅ | Very Low | Small (pre-RT only) |
| P7 | Constexpr annotations + minor cleanups | OI-05, OI-07, OI-09 ✅ | None | Trivial |

**Execution order matters**: P1 must land before P2/P3 because the orchestrator's build phase calls into DHDO methods that will change their internal dispatch. However P2 and P3 can be developed in parallel since they're independent concern areas (registry wiring vs builder API).

### Validation & Benchmarking Requirements

Every phase requires these validation gates before merge:

| Gate | Description | Tool / Method |
|------|-------------|---------------|
| **Compilation** | Clean build on all target configurations (x86_64, ARM cross-compile) | CMake configure + Build_CMakeTools |
| **Unit tests** | All existing `tests/*.cpp` pass via CTest | RunCtest_CMakeTools |
| **Static analysis** | clang-tidy clean; `-fsanitize=undefined` passes on key files (DHDO.cpp, SimulatedRTBackend.cpp) | Compile with `-fsanitize=undefined -fno-sanitize-recover=all` then run demo binary |
| **Cycle benchmark** (P1 only) | Measure RT cycle time on target ARM hardware: compare switch-based vs bitmask-dispatch readAll/writeAll latency | Add timing harness to simulated_io_demo wrapping N cycles of readAll/writeAll with `signalProcessTickNow()` fences; verify sub-100µs for typical channel counts |
| **Integration smoke test** (P2+P3 only) | Register a mock backend at runtime through BackendRegistry, verify discover→build→freeze→readAll/writeAll end-to-end path works without hardcoded if-block involvement | New `tests/test_registry_integration.cpp` exercise |

### CI / Testing Strategy

- Existing CTest infrastructure covers unit tests. After P2/P3 merge, add an integration test target that exercises the full registry-driven path.
- Cross-compile paths (`ETHERCAT_LIB`, libgpiod optional flags) remain unbroken — each phase's "Files Modified" table lists exact touch points; backend-specific headers are NOT changed by non-backend phases.
- The `dh-discover` tool and both demos (`ethercat_demo`, `simulated_io_demo`) serve as consumer-facing regression guards — they must compile and link after every phase.

---

## Phase 1 — DHDO read/write: switch(type) → composable bitmask dispatch

### Problem

`DHDOEntry::read()` and `write()` use `switch(type)` on individual enum values, completely ignoring the composable bitmask system (`entryValueFormat`, `entryBitSize`). This means:

- **Int8Input**, **Int8Output**, **Int32Output** are defined in the enum but silently do nothing at runtime (hit empty default case)
- Every new EntryType composition requires editing these switch statements — violating OCP
- The constexpr bitmask extractors exist as dead code in the hot path

Current state:
```cpp
void DHDOEntry::read() noexcept {
    if (!image) return;
    switch (type) {
        case EntryType::BoolInput:   { /* bit extraction + debounce */ break; }
        case EntryType::Int32Input:  { /* memcpy int32Val_ */       break; }
        case EntryType::Int16Input:  { /* memcpy int16Val_ */       break; }
        case EntryType::FloatInput:  { /* memcpy floatVal_ */       break; }
        default:                     { /* SILENT NO-OP for Int8Input, MessageIn, etc. */ break; }
    }
}
```

### Solution

Replace the switch with bitmask-driven dispatch using the existing constexpr extractors. The key insight: `entryValueFormat()` extracts direction+base+size into a single value that uniquely identifies the data layout, and `entryBitSize()` tells us how many bits to read. This makes ALL composed types work automatically without code changes.

**New read():**
```cpp
void DHDOEntry::read() noexcept {
    if (!image) return;

    uint8_t fmt = entryValueFormat(type);       // direction | base | size
    bool is_signed = entryIsSigned(type);

    // Bool input — special handling (bit-level + debounce)
    if ((fmt & DIR_INPUT) && (fmt & BASE_BOOL)) {
        const uint8_t byte = *image;
        const bool raw = (byte >> bitOffset) & 1u;
        boolVal_ = debounce.filter(raw, dynamichardware::rt::signalProcessNowNs());
        return;
    }

    // Numeric inputs — memcpy based on size extracted from bitmask
    if (!(fmt & DIR_INPUT)) return;             // Not an input type → skip
    if (entryIsMessage(type)) return;           // MessageIn handled by adapter hooks

    switch (entryBitSize(type)) {
        case SZ_32:                             // Int32Input, FloatInput
            if ((fmt & BASE_FLOAT)) {
                std::memcpy(&floatVal_, image, sizeof(float));
            } else {                            // Int32Input (signed integer)
                std::memcpy(&int32Val_, image, sizeof(int32_t));
            }
            break;
        case SZ_16:                             // Int16Input (signed integer)
            std::memcpy(&int16Val_, image, sizeof(int16_t));
            break;
        case SZ_8:                              // Int8Input (NEW — now works!)
            int8_t val;
            std::memcpy(&val, image, sizeof(int8_t));
            int16Val_ = static_cast<int16_t>(val);  // Store in existing cache field
            break;
        default:                                // SZ_1 or unknown → skip safely
            break;
    }
}
```

**New write():** Same pattern for outputs:
```cpp
void DHDOEntry::write() noexcept {
    if (!image) return;

    uint8_t fmt = entryValueFormat(type);

    // Bool output — special handling (pulse machine + bit-level)
    if ((fmt & DIR_OUTPUT) && (fmt & BASE_BOOL)) {
        const bool pinState = pulse.tick(dynamichardware::rt::signalProcessNowNs());
        if (pinState)     *image |=  static_cast<uint8_t>(1U << bitOffset);
        else              *image &= static_cast<uint8_t>(~(1U << bitOffset));
        return;
    }

    // Numeric outputs — memcpy based on size extracted from bitmask
    if (!(fmt & DIR_OUTPUT)) return;            // Not an output type → skip
    if (entryIsMessage(type)) return;           // MessageOut handled by adapter hooks

    switch (entryBitSize(type)) {
        case SZ_32:                             // Int32Output, FloatOutput (NEW!)
            if ((fmt & BASE_FLOAT)) {
                std::memcpy(image, &floatDesired_, sizeof(float));
            } else {                            // Int32Output (NEW — now works!)
                int32_t val = static_cast<int32_t>(int16Desired_);  // TODO: add int32Desired_ cache field?
                std::memcpy(image, &val, sizeof(int32_t));
            }
            break;
        case SZ_16:                             // Int16Output
            std::memcpy(image, &int16Desired_, sizeof(int16_t));
            break;
        case SZ_8:                              // Int8Output (NEW — now works!)
            int8_t val = static_cast<int8_t>(int16Desired_);
            std::memcpy(image, &val, sizeof(int8_t));
            break;
        default:                                // SZ_1 or unknown → skip safely
            break;
    }
}
```

### Cache Field Gap Analysis

| EntryType | Read needs cache field? | Write needs desired state field? | Current fields sufficient? |
|-----------|------------------------|----------------------------------|--------------------------|
| BoolInput/Output | boolVal_ / pulse machine | pulse arm() | ✅ Yes |
| Int8Input | Needs temporary (stored in int16Val_) | N/A | ⚠️ Works but loses precision info — consumer calls getInt16() for an 8-bit value. Acceptable since getBool returns bool and the type field is truth. |
| Int16Input/Output | int16Val_ / int16Desired_ | int16Desired_ | ✅ Yes |
| Int32Input | int32Val_ | N/A | ✅ Yes |
| Int32Output | N/A | **NO int32Desired_ field exists** | ❌ GAP — need `setInt32()` + `int32Desired_` |
| FloatInput/Output | floatVal_ / floatDesired_ | floatDesired_ | ✅ Yes |

**Action required:** Add `int32_t int32Desired_{0};` to DHDOEntry's private write-side cache, add `void setInt32(int32_t v) noexcept;`, and update `write()` SZ_32 INT case to use it instead of casting from int16Desired_.

**Additional cache field note:** Consider adding `int8_t int8Val_{}` / `int8_t int8Desired_{}` for full precision on 8-bit types. However, promoting Int8→Int16 is acceptable for simplicity (same rationale as getBool returning bool despite the type field being truth). The promoted approach avoids growing the struct by 4 bytes when no consumer currently depends on distinguishing Int8 vs Int16 at runtime.

**Inlining for RT targets:** Moving read()/write() inline in the header should include `[[gnu::always_inline]]` annotation (or project-wide `-finline-functions` at -O2+) to guarantee the compiler folds them into the per-entry loop body — eliminating call overhead entirely. This is critical because these methods are called N times per cycle where N = total entry count across all backends.

**Template helper option:** For further branch reduction, consider compile-time template instantiation for numeric dispatch:
```cpp
template<uint8_t SizeBits, bool IsFloatType>
struct NumericReadDispatcher {
    static void apply(DHDOEntry& e) noexcept { /* specialized memcpy path */ }
};
// Specializations for SZ_32+FLOAT, SZ_32+INT, SZ_16+INT, SZ_8+INT
```
The bitmask switch-on-size remains as the primary dispatch path; templates can coexist for clarity and let the optimizer eliminate branches when EntryType is known at compile time (e.g., via constexpr if in templated consumer code).

**MessageIn/Out confirmation:** Adapters populate `msgSlot_` through their own lifecycle hooks (`onBeforeReadInputs` / `onAfterWriteOutputs`). The generic sweep correctly skips message types with an early return — no change needed here.

| File | Change |
|------|--------|
| `include/dynamichardware/dhdo/DHDO.h` | Add `int32_t int32Desired_{0}` to private cache fields; add `void setInt32(int32_t) noexcept`; move read()/write() declarations to inline in header (RT optimization — already marked noexcept, makes them compiler-inlineable without LTO) |
| `src/dynamichardware/dhdo/DHDO.cpp` | Rewrite read()/write() to use bitmask dispatch; remove empty default cases that silently skip Int8/Int32Output types |

### Tests Required

- `tests/test_dhdo_factory.cpp`: Verify Int8Input, Int8Output, Int32Output now produce correct values through read/write cycle
- Existing tests should all pass with no behavioral change for BoolInput/Int16/Float types (regression guard)

### Risk Assessment

**Medium risk.** This touches the core RT hot path. Mitigated by:
1. The bitmask extractors are constexpr and well-tested via existing registry filtering code
2. Existing entry types (Bool, Int16, Float) have identical data paths — just routed through different branch logic instead of switch case labels
3. Full test suite validates all current entry types before new ones are exercised
4. Can be verified with cycle-time benchmarking on target hardware

---

## Phase 2 — Wire BackendRegistry + self-registration

### Problem

BackendRegistry is fully implemented but completely unused:
- Zero backend modules call `registerBackend()` at static init time
- Orchestrator instantiates backends directly with hardcoded type names in two methods (`runDiscoveryScan`, `buildRT`)
- The registry ships into the library binary as dead weight (~5KB object code that never executes)

This violates OCP because adding a transport requires editing the orchestrator source in three places (builder string chain + orchestrator ×2).

### Solution

Two-sided wiring: **backends register themselves**, **orchestrator queries the registry**.

#### Step 2A: Add self-registration to each backend module

Each `{Transport}Discovery.cpp` adds a static initializer that calls `BackendRegistry::registerBackend()`. This follows the standard plugin pattern where the creator function captures any construction parameters needed by both scanner and adapter.

**Example — `src/dynamichardware/backends/ethercat/EthercatDiscovery.cpp`:**
```cpp
// Static registration at load time — registers "EtherCAT" with the global BackendRegistry
// Uses call_once to guard against multiple registration attempts across TUs or dynamic reloading
static std::once_flag sRegistrationFlag;

static void registerEthercatBackend() {
    config::BackendRegistry::registerBackend("EtherCAT",
        [](
            const std::string& /*name*/,
            const std::unordered_map<std::string, std::string>& /*config*/
        ) -> std::pair<std::unique_ptr<dhdo::IBackendScanner>, 
                      std::unique_ptr<dhdo::IRuntimeAdapter>> {
        return {
            std::make_unique<ethercat::EthercatDiscovery>(),
            std::make_unique<ethercat::EthercatRTBackend>()
        };
    });
}

// Ensure registration runs exactly once before main()
namespace {
    struct Registrar {
        Registrar() { std::call_once(sRegistrationFlag, registerEthercatBackend); }
    };
    static constexpr Registrar sRegistrar{};
}
```

Same pattern for all five backends (GPIO, I2C, SPI, Simulated). Each registers under its canonical name string ("GPIO", "I2C", "SPI", "Simulated").

**Creator signature enhancement:** The lambda accepts `(name, configMap)` parameters so creator functions can receive backend-specific configuration at construction time. This allows the orchestrator to pass through per-backend config values from `OrchestratorState.enabledBackends[name]` without requiring post-construction setup calls.

**Self-registration safety note — documented for future backend authors:**
The call_once guard handles edge cases where multiple TUs might trigger registration (e.g., if a discovery header is included in several translation units and defines inline static variables). Pattern is:
1. File-scope `static std::once_flag` + registration function
2. Namespace-scope RAII struct whose constructor calls `std::call_once(flag, reg_fn)`
3. Static constexpr instance ensures initialization happens during C++ static init phase with exactly-once guarantee
This pattern should be copied verbatim by any external backend author adding transports.

#### Step 2B: Rewrite orchestrator to query registry instead of hardcoded types

Replace the two methods that currently have hardcoded if-blocks:

**Current `runDiscoveryScan()` (~60 lines of if-blocks) → new version:**
```cpp
bool HardwareOrchestrator::runDiscoveryScan() {
    // Collect enabled backend names from orchestrator state into a set for O(1) lookup
    std::unordered_set<std::string> enabledNames(state_.enabledBackends.begin(), 
                                                 state_.enabledBackends.end());

    // Query registry — only scan backends that are both registered AND enabled
    for (const auto& name : config::BackendRegistry::getAll()) {
        if (!enabledNames.count(name)) continue;

        const auto* creator = config::BackendRegistry::getCreator(name);
        if (!creator) {
            SPDLOG_WARN("Backend '{}' requested but not found in registry", name);
            return false;  // Unknown or unregistered backend name
        }

        // Pass backend-specific config map through to the creator function
        const auto& cfgMap = state_.enabledBackends[name];
        auto [scanner, adapter] = (*creator)(name, cfgMap);
        
        // Discovery phase: scanner returns pure data, orchestrator feeds catalog
        auto descriptors = scanner->scan();
        for (auto& desc : descriptors) {
            catalog_.addEntry(std::move(desc));
        }
        
        // Store adapters in temporary vector for buildRT() to consume later
        pendingAdapters_[name] = std::move(adapter);
    }
    
    return true;
}
```

**Current `buildRT()` (~70 lines of if-blocks) → new version:**
```cpp
std::unique_ptr<DynamicHardwareContextObject> HardwareOrchestrator::buildRT() {
    dhdo::HardwareRegistry registry;

    for (auto& [name, adapter] : pendingAdapters_) {
        // Extract mapped channels for this backend by filtering catalog entries
        MappedChannels channels = extractChannelsForBackend(name);
        if (!adapter->build(channels)) {
            SPDLOG_WARN("Backend '{}' build failed", name);
            continue;  // Non-fatal: other backends may still succeed
        }

        // Add built adapter to registry — transfers ownership
        registry.addBackend(std::move(adapter));
    }

    // Validate: at least one backend should have succeeded
    if (registry.empty()) {
        SPDLOG_ERROR("No backends successfully built — context will be empty");
    }

    return std::make_unique<DynamicHardwareContextObject>(
        DynamicHardwareContextObject::Impl{
            .registry = std::move(registry),
            .catalog  = std::move(catalog_),
            .nameToUuid = {}  // Built during freeze phase
        }
    );
}
```

### Files Modified

| File | Change |
|------|--------|
| `src/dynamichardware/backends/ethercat/EthercatDiscovery.cpp` | Add static self-registration block at file scope |
| `src/dynamichardware/backends/gpio/GPIODiscovery.cpp` | Same pattern |
| `src/dynamichardware/backends/i2c/I2CDiscovery.cpp` | Same pattern |
| `src/dynamichardware/backends/spi/SPIDiscovery.cpp` | Same pattern |
| `src/dynamichardware/backends/simulated/SimulatedDiscovery.cpp` | Same pattern |
| `include/dynamichardware/HardwareOrchestrator.h` | Replace hardcoded if-blocks in runDiscoveryScan/buildRT with registry-driven iteration; add private member: `std::unordered_map<std::string, std::unique_ptr<dhdo::IRuntimeAdapter>> pendingAdapters_` to hold adapters between scan and build phases |
| `src/HardwareOrchestrator.cpp` | Implement new registry-driven methods (remove ~130 lines of hardcoded backend instantiation) |

### Tests Required

- Update `tests/test_backend_registry.cpp`: Verify all 5 built-in backends are registered after library loads (static init runs before test main); verify creator function accepts config map parameter correctly
- New `tests/test_registry_integration.cpp`: Register a mock backend at runtime (not via static init), call `.enableBackend()`, run full discover→build→freeze→readAll/writeAll cycle, and verify end-to-end data flow without any hardcoded if-block involvement. This proves external plugin backends work.
- Regression test: All existing unit tests pass — behavior should be identical for consumers since the public API surface is unchanged
- Cross-compile validation: Verify CMake configure succeeds when EtherCAT lib is absent (stub mode) and GPIO lib is absent to ensure conditional compilation paths remain intact

### Risk Assessment

**Low-Medium risk.** The orchestrator internals change significantly but the consumer-facing API (`DynamicHardwareBuilder`) does not. Static initialization order is well-defined within a single translation unit but across TUs there's no guarantee — mitigated by lazy lookup at runtime (registry is queried during discover() which always runs after static init completes).

---

## Phase 3 — Builder.enableBackend → registry-driven lookup

### Problem

`DynamicHardwareBuilder::enableBackend()` has a hardcoded string-comparison chain that requires source edits per new backend. This is one of three OCP violation sites identified in the analysis.

```cpp
// Current: requires editing for every new transport name
if (name == "EtherCAT") { st.enableEthercat = true; ... }
else if (name == "GPIO")  { st.enableGPIO = true;     ... }
// ... etc
```

After Phase 2, the BackendRegistry knows all available backend names. This phase removes the string-parsing code from the builder and delegates to the orchestrator using a simple `std::set<std::string>` of enabled names instead of per-backend boolean flags.

### Solution

#### Step 3A: Replace OrchestratorState boolean flags with enabled backends set

**Current:**
```cpp
struct OrchestratorState {
    bool enableEthercat{false};
    uint32_t ethercatCycleNs{1'000'000u};
    bool enableGPIO{false};
    bool enableI2C{false};
    std::string i2cBusPath{"/dev/i2c-1"};
    // ... per-backend config scattered across struct fields
};
```

**New:**
```cpp
struct OrchestratorState {
    std::unordered_map<std::string, 
                       std::unordered_map<std::string, std::string>> enabledBackends;
    // Key = backend name ("EtherCAT", "CustomTransport"), 
    // Value = config map {"cycleNs": "1000000", "busPath": "/dev/i2c-1", etc.}
    
    std::string catalogPath{"hardware.json"};
    std::string mappingPath;
};
```

This is OCP-compliant because adding a new transport requires zero changes to this struct — it's just another key in the map. Backend-specific config values (like `ethercatCycleNs`) live in the config sub-map and are passed through to the adapter at construction time via the creator function or a post-construction configure hook.

#### Step 3B: Rewrite Builder.enableBackend() as pure delegation

```cpp
DynamicHardwareBuilder& DynamicHardwareBuilder::enableBackend(
        std::string name,
        const std::unordered_map<std::string, std::string>& config) {
    
    // Delegate entirely to orchestrator state — no string parsing here
    orchestrator_->state_.enabledBackends[name] = config;
    return *this;
}
```

The builder now has **zero knowledge** of which backends exist. It passes the name string through verbatim. Validation happens later during discover() when the registry lookup either succeeds or returns nullptr (logged as an error).

### Files Modified

| File | Change |
|------|--------|
| `include/dynamichardware/HardwareOrchestrator.h` | Replace per-backend boolean flags with `std::unordered_map<string, map<string,string>> enabledBackends`; remove hardcoded config fields (`i2cBusPath`, `ethercatCycleNs`, etc.) |
| `src/HardwareOrchestrator.cpp` | Update runDiscoveryScan/buildRT to read from enabledBackends map instead of flag fields; pass config sub-map to adapter creator function for backend-specific parameterization |
| `src/DynamicHardwareBuilder.cpp` | Remove entire enableBackend implementation (~30 lines of if-else chain); replace with single delegation line |

### Tests Required

- Verify `.enableBackend("EtherCAT", {"cycleNs": "500000"})` still works correctly after removal of Builder's internal parsing logic
- Verify unknown backend names are handled gracefully at discover time (not silently ignored)
- Regression: all existing fluent API chains produce identical behavior

### Risk Assessment

**Low risk.** This is a pure refactoring — same inputs, same outputs, different internal representation. The orchestrator state struct changes but it's private and only accessed by the builder (via friend). Consumer-facing API is unchanged.

---

## Phase 4 — PhaseManager: enforce transitions instead of swallowing exceptions

### Problem

The orchestrator wraps every `phaseManager_.advance()` call in `try { ... } catch (...) {}`, making phase enforcement advisory rather than mandatory. Calling `discover()` after `buildRT()` silently proceeds instead of failing loudly.

```cpp
// Current pattern — hides programming errors during development:
bool HardwareOrchestrator::discover() {
    try { phaseManager_.advance(HardwarePhase::DISCOVERY); }
    catch (...) {}  // ← Swallows ALL exceptions including illegal transitions
    
    // Proceeds even if we're past DISCOVERY phase
}
```

### Solution

Replace blanket exception swallowing with explicit error handling that returns false on illegal transitions while still allowing legitimate re-discovery scenarios:

#### Step 4A: Add "reset" capability to PhaseManager for intentional re-scanning

```cpp
class PhaseManager {
public:
    bool resetToDiscovery();  // Explicit opt-in: RUNNING→DISCOVERY allowed only via this method
                              // Returns true if successful; false otherwise
    
    // Existing advance() remains strict — throws std::invalid_argument on illegal transition
};
```

This distinguishes between **programming mistakes** (accidentally calling discover twice) and **intentional re-discovery** (hardware hot-plug, configuration reload). The former gets a loud failure in debug builds; the latter uses an explicit API call.

#### Step 4B: Rewrite orchestrator methods to handle phase errors explicitly

```cpp
bool HardwareOrchestrator::discover() {
    if (!phaseManager_.isAt(HardwarePhase::DISCOVERY)) {
        // Not at discovery phase — log warning but allow idempotent calls during same phase cycle
        auto current = phaseManager_.get();
        if (current > HardwarePhase::DISCOVERY) {
            SPDLOG_WARN("discover() called after buildRT() — use resetToDiscovery() first");
            return false;  // Fail loudly instead of silently proceeding
        }
    }
    
    try { phaseManager_.advance(HardwarePhase::DISCOVERY); } 
    catch (...) {}  // Only catches same-phase-advance (idempotency guard), not backward transitions
    
    // ... rest of discovery logic unchanged
}
```

### Files Modified

| File | Change |
|------|--------|
| `include/dynamichardware/config/PhaseManager.h` | Add `resetToDiscovery()` method with explicit opt-in for re-scanning scenarios |
| `src/HardwareOrchestrator.cpp` | Replace blanket `catch (...)` with explicit error handling that returns false on illegal transitions |

### Tests Required

- Verify discover() fails when called after buildRT() without resetToDiscovery()
- Verify intentional re-discovery via resetToDiscovery() works correctly
- Regression: normal DISCOVERY→MAPPING→BUILD_RT flow still succeeds

### Risk Assessment

**Very low risk.** This makes errors more visible during development without changing production behavior. The existing code silently ignored these cases anyway, so returning false is a strict improvement.

---

## Phase 5 — Simulated backend: reinterpret_cast → memcpy consistency

### Problem

The simulated RT backend uses `reinterpret_cast<float*>(image)` for float writes, violating C++ strict aliasing rules if alignment is wrong. The DHDO layer itself uses `memcpy` (correct anti-aliasing pattern). This inconsistency within the library's own codebase standards is confusing and technically UB.

Current code in `SimulatedRTBackend::onBeforeReadInputs()`:
```cpp
case dhdo::EntryType::FloatInput: {
    // Technically UB if image pointer isn't float-aligned
    *(reinterpret_cast<float*>(entry.image + sim.byteOffset)) = value;
}
```

### Solution

Replace all reinterpret_cast patterns with std::memcpy throughout the simulated backend, matching the DHDO layer's approach:

```cpp
case dhdo::EntryType::FloatInput: {
    float value = std::sin(sim.phase) * sim.amplitude + sim.offset;
    std::memcpy(entry.image + sim.byteOffset, &value, sizeof(float));
}
```

Same fix for any int8/16/32 casts that use pointer arithmetic on uint8_t*.

### Files Modified

| File | Change |
|------|--------|
| `src/dynamichardware/backends/simulated/SimulatedRTBackend.cpp` | Replace ALL `reinterpret_cast<T*>(image)` with `std::memcpy(image, &value, sizeof(T))` — approximately 5-7 locations across onBeforeReadInputs() |

### Tests Required

- Existing simulated backend tests should pass with identical behavior (memcpy produces same bit pattern as cast when alignment is correct)
- Add static analysis check: clang-tidy `-fsanitize=undefined` passes without aliasing violations

### Risk Assessment

**Very low risk.** memcpy and reinterpret_cast produce identical results when alignment is correct (which it always is for vector<uint8_t> on ARM/x86). This change only fixes technical UB — no behavioral difference.

---

## Phase 6 — DHDOFactory: thread-safe entryTypeToString()

### Problem

`DHDOFactory::entryTypeToString()` uses a static char buffer (`static char buf[32]`) making it non-reentrant and not thread-safe for concurrent calls during init phase.

```cpp
const char* DHDOFactory::entryTypeToString(EntryType type) {
    // ... 
    static char buf[32];  // ← Thread-safety hazard
    snprintf(buf, sizeof(buf), "0x%02X", static_cast<unsigned>(type));
    return buf;           // Returns pointer to shared mutable state
}
```

This is strictly pre-RT only (factory methods run before freezeForRt()), so it doesn't affect RT determinism scores. It's purely a SOLID concern — the function contract promises a `const char*` but returns a pointer to shared mutable storage that gets clobbered by the next call.

### Solution

Replace static buffer with thread-local storage or return std::string instead of const char*. Since this is an init-time utility called infrequently, correctness matters more than performance.

**Option A — Return std::string (cleanest, changes API):**
```cpp
std::string DHDOFactory::entryTypeToString(EntryType type);
// Caller owns the string lifetime — no shared mutable state
```

**Option B — Thread-local buffer (preserves current signature):**
```cpp
static thread_local char buf[32];  // Each thread gets its own buffer
```

Recommendation: **Option A** because it eliminates all lifetime concerns and is consistent with how modern C++ handles dynamic strings. The existing callers are infrequent (catalog serialization, debug output) where returning a temporary string has zero practical cost.

### Files Modified

| File | Change |
|------|--------|
| `include/dynamichardware/dhdo/DHDOFactory.h` | Change `entryTypeToString()` return type from `const char*` to `std::string`; remove `#include <cstdio>` if only used for snprintf |
| `src/dynamichardware/dhdo/DHDOFactory.cpp` | Update implementation; replace static buffer with direct string construction or stringstream formatting |
| All callers of entryTypeToString() | Update to accept std::string instead of const char* (likely just `.c_str()` in format calls — trivial change) |

### Tests Required

- Verify catalog JSON serialization still produces correct output after API change
- No new tests needed — this is a pure correctness fix with no behavioral change when called single-threaded

### Risk Assessment

**Very low risk.** Pre-RT init-time code path. Return type change affects callers but the diff is mechanical (add `.c_str()` where a const char* was expected).

---

## Phase 7 — Constexpr annotations + minor cleanups

### Problem Summary

Three low-priority items that are cheap to fix:

1. **OI-05**: `isInputEntryType`/`isOutputEntryType` in HardwareRegistry are NOT marked constexpr despite being pure bitwise operations
2. **OI-07**: DynamicHardwareContextObject names its inline member grouping "Impl" suggesting pImpl pattern, but it's not behind a pointer — misleading naming convention  
3. **OI-09**: signalProcessTickNow has POSIX dependency without debug assert for multi-threaded misuse detection

### Solution

#### 7A: Mark registry filtering functions constexpr

```cpp
// In HardwareRegistry.h — add constexpr (these are already static inline):
static constexpr bool isInputEntryType(EntryType t) noexcept { ... }
static constexpr bool isOutputEntryType(EntryType t) noexcept { ... }
```

This enables use in template constraints and static_assert contexts. Zero behavioral change — compiler already optimizes these aggressively at -O2+.

#### 7B: Rename Impl → InternalState

```cpp
// In DynamicHardwareContextObject.h:
struct InternalState {  // Was: struct Impl {
    dhdo::HardwareRegistry registry;
    dhdo::HardwareCatalog  catalog;
    std::unordered_map<std::string, std::string> nameToUuid;
};

InternalState internal_;  // Was: Impl impl_;
```

Pure rename with no behavioral impact. More honestly represents that this is an inline composition grouping rather than an opaque implementation pointer.

#### 7C: Add debug assertion to gSignalProcessNowNs access pattern

Add a compile-time gate that detects multi-threaded access in debug builds:

```cpp
#ifdef DEBUG
inline void signalProcessAssertSingleThread() noexcept {
    static thread_local int accessed = 0;
    assert(accessed == 0 && "signalProcessTickNow called from multiple threads");
}
#endif
```

Note: This can't actually detect concurrent access (that requires atomics which defeats the purpose), but it CAN catch sequential reuse from different threads within the same cycle if disciplined usage is violated. Better approach: document clearly and trust consumer discipline — add comment block making the invariant explicit.

### Files Modified

| File | Change |
|------|--------|
| `include/dynamichardware/dhdo/HardwareRegistry.h` | Add `constexpr` to isInputEntryType/isOutputEntryType declarations |
| `include/dynamichardware/DynamicHardwareContextObject.h` | Rename `Impl` → `InternalState`, `impl_` → `internal_` throughout |
| `include/dynamichardware/rt/SignalProcess.h` | Expand documentation comment on single-thread invariant; no runtime check added (would require atomic, defeating the ~10ns cost goal) |

### Tests Required

None beyond existing test suite — all changes are annotations or renames with zero behavioral impact.

### Risk Assessment

**Zero risk.** Purely additive changes (constexpr keyword allows more compiler optimization) or cosmetic renames.

---

## Dependency Graph and Execution Order

```
Phase 1 ─────────┐
                 ├──> Phase 2 ──────> Phase 3  (OCP fix completes here)
Phase 4          │                        │
   │             │                        ↓
   ↓             │               All OCP violations resolved
Phase 5 ─────────┘               (Builder + Orchestrator wired to registry)
   │                            (DHDO uses bitmask dispatch)
   ↓                            
Phase 6 ───────────> Phase 7     (Final cleanup pass)
```

**Parallelizable work:** Phases 4, 5, and 6 are independent of each other and can be developed concurrently once Phase 1 is merged. Phase 3 depends on both Phase 1 AND Phase 2 completing first.

### Recommended Merge Sequence

1. **Merge P7 first** (zero-risk constexpr renames — gets out of the way, no merge conflicts with later phases)
2. **Merge P1 next** (core RT path change — biggest risk, needs clean baseline before orchestrator touches DHDO methods)
3. **Merge P2+P3 together** (they're tightly coupled — registry wiring AND builder delegation must land in one commit or the orchestrator won't compile)
4. **Merge P4+P5+P6 as a batch** (all low-risk fixes that don't depend on each other)

---

## Score Impact Projection

After all phases complete, expected score changes:

| Layer | Dimension | Current | After Fix | Change | Reason |
|-------|-----------|---------|-----------|--------|--------|
| Builder + Orchestrator | SOLID | 8.2 | **9.2** | ⬆️ +1.0 | OCP violations eliminated: BackendRegistry wired, enableBackend delegates to registry, orchestrator uses polymorphic iteration instead of hardcoded if-blocks |
| DHDO | RT Determinism | 9.0 | **9.5** | ⬆️ +0.5 | Int8Input/Int8Output/Int32Output now produce correct values; switch replaced with bitmask dispatch making new types automatically work without code edits |
| DHDO | SOLID | 9.3 | **9.5** | ⬆️ +0.2 | entryTypeToString thread-safe; read/write methods use composable bitmask system consistently |
| Registry | SOLID | 9.3 | **9.4** | ⬆️ +0.1 | isInputEntryType/isOutputEntryType marked constexpr for template constraint usage |
| Simulated Backend | — (under Interfaces+Backends) | — | — | Minor fix-only | reinterpret_cast → memcpy consistency (no score change, just correctness improvement) |
| RT Utilities | SOLID | 9.4 | **9.4** | — no change | POSIX dependency remains (target platform is Linux); documentation improved but architecture unchanged |

### Projected New Composite

| Layer                          | RT Determinism | SOLID   | Composite            |
|--------------------------------|---------------:|--------:|---------------------:|
| Builder + Orchestrator         | N/A            | 9.2     | **9.2**              |
| DHDO                           | 9.5            | 9.5     | **(9.5·0.55 + 9.5·0.45) = 9.5** |
| Runtime Context                | N/A            | 9.0     | **9.0**              |
| Registry                       | 9.7            | 9.4     | **(9.7·0.55 + 9.4·0.45) = 9.57** |
| Catalog                        | 8.5            | 9.0     | **8.73**             |
| Interfaces + Backends          | 9.3            | 9.1     | **9.21**             |
| RT Utilities                   | 9.7            | 9.4     | **9.57**             |

> **Projected composite after all fixes: ~9.3 / 10** (up from current ~9.1)

---

## Open Items Tracking Matrix

| OI ID | Phase Addressed | Status After Plan Complete |
|-------|----------------|---------------------------|
| OI-01 (OCP in orchestrator) | P2, P3 ✅ | RESOLVED — BackendRegistry wired with self-registration; orchestrator uses polymorphic iteration; builder delegates to registry-driven map |
| OI-02 (Int8/Int32Output dead code) | P1 ✅ | RESOLVED — Bitmask dispatch handles ALL composed EntryType values automatically via entryValueFormat() + entryBitSize() extraction |
| OI-03 (No frozen_ in DHDO) | Not addressed — remains LOW priority | DEFERRED — Defense-in-depth already exists at HardwareRegistry level with frozen_ flag and logic_error on post-freeze addBackend(). Adding per-DHDO guard is belt-and-suspenders that adds no user value given the existing enforcement chain. Revisit if multi-backend-per-registry pattern emerges. |
| OI-04 (Static buffer in factory) | P6 ✅ | RESOLVED — Returns std::string instead of const char* to static buffer. Thread-safe for concurrent init-time calls. Note: pre-RT only, does not affect determinism scores. |
| OI-05 (constexpr missing) | P7 ✅ | RESOLVED — isInputEntryType/isOutputEntryType marked constexpr enabling template constraint usage |
| OI-06 (PhaseManager exception swallowing) | P4 ✅ | RESOLVED — Explicit error handling returns false on illegal transitions; resetToDiscovery() added for intentional re-scanning scenarios |
| OI-07 (Impl naming misleading) | P7 ✅ | RESOLVED — Renamed to InternalState with matching member variable name change |
| OI-08 (reinterpret_cast aliasing) | P5 ✅ | RESOLVED — All simulated backend casts replaced with memcpy matching DHDO layer anti-aliasing pattern |
| OI-09 (POSIX dependency / debug assert) | Partially addressed in P7 | PARTIAL — Documentation expanded to make single-thread invariant explicit. No runtime check added because it would require atomics that defeat the ~10ns cost goal. POSIX dependency accepted as target platform constraint (Linux/ARM). |

---

## Versioning & Changelog Impact

### After Phase 1 Merge (DHDO bitmask dispatch)
- **Breaking change:** `DHDOEntry` struct gains `int32_t int32Desired_{}` field — struct size increases by 4 bytes. This affects any consumer using `sizeof(DHDOEntry)` or manual memory layout assumptions.
- **New API surface:** `void setInt32(int32_t v) noexcept;` on DHDOEntry for Int32Output desired state.
- **Behavioral fix:** Int8Input, Int8Output, and Int32Output now produce correct values through read/write cycle instead of silently doing nothing.
- **Changelog entry:** "Fixed dead EntryType paths: Int8* and Int32Output now fully functional via composable bitmask dispatch replacing switch-based type routing."

### After Phase 2+3 Merge (BackendRegistry wiring + Builder delegation)
- **Internal-only change:** OrchestratorState struct changes from per-backend boolean flags to `enabledBackends` map. Not exposed publicly — no breaking change.
- **Consumer-facing benefit:** External backends can call `BackendRegistry::registerBackend(name, creator_fn)` then `.enableBackend(name)` through the fluent builder without library source edits.
- **Changelog entry:** "Wired BackendRegistry into orchestrator flow: all built-in backends self-register at load time, enabling extensible transport plugin architecture without source modifications."

### Documentation Tie-In Plan
For each phase that changes public API or behavior:
| Phase | Doc Update Needed |
|-------|-------------------|
| P1 | Update `doc/GettingStarted.md` with note about Int8*/Int32Output support; add before/after code snippet showing bitmask-dispatched read/write in action |
| P2+P3 | Add backend authoring guide section to `doc/Architecture.md`: how to implement IBackendScanner + IRuntimeAdapter, copy-paste registration pattern, and test with mock discovery |
| P6 | No doc update needed — internal API only (`entryTypeToString()` is init-time utility) |
| All phases | Verify examples/ compile against post-change headers; update any comments referencing old switch-based dispatch or hardcoded if-blocks |

---

## Verdict

**SHIP-READY after all 7 phases.** The implementation plan resolves every open item from the architectural analysis with a projected composite score of ~9.3 / 10 (up from current ~9.1). Priority order: P7 → P1 → (P2+P3 together) → (P4+P5+P6 batch).

After Phases 1–3 complete, perform a full integration pass on target hardware (EtherCAT + GPIO on ARM platform) with performance profiling to validate sub-100µs cycle times are maintained. Update `examples/` and `dh-discover` tool as regression guards.