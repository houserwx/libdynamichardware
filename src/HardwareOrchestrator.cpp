// ============================================================================
// HardwareOrchestrator — internal phase coordination + backend iteration.
// 
// Internal phase coordination extracted from monolithic factory to fix SRP violation.
// Uses new interfaces exclusively: scan() for discovery, build(channels) for RT.
// ============================================================================

#include "dynamichardware/HardwareOrchestrator.h"
#include "dynamichardware/dhdo/IBackendScanner.h"

#include "dynamichardware/DynamicHardwareContextObject.h"
#include "dynamichardware/dhdo/HardwareRegistry.h"
#include "dynamichardware/dhdo/DHDOFactory.h"
#include "dynamichardware/config/BackendRegistry.h"

// Backend-specific includes removed from orchestrator source.
// All backend instantiation now flows through BackendRegistry self-registration.
// Backends include their own headers in their .cpp files where REGISTER_BACKEND lives.

// For mapping persistence — need nlohmann::json for save/load mappings
#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <set>
#include <string>

namespace dynamichardware {

HardwareOrchestrator::HardwareOrchestrator(OrchestratorState state)
    : state_(std::move(state)) {}

void HardwareOrchestrator::addChannelDefinition(const std::string& keyOrUuid, 
                                                 dhdo::EntryType type,
                                                 const std::string& friendlyName) {
    ChannelDefinition def{};
    def.keyOrUuid    = keyOrUuid;
    def.type         = type;
    def.friendlyName = friendlyName;
    channelDefs_.push_back(std::move(def));
}

const dhdo::HardwareCatalog& HardwareOrchestrator::catalog() const noexcept { return catalog_; }
dhdo::HardwareCatalog& HardwareOrchestrator::catalog()       noexcept { return catalog_; }

// ============================================================================
// Mapping persistence helpers (extracted from old factory)
// Format: { "mappings": [ { "uuid": ..., "type": "BoolOutput", "name": ... }, ... ] }
// ============================================================================

size_t HardwareOrchestrator::loadMappings() {
    if (state_.mappingPath.empty()) return 0;

    std::ifstream f(state_.mappingPath);
    if (!f) return 0;

    try {
        nlohmann::json j;
        f >> j;

        size_t count = 0;
        for (const auto& m : j.value("mappings", nlohmann::json::array())) {
            ChannelDefinition def{};
            def.keyOrUuid    = m.value("uuid", "");
            def.friendlyName = m.value("name", "");

            std::string typeStr = m.value("type", "BoolInput");
            def.type = dhdo::DHDOFactory::stringToEntryType(typeStr);

            channelDefs_.push_back(std::move(def));
            ++count;
        }
        std::printf("[Mapping] Loaded %zu mappings from '%s'\n",
                    count, state_.mappingPath.c_str());
        return count;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[Mapping] Parse error loading '%s': %s\n",
                     state_.mappingPath.c_str(), ex.what());
        return 0;
    }
}

void HardwareOrchestrator::saveMappings() {
    if (state_.mappingPath.empty()) return;

    try {
        nlohmann::json j;
        nlohmann::json arr = nlohmann::json::array();

        for (const auto& cdef : channelDefs_) {
            nlohmann::json entry;
            entry["uuid"] = cdef.keyOrUuid;
            entry["type"] = dhdo::DHDOFactory::entryTypeToString(cdef.type);
            if (!cdef.friendlyName.empty()) {
                entry["name"] = cdef.friendlyName;
            }
            arr.push_back(std::move(entry));
        }

        j["mappings"] = arr;

        std::ofstream f(state_.mappingPath);
        if (!f) {
            std::fprintf(stderr, "[Mapping] Cannot open '%s' for writing\n",
                         state_.mappingPath.c_str());
            return;
        }
        f << j.dump(2) << '\n';
        std::printf("[Mapping] Saved %zu mappings to '%s'\n",
                    channelDefs_.size(), state_.mappingPath.c_str());
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[Mapping] Save error: %s\n", ex.what());
    }
}

// ============================================================================
// Discovery phase — scan() pipeline feeds into catalog, then purge stale entries
// ============================================================================

