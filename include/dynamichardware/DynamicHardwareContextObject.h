#pragma once

// ============================================================================
// DynamicHardwareContextObject — pure RT lifecycle, zero discovery knowledge.
//
// Created by DynamicHardwareContextFactory::buildRT(). Owned by the application.
// Used exclusively during operation (freeze → readAll/writeAll cycles → shutdown).
// ============================================================================

#include "dynamichardware/dhdo/DHDO.h"
#include "dynamichardware/dhdo/HardwareRegistry.h"
#include "dynamichardware/dhdo/HardwareCatalog.h"

#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dynamichardware {

class DynamicHardwareContextObject {
public:
    // ---- Lifecycle (state machine) ----

    enum class State { ACTIVE, FROZEN, SHUTDOWN };

    [[nodiscard]] State state() const noexcept { return state_; }

    /// Lock PDOs for real-time operation. Must be called before first read/write cycle.
    /// Transitions from ACTIVE → FROZEN. After freeze, no new entries can be added.
    bool freeze();

    /// Shut down backends and release all resources. Transitions to SHUTDOWN.
    void shutdown();

    // ---- RT cycle (call from RT thread after freeze) ----

    /// Read all inputs from all active backends.
    void readAll() noexcept;

    /// Write all outputs to all active backends.
    void writeAll() noexcept;

    // ---- Channel access (init-time: lookup, RT-time: use cached pointers) ----

    /// Resolve a DHDOEntry by UUID — safe to call at init-time or RT-time if result is cached.
    [[nodiscard]] dhdo::DHDOEntry* lookupByUuid(std::string_view uuid) noexcept;

    /// Resolve a DHDOEntry by name — slower string search, use at init-time only.
    [[nodiscard]] dhdo::DHDOEntry* lookupByName(std::string_view name) noexcept;

    /// List all discovered channels (debug / config inspection).
    [[nodiscard]] const std::vector<dhdo::CatalogEntry>& catalogEntries() const noexcept;

    // ---- Health monitoring ----

    /// Returns number of registered backends.
    [[nodiscard]] std::size_t backendCount() const noexcept;

    /// Returns true if all backends report healthy communication.
    [[nodiscard]] bool allBackendsHealthy() const noexcept;

    /// Returns total DHDOEntry count across all backends.
    [[nodiscard]] std::size_t entryCount() const noexcept;

    // ---- Debug ----

    /// Print full internal state (backends, entries) to stdout.
    void printState() const;

private:
    friend class DynamicHardwareContextFactory;
    template<class T> friend struct std::default_delete;

    struct Impl {
        dhdo::HardwareRegistry registry;
        dhdo::HardwareCatalog  catalog;
        std::unordered_map<std::string, std::string> nameToUuid;
    };

    explicit DynamicHardwareContextObject(Impl&& impl);
    ~DynamicHardwareContextObject();

    DynamicHardwareContextObject(const DynamicHardwareContextObject&) = delete;
    DynamicHardwareContextObject& operator=(const DynamicHardwareContextObject&) = delete;

    Impl impl_;
    State state_{State::ACTIVE};
};

} // namespace dynamichardware
