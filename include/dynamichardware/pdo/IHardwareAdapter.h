#pragma once
#include "dynamichardware/pdo/PDO.h"
#include <cstddef>
#include <vector>

// ============================================================
// IHardwareAdapter — abstract transport backend.
//
// RT DESIGN PATTERN: Virtual Dispatch at Backend Level (The Exception)
// Only 2 virtual calls PER BACKEND PER CYCLE.
// Per-entry read/write is concrete (PDOEntry::read/write).
//
// Lifecycle contract:
//   - initialize(): discovery + PDO construction (called by context during build())
//   - activate(): hardware activation / handle acquisition (optional; called before freeze)
//   - onBeforeReadInputs()/onAfterWriteOutputs(): RT cycle hooks
//
// SOLID notes:
//   - Catalog registration: all adapters accept HardwareCatalog* via setCatalog().
//     This coupling is intentional — the catalog IS the discovery output, and backends are the only
//     producers. No virtual registrar needed since no adapter functions without catalog access.
//   - pdos_ ownership: protected for subclass mutation during init/activate phases.
//     Consumers never manipulate PDOs directly — they interact through DynamicHardwareContext/PDOFactory.
//     const accessor prevents accidental mutation from external code.
// ============================================================

namespace dynamichardware::pdo {

class IHardwareAdapter {
public:
    virtual ~IHardwareAdapter() = default;

    IHardwareAdapter(const IHardwareAdapter&)            = delete;
    IHardwareAdapter& operator=(const IHardwareAdapter&) = delete;
    IHardwareAdapter(IHardwareAdapter&&)                 = delete;
    IHardwareAdapter& operator=(IHardwareAdapter&&)      = delete;

    // --- Lifecycle ----------------------------------------------------------

    /// Discover hardware, populate catalog, construct PDO entries.
    /// Called once during build phase before any RT activity.
    virtual bool initialize() = 0;

    /// Activate hardware handles / resources for registered lines only.
    /// Optional override — called by context between build() and freeze().
    /// Default implementation is a no-op (backends like EtherCAT activate inline).
    virtual void activate() {}

    // --- RT cycle hooks (called per-backend per-cycle) ----------------------

    /// Pre-read hook: backend fills process image buffer with fresh data.
    virtual void onBeforeReadInputs()  noexcept {}

    /// Post-write hook: backend flushes process image to physical hardware.
    virtual void onAfterWriteOutputs() noexcept {}

protected:
    IHardwareAdapter() = default;

    // Accessible to derived adapters for PDO construction during initialize()/activate().
    // External consumers never manipulate PDOs directly — they go through DynamicHardwareContext/PDOFactory.
    // Registry accesses via friendship for mutable RT sweep iteration and freeze verification.
    friend class HardwareRegistry;
    std::vector<PDO> pdos_;

public:
    // --- Accessors ----------------------------------------------------------

    /// Const accessor for reading PDO structure (debug, verification).
    /// External consumers never manipulate PDOs directly — they go through DynamicHardwareContext/PDOFactory.
    [[nodiscard]] const std::vector<PDO>& getPDOs() const noexcept { return pdos_; }
};

} // namespace dynamichardware::pdo
