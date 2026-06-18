# Analysis Update Directive — CIVControl-ARM

**Purpose:** This document instructs Copilot agents on exactly how and when to update `doc/CurrentAnalysis.md` so that the architecture analysis always reflects the current committed state and maintains the same rigour as the original SeparateAnalysis.txt baseline (9.5/10).

---

## When to Update CurrentAnalysis.md

Update the analysis after any commit that does one or more of the following:

- Adds, removes, or restructures a source file in `src/`
- Changes the RT cycle path in `Application.cpp`, `HardwareRegistry.cpp`, `PDO.cpp`, or any `*Adapter.cpp`
- Modifies `ProductState`, `ProductBuffer`, `CurrentProductCursor`, or any wrapper class
- Implements `FunctionEvaluator`, `FunctionState`, `WrapperPool`, or `FunctionConfig`
- Changes any init/freeze lifecycle (`freezeForRt`, `PDO::freeze`, `ProductBuffer`)
- Changes threading, scheduling, or memory model in `Threadrunner` or `Application`
- Adds or removes a file from `CMakeLists.txt`

Do **not** update CurrentAnalysis.md for documentation-only commits, comment changes, test changes, or CI script changes unless they reveal a design issue.

---

## How to Evaluate — Criteria and Scoring

### Dimension 1: RT Determinism (weighted 55%)

Score on a 0–10 scale. Use the table below. Each row is evaluated independently; missing or failing a row lowers the score.

| Criterion | Pass condition | Failure condition | Weight |
|---|---|---|---|
| **No allocation after freeze/init** | All `std::vector` capacity reserved before RT loop starts; no `push_back`, `resize`, or `new` inside `run()`, `rtCycle()`, `readAll()`, `writeAll()`, `tick()`, or any method called from them | Any allocation reachable from the RT hot path | High |
| **`noexcept` on all hot-path methods** | Every method in the call graph from `run()` through `tick()` / `evaluate()` is marked `noexcept` | Any non-`noexcept` method in the chain | High |
| **Zero virtual calls per entry in the sweep** | `readAll()`/`writeAll()` perform exactly 2 virtual calls per backend (the two cycle hooks); `tick()` performs zero virtual calls | Any `virtual` or `std::function` call inside a per-entry loop | Very High |
| **Bounded O(1) or O(log n) access** | Registry lookups use `std::lower_bound` on sorted `std::vector` (O(log n)); wrapper pool access uses array index (O(1)); `isInPosition` is O(1) | Any `std::unordered_map` lookup, any linear scan, any unbounded loop in the hot path | High |
| **No system calls in hot path** | Exactly one `clock_gettime` per cycle (via `signalProcessTickNow()`); no file I/O, socket, or OS call in `rtCycle()` | Any additional syscall per cycle | High |
| **Single RT thread owner of all RT data** | `ProductBuffer`, `FunctionState[]`, `WrapperPool`, `HardwareRegistry` cached values are only accessed on the RT thread; cross-thread writes use `VectorBuffer` only | Any direct shared-memory access or lock between RT and non-RT threads | Very High |
| **Contiguous iteration in sweep** | Entries iterated as `std::vector<PDOEntry>` or `std::array<FunctionState>`; no pointer-chasing through a map inside a loop | Heap-scattered objects iterated by pointer list | Medium |
| **Stack pre-fault + SCHED_FIFO** | `Threadrunner` calls `mlockall` + stack touch loop + `SCHED_FIFO` before entering `run()` | Missing any of these three | High |

**Scoring guide:**
- All criteria pass → 9.5–10.0
- One High criterion fails → subtract 0.4–0.6
- One Very High criterion fails → subtract 0.8–1.2
- Two or more failures → score drops below 8.5; document urgently

---

### Dimension 2: SOLID Principles (weighted 45%)

Score each principle 0–10. Final SOLID score = average.

#### S — Single Responsibility

Every class should have exactly one reason to change. Check each class modified in the commit:
- Does it mix transport logic with signal processing? → violation
- Does it mix application logic (what fires when) with hardware access? → violation
- Does it mix init-time construction with RT-time operation? → violation

Excellent (9–10): Every class in the diff has one clearly stated responsibility.
Acceptable (7–8): One class has a minor dual purpose but it is bounded and commented.
Concern (< 7): A class owns both policy and mechanism at the same RT layer.

#### O — Open/Closed

Adding a new function type, hardware channel, or backend should require **additive changes only** (new enum value, new JSON entry, new class). Check: does the commit require modifying an existing class's interface to support the new feature?

Excellent: New feature = new file or new enum case only.
Concern: New feature requires editing existing class signatures.

#### L — Liskov Substitution

