# Copilot / AI Agent Code Quality Guidelines  
**(Real-time Industrial Control & Embedded-style C++ Projects)**

This document defines the **minimum acceptable code quality bar** for any code you generate, review, suggest, or refactor in this codebase.

These rules are **non-negotiable** when working on real-time, safety-relevant, or performance-critical components (PDO model, HardwareRegistry, EtherCAT adapter, signal processing machines, etc.).

Follow them **strictly** — even when the user asks for "quick" or "prototype" code.

## Core Principles – Always Obey These

1. **Hard real-time safety first**  
   - No heap allocations after initialization (`new`, `malloc`, `std::vector::push_back` after freeze, etc.)  
   - No virtual dispatch in hot paths (readAll / writeAll / per-entry logic)  
   - No `std::function`, lambdas with capture, `std::variant`+`visit`, or any indirect call in cycle-critical code  
   - No exceptions in RT paths (`noexcept` everywhere possible)  
   - No locks, atomics, or condition variables in the real-time loop unless explicitly justified and measured

2. **Freeze-the-world pattern**  
   - All dynamic sizing (`vector`, `string`, `unordered_map`) must be completed **before** entering real-time mode  
   - Use `.shrink_to_fit()`, re-base pointers, and make structures immutable after `freezeForRt()` / `freeze()`  
   - Never call mutating methods on collections after freeze

3. **Zero-cost abstractions where performance matters**  
   - Prefer concrete structs + `switch` on closed enum sets over inheritance / virtuals / variants in hot paths  
   - Use value semantics, embedded state machines (`PulseMachine`, `DebounceMachine` style)  
   - Tiny state machines must be allocation-free, branch-predictable, and inline-friendly

4. **Deterministic and cache-friendly data layout**  
   - Contiguous storage (`std::vector`) over maps/hashes in hot loops when possible  
   - Sorted vectors + `std::lower_bound` acceptable for lookup (O(log N) with N < 1000 is fine)  
   - Avoid pointer chasing inside inner loops

5. **Type & aliasing safety**  
   - Use `std::memcpy` for type punning from byte buffers (`uint8_t*`) — never `reinterpret_cast` or C-style cast on unaligned/unknown-alignment memory  
   - Respect strict aliasing — never violate it

6. **Single RT-thread invariant**  
   - Assume **one thread only** executes `readAll()`, `writeAll()`, and typed `get/set` accessors  
   - Do **not** add locks or atomics to compensate for multi-threading unless the user explicitly requests a multi-threaded design (and even then, measure impact)

7. **Configuration at init, constants in RT**  
   - All timing, debounce, pulse durations, etc. configured **once** at startup  
   - RT path must never re-read configuration files, environment variables, etc.

8. **Documentation discipline**  
   - Every RT-critical function must be marked `noexcept`  
   - Comment lifecycle invariants (init / freeze / RT)  
   - Document thread-safety assumptions explicitly  
   - Add big comment blocks explaining why something is done the "hard way" (memcpy, switch instead of virtual, etc.)

## .clang-tidy Ruleset – Mandatory Checks

Enable and **obey at least** the following checks (ideally the full modern-cpp + performance + bugprone + readability set):

```yaml
Checks: '*,\
  -cppcoreguidelines-owning-memory,\
  -cppcoreguidelines-pro-bounds-pointer-arithmetic,\
  -fuchsia-default-arguments,\
  -google-build-using-namespace,\
  -modernize-use-trailing-return-type,\
  -readability-identifier-naming,\
  -performance-unnecessary-value-param,\
  bugprone-*,\
  cert-*,\
  clang-analyzer-*,\
  concurrency-*,\
  misc-*,\
  modernize-*,-modernize-use-trailing-return-type,\
  performance-*,\
  portability-*,\
  readability-*'
```

**Especially enforce these:**

- `performance-noexcept-move-constructor`  
- `performance-move-const-arg`  
- `performance-unnecessary-copy-initialization`  
- `bugprone-unchecked-optional-access` (if using `std::optional`)  
- `bugprone-forward-declaration-namespace`  
- `bugprone-unused-return-value`  
- `readability-braces-around-statements`  
- `readability-simplify-boolean-expr`  
- `readability-redundant-member-init`  
- `modernize-use-nullptr`  
- `modernize-deprecated-headers`  
- `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling`

