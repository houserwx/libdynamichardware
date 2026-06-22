# Copilot Agent Code Quality Guidelines

Rules for AI-generated code changes in libdynamichardware.

## 1. SOLID Compliance

### Single Responsibility Principle (SRP)

Each class has one reason to change:

| Class | Single Responsibility |
|---|---|
| `HardwareCatalog` | Load/validate JSON, expose channel definitions |
| `HardwareRegistry` | Map names to runtime addresses |
| `IBackendScanner` | Pure-data discovery scan |
| `IDHDOBuilder` | Build DHDO from channel list |
| `IRuntimeAdapter` | RT lifecycle hooks + read/writeAll |
| `SignalProcessClock` | High-resolution timestamp generation |

**Rule:** If a method doesn't directly serve the class's responsibility, move it elsewhere.

### Open-Closed Principle (OCP)

Add backends without modifying shared code. New backend = new `.h/.cpp` pair implementing `IBackendScanner`. Copy an existing backend as template. No changes to `examples/`, `tests/`, or shared headers needed.

### Liskov Substitution Principle (LSP)

Runtime adapters must be substitutable for their base types without breaking assumptions. Consumers call generic methods; transport-specific behavior is hidden inside the adapter implementation.

### Interface Segregation Principle (ISP)

No fat interfaces. Each interface serves exactly one consumer role:

- **IBackendScanner** — Discovery phase only
- **IDHDOBuilder** — Configuration/mapping phase only  
- **IRuntimeAdapter** — RT cycle hooks (inherits builder)
- **IHardwareCatalog** — Read-only JSON access

### Dependency Inversion Principle (DIP)

High-level orchestration depends on abstractions (`IBackendScanner*`, `IDHDOBuilder*`). Concrete backends depend on those same abstractions. The orchestrator never includes concrete backend headers.

## 2. Runtime Safety Rules

Every change affecting the hot path must pass this checklist:

- [ ] Zero allocations in RT loop (no `new`/`malloc`/`std::string`)
- [ ] Contiguous vector iteration only (range-for or index loops)
- [ ] No virtual calls per entry (only 2 per backend: `onBeforeReadInputs` + `onAfterWriteOutputs`)
- [ ] All RT methods marked `noexcept`
- [ ] No locks, mutexes, or atomic operations
- [ ] Single `clock_gettime` call via vDSO (~10ns overhead)
- [ ] Cache-friendly memory layout verified for process image structures

If any item is violated, the change needs review before merging.

## 3. Testing Requirements

| Test Type | Framework | Location | Purpose |
|---|---|---|---|
| Unit tests | Catch2 | `tests/test_*.cpp` | Isolated class logic |
| Integration tests | Catch2 | `tests/signal_process.cpp` | Full pipeline behavior |

**Test tags:** `.#[backend]` for backend-specific, `[Slow]` for expensive setup. Run all tests before committing; minimum 96 cases / 375 assertions must pass.

**Rule:** Every new public method gets at least one unit test covering the happy path and one edge case.

## 4. Documentation Standards

- Update docs in the same commit as code changes
- Keep inline comments focused on *why*, not *what*
- Public API methods get Doxygen-style headers
- Architecture.md reflects actual interface signatures (verify against source)
