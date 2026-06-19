# libdynamichardware — PDO System Architecture
**Process Data Object (PDO) Flow & Component Responsibilities**

**Version:** 1.1 (Updated June 19, 2026)  
**Status:** Current implementation analysis

---

## 1. Design Goals

- Deterministic real-time control
- Pluggable backends (GPIO, EtherCAT, Simulated, etc.)
- Hardware discovery first, then selective consumer configuration
- Minimal runtime overhead after `FreezeForRT()`

---

## 2. Actual Initialization Flow (Current Implementation)

1. **Backend Discovery Phase** (First thing that happens)
   - All registered backends (`GPIOAdapter`, `EthercatAdapter`, etc.) scan for available hardware.
   - They populate the **HardwareCatalog** with *all possible* channels/pins (GPIO lines, EtherCAT slaves, etc.).
   - This is **unconditional** — everything physically present is discovered and catalogued.

2. **Consumer Configuration Phase**
   - Consumer reviews the catalog (available channels by UUID, name, type, etc.).
   - Uses **`PDOFactory`** (mainly `fromCatalogEntry()` or `create()`) to turn selected catalog entries into `PDOEntry` objects.
   - This defines the active process data image / memory block.

3. **Registration Phase**
   - `PDOEntry`s are registered into **`HardwareRegistry`**.
   - Registry builds UUID lookup map.

4. **Freeze / Activation Phase**
   - `HardwareRegistry::freezeForRt()` (or equivalent) is called.
   - Adapters (e.g. `GPIOAdapter`) activate **only** the lines/channels that have associated `PDOEntry`s.
   - Hardware is requested (e.g. `gpiod` line requests), process image is finalized, system becomes immutable.

5. **RT Cycle**
   - `HardwareRegistry::readAll()` / `writeAll()`
   - Calls `onBeforeReadInputs()` / `onAfterWriteOutputs()` on each backend
   - Backends populate only their registered `PDO`s

---

## 3. Component Responsibilities (Current Design)

| Component              | Responsibility                                              | Key Behavior / Notes |
|------------------------|-------------------------------------------------------------|----------------------|
| **Backends** (`GPIOAdapter` etc.) | Scan **all** available hardware and populate catalog | Discovery-first (unconditional) |
| **HardwareCatalog**    | Store discovered available channels/pins                    | Populated during init by backends |
| **PDOFactory**         | Create `PDOEntry` from catalog entries or explicit params  | Pure factory — does **not** own list |
| **Consumer**           | Select which catalog entries to turn into active PDOs      | Uses factory with UUIDs from catalog |
| **HardwareRegistry**   | Central owner of active backends + registered `PDOEntry`s  | `freezeForRt()`, `readAll()`, `writeAll()`, UUID map |
| **IHardwareAdapter**   | Backend lifecycle and I/O                                   | `initialize()`, `onBeforeReadInputs()`, `onAfterWriteOutputs()` |
| **PDO / PDOEntry**     | Process image buffer + typed channel access                 | Memory layout for RT hot path |

---

## 4. Areas of Responsibility Summary

- **Discovery** → Backends + `HardwareCatalog` (broad, unconditional)
- **Selection** → Consumer + `PDOFactory` (consumer decides what to activate)
- **Ownership of Active Set** → `HardwareRegistry`
- **Hardware Activation** → Individual Adapters (should be selective in `FreezeForRT()`)
- **RT Data Movement** → `HardwareRegistry` + Adapters

---

## 5. Current Pain Points

- **GPIOAdapter** currently activates **all** discovered GPIO lines instead of only those with registered `PDOEntry`s.
- The discovery-first approach makes it easy for the system (and LLMs) to assume "everything in catalog = everything active".
- Lack of strong enforcement that activation must be driven by registered PDOs.

**Recommended Improvement**: In `GPIOAdapter::FreezeForRT()` (or equivalent), query the `HardwareRegistry` and only request/activate GPIO lines that have associated `PDOEntry`s.

---

**Document updated with your correction.**  
The flow now accurately starts with **all backends scanning and populating the catalog**.

Let me know if you want further adjustments, more detail on any class, or a diagram added.