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

## 5. Terminology Note: PDO Collision Between Transports ⚠️

The term **PDO** means different things at different layers:

| Layer | Meaning | Contiguous? | Selection model |
|---|---|---|---|
| **EtherCAT transport** | Process Data Object — hardware-mapped DMA buffer on the bus, defined by slave EEPROM | **Yes** — one giant flat memory block across all slaves | Register ALL entries; you can't cherry-pick without breaking the layout |
| **This library** (`pdo::PDO`) | Abstract container holding `entries[]` + `image[]`, used by every backend | Depends on backend | Backend decides how to populate |

**Why EtherCAT is different from other backends:**
- EtherCAT's process data IS a contiguous DMA region managed by IgH domain objects. Every slave's subindexes sit at fixed byte/bit offsets determined by the hardware. Reading it all in one shot has zero performance penalty — there's no per-entry syscall overhead.
- GPIO, I2C, SPI, and simulated backends have NO such constraint. Each channel is an independent file read, register access, or synthetic value generation. They select only the channels the application explicitly registered.

When reading code, "building PDOs" always refers to our library-level abstraction. The underlying transport may or may not use actual PDO mappings (EtherCAT does, everything else doesn't).

---

## 6. Current Pain Points

- **GPIORTBackend** currently activates **all** discovered GPIO lines instead of only those with registered `PDOEntry`s.
- The discovery-first approach makes it easy for the system (and LLMs) to assume "everything in catalog = everything active".
- Lack of strong enforcement that activation must be driven by registered PDOs.

**Recommended Improvement**: In `GPIORTBackend::buildRT()` (or equivalent), query the registry and only request/activate GPIO lines that have associated `PDOEntry`s.

---

**Document updated with terminology clarification.**  
The flow now accurately starts with **all backends scanning and populating the catalog**.

Let me know if you want further adjustments, more detail on any class, or a diagram added.