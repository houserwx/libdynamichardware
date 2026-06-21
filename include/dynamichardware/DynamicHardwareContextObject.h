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

    // ---- Typed candidate queries (obfuscated — returns UUIDs only) ----
    // These let consumers discover available channels without exposing raw keys.

    /// Lightweight candidate: just UUID + human display name.
    struct ChannelCandidate {
        std::string uuid;          ///< Stable identifier for lookupByUuid()
        std::string displayName;   ///< Human-readable: "GPIO17", "EL3632 ch0",
                                   ///< or user-assigned logicalName if set
        std::string channelType;   ///< "DigitalInput", "Int16Output", etc.
        bool        isOutput{false};
    };

    /// Get candidates matching a specific EntryType bitmask filter.
    /// Pass | to combine types, e.g., dhdo::BoolInput | dhdo::BoolOutput.
    [[nodiscard]] std::vector<ChannelCandidate> getCandidates(uint8_t typeMask) const noexcept;

    /// Convenience: all BoolInput candidates.
    [[nodiscard]] std::vector<ChannelCandidate> getBoolInputCandidates() const noexcept;

    /// Convenience: all BoolOutput candidates.
    [[nodiscard]] std::vector<ChannelCandidate> getBoolOutputCandidates() const noexcept;

    /// Convenience: all FloatInput candidates.
    [[nodiscard]] std::vector<ChannelCandidate> getFloatInputCandidates() const noexcept;

    /// Convenience: all FloatOutput candidates.
    [[nodiscard]] std::vector<ChannelCandidate> getFloatOutputCandidates() const noexcept;

 

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
        std::unordered_map<std::string, std::string> nameToUuid;     ///< displayName → uuid
    };

    explicit DynamicHardwareContextObject(Impl&& impl);
    ~DynamicHardwareContextObject();

    DynamicHardwareContextObject(const DynamicHardwareContextObject&) = delete;
    DynamicHardwareContextObject& operator=(const DynamicHardwareContextObject&) = delete;

    Impl impl_;
    State state_{State::ACTIVE};
};

} // namespace dynamichardware