bool HardwareOrchestrator::runDiscoveryScan() {
    bool anyDiscovered = false;

    for (const auto& [name, cfg] : state_.enabledBackends) {
        // Registry-driven dispatch: look up creator function by backend name.
        // Zero hardcoded branches — new backends self-register via REGISTER_BACKEND macro.
        const auto* creator = config::BackendRegistry::getCreator(name);
        if (!creator) {
            std::fprintf(stderr,
                "[Discover] WARNING: Backend '%s' requested but not in registry — skipping\n",
                name.c_str());
            continue;  // Non-fatal: allow other backends to proceed with partial configuration
        }

        // Creator returns scanner+adapter pair — scanner used here, adapter stored for buildRT()
        auto [scanner, adapter] = (*creator)();

        // Post-creation configure hook injects per-backend parameters from enabledBackends map
        scanner->configure(cfg);
        adapter->configure(cfg);

        // Pure-data scan flow:
        // scan() returns descriptors without mutating shared catalog state.
        // orchestrator converts descriptors to CatalogEntry objects inline.
        std::printf("[Discover] Scanning %s devices...\n", name.c_str());
        auto descriptors = scanner->scan();

        // Mark this backend as participating in the current discovery cycle.
        // Only participating backends can purge their own territory — others are preserved.
        if (!descriptors.empty()) {
            dhdo::BackendType participatedType = descriptors[0].backend;
            catalog_.markBackendParticipated(participatedType);

            for (auto& desc : descriptors) {
                dhdo::CatalogEntry entry{};
                entry.uuid          = desc.uuid;
                entry.channelType   = desc.channelType;
                entry.name          = desc.name;
                entry.slaveName     = "Unknown";  // Will be overridden by backend-specific metadata
                entry.isOutput      = desc.isOutput;
                entry.backend       = desc.backend;
                entry.backendData   = std::move(desc.backendData);
                catalog_.addEntry(std::move(entry));
            }

            anyDiscovered = true;
        } else {
            std::printf("[Discover] %s discovery returned no entries (stub mode or unavailable)\n", name.c_str());
        }

        // Store adapter between discovery and build phases
        pendingAdapters_[name] = std::move(adapter);
    }

    return anyDiscovered;
}

bool HardwareOrchestrator::discover() {
    // Phase starts at DISCOVERY by default; no advance needed on first call.
    // If called again after reset, ensure we're in DISCOVERY:
    if (!phaseManager_.isAt(config::HardwarePhase::DISCOVERY)) {
        auto current = phaseManager_.get();
        if (current > config::HardwarePhase::DISCOVERY) {
            std::fprintf(stderr,
                "[Discover] Cannot discover past BUILD_RT phase — use new builder instance for re-discovery\n");
            return false;
        }
        try { (void)phaseManager_.advance(config::HardwarePhase::DISCOVERY); } catch (...) {}
    }

    // Load existing catalog (preserves UUIDs across restarts)
    catalog_.load(state_.catalogPath);

    // Begin discovery cycle — any entry NOT re-registered by the end gets purged.
    catalog_.beginDiscovery();

    bool anyDiscovered = runDiscoveryScan();

    // Report backends that had existing catalog entries but didn't participate this cycle.
    // These territories are preserved (not purged) — just flagged as offline so mapping/build can skip them.
    catalog_.reportOfflineBackends(state_.enabledBackends);

    // Purge ONLY stale entries from participating backends (devices removed/changed).
    // Non-participating backend territory is left untouched.
    auto purged = catalog_.purgeStaleEntries();
    if (purged > 0) {
        std::printf("[Discover] Removed %zu stale entries from catalog\n", purged);
    }

    // Save updated catalog after discovery phase.
    if (!state_.catalogPath.empty()) {
        catalog_.save(state_.catalogPath);
    }

    size_t totalEntries = catalog_.entries().size();
    std::printf("[Discover] Complete: %zu catalog entries (%s)\n",
                totalEntries, anyDiscovered ? "new channels found" : "no new channels");

    // Warn about previously-mapped UUIDs that no longer exist in current hardware scan.
    if (!channelDefs_.empty()) {
        size_t staleCount = 0;
        for (const auto& cdef : channelDefs_) {
            const auto* catEntry = catalog_.findByUuid(cdef.keyOrUuid);

            if (!catEntry) {
                ++staleCount;
                std::fprintf(stderr,
                    "[Mapping] WARNING: Previously-mapped entry '%s' "
                    "(type=%s) no longer exists in discovered hardware!\n",
                    cdef.keyOrUuid.c_str(),
                    dhdo::DHDOFactory::entryTypeToString(cdef.type).c_str());
            }
        }
        if (staleCount > 0) {
            std::fprintf(stderr,
                "[Mapping] %zu of %zu mapped entries are STALE — remap required.\n",
                staleCount, channelDefs_.size());
        } else {
            std::printf("[Mapping] All %zu mapped entries still valid.\n", channelDefs_.size());
        }
    }

    // Persist current channel definitions (user's mapped channels).
    saveMappings();

    // Advance to MAPPING phase after discovery completes (if not already there)
    if (!phaseManager_.isAt(config::HardwarePhase::MAPPING)) {
        try { (void)phaseManager_.advance(config::HardwarePhase::MAPPING); }
        catch (const std::exception& e) {
            std::fprintf(stderr, "[Discover] Phase advance failed: %s\n", e.what());
        }
    }

    return anyDiscovered || totalEntries > 0;
}