## Non-negotiable Code Smells – Reject Immediately

- Adding `virtual` to any hot-path class/method without extremely strong justification  
- Introducing `std::shared_ptr` / `std::unique_ptr` for per-channel objects  
- Using `std::map` or `std::unordered_map` for per-cycle lookups  
- Putting timing logic (`clock_gettime`) inside per-entry code  
- Using `memcpy` with variable size in hot path (fixed 2/4/8 bytes is okay)  
- Adding runtime string operations (`std::string`, `std::format`) in RT path  
- Ignoring alignment in process-image access (use `memcpy`!)

## Design Decision Policy — Always Propose the Cleanest Path

When there are multiple valid ways to implement something, **always identify the cleanest and architecturally best option** according to the scoring criteria in this document, and propose it explicitly before acting.

**Default behaviour:** implement the cleanest option unless the user has already stated a preference.

**When to ask for confirmation:** if the cleanest approach requires deleting files, changing a public API, or restructuring something that could surprise the user — briefly describe *what* you are going to do and *why it is cleaner*, then proceed unless the user says otherwise.

> Example: "The cleanest approach here is to fold `ExampleFunction` directly into `HardwareDemoRoutine` — it eliminates three wrapper members from `Application`, two source files, and one include chain, with no loss of functionality. I'll do that unless you prefer to keep it separate."

**Never silently choose the expedient / copy-paste path** when a structurally better option is obvious (e.g. merging a demo class into an existing demo class rather than leaving orphaned members in the owner). Prefer consolidation, deletion of dead code, and the fewest moving parts that still satisfy the requirement.

**Scoring criteria (in priority order):**
1. RT safety — always first
2. Fewest moving parts / smallest surface area
3. Elimination of duplication (constants, classes, includes)
4. Readability and self-documenting structure
5. Consistent with existing patterns in the codebase

## When the User Asks for "Quick & Dirty" or "Prototype"

Still follow 80–90% of these rules.  
Explain politely which relaxations you are making and why, then offer the clean version as an alternative:

> "Here is a quick prototype version using [simpler approach].  
> For production usage I strongly recommend switching to the freeze-pattern + concrete-switch style shown in PDOEntry / HardwareRegistry to preserve determinism."

## To-Do List Execution Rules — Mandatory Steps Per Task

When executing a multi-step implementation plan (any numbered or bulleted to-do list), **every single task** must follow this sequence before moving to the next task:

1. **Write the code** — implement the task fully, adhering to all quality rules above.
2. **Update `CMakeLists.txt`** — add any new `.cpp` files to the build immediately; never let new sources accumulate across tasks.
3. **Build** — run `./build.sh` and confirm zero errors. Do not proceed with a broken build.
4. **Static analysis** — run `./build.sh --dev` (or equivalent) to execute cppcheck and clang-tidy.
5. **Fix all issues** — resolve every warning, error, or finding reported by the analysis tools before moving on. No deferring.
6. **Smoke-check** — if the binary is runnable at this point, run it briefly to confirm no obvious crashes or regressions.
7. **Git commit** — stage and commit all changes for this task with a clear, descriptive commit message. One commit per task.

### Non-negotiable rules for this workflow:

- **Never batch commits** across multiple tasks. Each task gets its own commit, even if small.
- **Never proceed to the next task with a failing build** or unfixed analysis findings.
- **Never skip the analysis step** — even for "simple" header-only additions.
- If a clang-tidy or cppcheck finding reveals a design problem (not just style), **stop and fix the design** before continuing. Do not suppress legitimate warnings.
- **Mark the task completed in the to-do list only after the commit is made**.

---

## Summary Motto

**"Write once at init, read/write zero-cost in the cycle — or don't write it at all."**

Follow these guidelines religiously — even when it feels like over-engineering.  
This codebase is aiming for **industrial-grade hard real-time quality**, not research/experimental code.

Last updated: March 2026  
(Jeffrey's real-time control project style guide)