All `IHardwareAdapter` subclasses must be substitutable. All wrapper types must fulfil their contract. Check: does any subclass throw where the base promises `noexcept`? Does any subclass weaken a postcondition?

#### I — Interface Segregation

No class should be forced to depend on methods it does not use. Check: does `FunctionState` / `FunctionEvaluator` / `WrapperPool` force any compile-time dependency on backend-specific types? Does any wrapper expose methods irrelevant to its type?

#### D — Dependency Inversion

High-level policy (functions, product tracking) must depend on abstractions (wrappers, registry interface), not on concrete adapters. Check the `#include` graph: does `FunctionEvaluator.h` include `EthercatAdapter.h` or `PDO.h` directly? If yes → violation.

---

## Structure of CurrentAnalysis.md

Maintain these sections in order. Do not add new top-level sections without updating this directive.

```
1. Header block (project, date, branch, last commit, evaluator)
2. Overall Score (hardware · product · application composites)
3. Layer-by-Layer Analysis
   3a. Layer 1–3 Hardware (PDO / Registry / Adapters)
   3b. Layer 4 Wrappers
   3c. Product Layer
   3d. Application Layer / RT Cycle Shell
4. Projected Full-Design RT Profile (call graph, virtual call count, allocs/cycle)
5. SOLID Summary table
6. Open Items table (from architecture doc)
7. Score Summary table
8. Verdict paragraph
```

---

## How to Read the Codebase Before Scoring

Run these searches before writing the analysis. Each search answers a scoring question.

```
# 1. Find any allocation in the hot path
grep -rn "new \|make_unique\|make_shared\|push_back\|resize\|emplace_back" \
     src/application/Application.cpp \
     src/application/FunctionEvaluator.cpp \
     src/hardware/HardwareRegistry.cpp \
     src/hardware/PDO.cpp

# 2. Find any non-noexcept method in the RT chain
grep -n "void\|bool\|int" src/application/Application.h | grep -v noexcept

# 3. Count virtual calls reachable from rtCycle
grep -rn "virtual\|override" src/application/ src/hardware/HardwareRegistry.h

# 4. Find any unordered_map in RT-touched files
grep -rn "unordered_map\|unordered_set" src/

# 5. Verify freeze pattern is intact
grep -rn "freeze\|shrink_to_fit" src/hardware/ src/application/product/

# 6. Check noexcept on tick/evaluate chains
grep -n "evaluate\|tick\|rtCycle\|readAll\|writeAll" src/application/Application.cpp
```

If the codebase has no compile_commands.json entry for a new file, it is not yet built — note it as "planned, not yet committed" in the analysis rather than scoring it.

---

## Scoring Anchors (reference baselines)

These scores represent the reference "excellent" baseline established by SeparateAnalysis.txt. Never score **above** a baseline without explicit justification.

| Layer | RT baseline | SOLID baseline | Justification for that score |
|---|---|---|---|
| Hardware (PDO/Registry/Adapters) | 9.7 | 9.3 | Freeze pattern, zero-virtual entry sweep, contiguous image, deterministic SortedMap lookups |
| Application Wrappers | 9.5 | 9.4 | All-`noexcept` accessors, single `lower_bound` per call, no virtual dispatch, ISP-clean per type |
| Product Layer | 8.6 | 8.8 | Clean POD design, O(1) all ops; missing log slab and `isRejected` (planned, not yet implemented) |
| Application / RT Shell | 9.2 | 9.0 | Correct SCHED_FIFO/affinity/prefault; FunctionEvaluator not yet wired (planned) |

When `FunctionEvaluator` + `WrapperPool` + `ProductStateLog` are implemented, the product and application layer scores should reach 9.5+ if the implementation matches the spec in `Project-Architecture-Overview.md`.

---

## Red-Line Rules

These conditions must **always** be flagged as critical findings, regardless of score impact. If any red line is violated the verdict must read "**DO NOT SHIP — RT regression**" and implementation must stop until resolved.

1. Any heap allocation reachable from `run()` after init completes.
2. Any `std::unordered_map` in any RT-cycle method.
3. Any `std::mutex`, `std::lock_guard`, or `std::condition_variable` inside the RT thread.
4. Any `virtual` call inside a per-entry loop (inside the PDOEntry sweep or FunctionState sweep).
5. Any blocking syscall (file, socket, sleep) inside `rtCycle()`.
6. Any direct shared-memory write to RT-owned data from a non-RT thread without `VectorBuffer`.
7. `SCHED_FIFO` priority dropped or CPU affinity removed from `Threadrunner`.

---

## Commit the Updated Analysis

After updating `CurrentAnalysis.md`, commit it with the message:

```
docs(analysis): update CurrentAnalysis for [brief change description]

- [bullet: what changed architecturally]
- [bullet: which score changed and why]
- [bullet: any new open items added or closed]
```
