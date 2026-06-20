#pragma once
#include "dynamichardware/dhdo/DHDO.h"
#include <cstddef>
#include <memory>
#include <vector>

// ============================================================
// IRTBackend — runtime lifecycle interface for transport backends.
//
// RT DESIGN PATTERN: Virtual Dispatch at Backend Level (The Exception)
// Only 2 virtual calls PER BACKEND PER CYCLE.
// Per-entry read/write is concrete (DHDOEntry::read/write).
//
// This interface owns the frozen PDO vector and provides the two RT hooks.
// Discovery is a separate concern handled by IDiscoveryBackend.
// Concrete backends inherit both interfaces via multiple inheritance:
//   class GPIOAdapter final : public IDiscoveryBackend, public IRTBackend { ... }
//
// Lifecycle contract:
//   - buildRT(): construct PDOs from consumer-selected catalog entries + allocate image buffers.
//     Called after discovery phase but before freeze(). Pure setup — no hardware activation yet.
//   - activate(): optional override called between buildRT() and freeze(). Acquires real handles,
//     opens master, etc. Default is no-op for backends that activate inline during buildRT().
//   - onBeforeReadInputs()/onAfterWriteOutputs(): RT cycle hooks (pure virtual)
//
// SOLID notes:
//   - Interface Segregation: this interface only contains methods relevant to the frozen RT loop.
//     Discovery (catalog population) lives in IDiscoveryBackend — transient init-time concern.
//     A backend's discovery object can be destroyed before freeze without affecting RT operation.
//   - dhdos_ ownership: protected for subclass mutation during buildRT()/activate().
//     Consumers never manipulate PDOs directly — they interact through DynamicHardwareContext/PDOFactory.
//     const accessor prevents accidental mutation from external code.
// ============================================================

namespace dynamichardware::dhdo {

class IRTBackend {
public:
    virtual ~IRTBackend() = default;

    IRTBackend(const IRTBackend&)            = delete;
    IRTBackend& operator=(const IRTBackend&) = delete;
    IRTBackend(IRTBackend&&)                 = delete;
    IRTBackend& operator=(IRTBackend&&)      = delete;

    // --- Pre-freeze setup ---------------------------------------------------

    /// Build PDO structure from consumer-selected entries and allocate image buffers.
    /// Called by context after discovery + consumer configuration, before freeze().
    /// This is where adapters translate catalog selections into concrete process-image layout.
    virtual bool buildRT() = 0;

    /// Activate hardware handles / resources for registered lines only.
    /// Optional override — called between buildRT() and freeze().
    /// Default implementation is a no-op (backends like EtherCAT activate inline during buildRT).
    virtual void activate() {}

    // --- RT cycle hooks (called per-backend per-cycle) ----------------------

    /// Pre-read hook: backend fills process image buffer with fresh data.
    virtual void onBeforeReadInputs()  noexcept = 0;

    /// Post-write hook: backend flushes process image to physical hardware.
    virtual void onAfterWriteOutputs() noexcept = 0;

protected:
    IRTBackend() = default;

    // Accessible to derived backends for PDO construction during buildRT()/activate().
    // External consumers never manipulate PDOs directly — they go through DynamicHardwareContext/PDOFactory.
    // Registry accesses via friendship for mutable RT sweep iteration and freeze verification.
    friend class HardwareRegistry;
    std::vector<DHDO> dhdos_;

public:
    // --- Accessors ----------------------------------------------------------

    /// Const accessor for reading PDO structure (debug, verification).
    /// External consumers never manipulate PDOs directly — they go through DynamicHardwareContext/PDOFactory.
    [[nodiscard]] const std::vector<DHDO>& getDHDOS() const noexcept { return dhdos_; }
};

} // namespace dynamichardware::dhdo