// ============================================================================
// Build RT context — create backends using build(channels), move into ContextObject
// ============================================================================

std::unique_ptr<DynamicHardwareContextObject> HardwareOrchestrator::buildRT() {
    // Ensure we've advanced through MAPPING before BUILD_RT — fail loudly on illegal transitions
    if (!phaseManager_.isAt(config::HardwarePhase::MAPPING)) {
        auto current = phaseManager_.get();
        if (current > config::HardwarePhase::MAPPING) {
            std::fprintf(stderr,
                "[RtBuild] Cannot build past RUNNING phase — use new builder instance\n");
            return nullptr;
        }
        try { (void)phaseManager_.advance(config::HardwarePhase::MAPPING); }
        catch (const std::exception& e) {
            std::fprintf(stderr, "[RtBuild] Phase advance to MAPPING failed: %s\n", e.what());
            return nullptr;
        }
    }
    if (!phaseManager_.isAt(config::HardwarePhase::BUILD_RT)) {
        try { (void)phaseManager_.advance(config::HardwarePhase::BUILD_RT); }
        catch (const std::exception& e) {
            std::fprintf(stderr, "[RtBuild] Phase advance to BUILD_RT failed: %s\n", e.what());
            return nullptr;
        }
    }

    // Build RT backends from adapters created during discovery phase.
    // Registry-driven dispatch: iterate over pendingAdapters_ populated by runDiscoveryScan().
    dhdo::HardwareRegistry registry;

    for (auto& [name, adapter] : pendingAdapters_) {
        // Adapter was already configured during discovery via configure(configMap).
        // Now inject catalog reference and pass all channel defs as opaque UUID tuples.
        // Each adapter self-selects the entries it owns during build() — orchestrator stays abstract.
        std::printf("[RtBuild] Building %s backend...\n", name.c_str());

        // Give adapter access to the catalog so it can resolve its own UUIDs internally.
        // This is init-time only; after build() returns we're in runtime territory.
        adapter->setCatalog(&catalog_);

        // Convert consumer channel definitions into MappedChannel structs.
        // Pass ALL defs — each adapter filters by its own BackendType inside build().
        std::vector<dhdo::MappedChannel> allChannels;
        for (const auto& cdef : channelDefs_) {
            const auto* catEntry = catalog_.findByUuid(cdef.keyOrUuid);
            if (!catEntry) continue;  // Stale mapping — skip silently

            dhdo::MappedChannel mc{};
            mc.uuid   = cdef.keyOrUuid;
            mc.type   = cdef.type;
            mc.name   = cdef.friendlyName.empty()
                           ? catEntry->name  // Fall back to catalog displayName
                           : cdef.friendlyName;
            allChannels.push_back(std::move(mc));
        }

        bool ok = false;
        try {
            // Each backend's RT adapter implements build(channels).
            // The adapter self-selects entries matching its transport type from the list.
            if (adapter->build(allChannels)) {
                ok = true;
            } else {
                std::printf("[RtBuild] %s RT setup failed — skipping\n", name.c_str());
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[RtBuild] %s build threw: %s — skipping\n",
                         name.c_str(), e.what());
        }

        if (ok) {
            registry.addBackend(std::move(adapter));
        }
    }

    // Build name → UUID mapping from catalog using deterministic hash-based UUIDs
    DynamicHardwareContextObject::InternalState ctxState{
        std::move(registry), std::move(catalog_), {}
    };
    for (const auto& entry : ctxState.catalog.entries()) {
        if (!entry.name.empty()) {
            ctxState.nameToUuid[entry.name] = entry.uuid;
        }
    }

    std::printf("[RtBuild] Complete: %zu backends, %zu entries\n",
                ctxState.registry.backendCount(), ctxState.catalog.entries().size());

    return std::unique_ptr<DynamicHardwareContextObject>(
        new DynamicHardwareContextObject(std::move(ctxState)));
}

} // namespace dynamichardware
