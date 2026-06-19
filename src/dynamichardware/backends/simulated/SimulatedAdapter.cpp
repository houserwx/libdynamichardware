#include "dynamichardware/backends/simulated/SimulatedAdapter.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>

namespace dynamichardware::simulated {

// ---------------------------------------------------------------------------
// Helper: map channelType string → EntryType enum + isOutput flag
// ---------------------------------------------------------------------------
static void resolveChannelType(const std::string& type, pdo::EntryType& outType, bool& outIsOutput) noexcept
{
    outIsOutput = false;

    if (type == "BoolInput")   outType = pdo::EntryType::BoolInput;
    else if (type == "BoolOutput") { outType = pdo::EntryType::BoolOutput; outIsOutput = true; }
    else if (type == "Int8Input")  outType = pdo::EntryType::Int8Input;
    else if (type == "Int16Input") outType = pdo::EntryType::Int16Input;
    else if (type == "Int32Input") outType = pdo::EntryType::Int32Input;
    else if (type == "Int16Output") { outType = pdo::EntryType::Int16Output; outIsOutput = true; }
    else if (type == "FloatInput")  outType = pdo::EntryType::FloatInput;
    else if (type == "FloatOutput") { outType = pdo::EntryType::FloatOutput; outIsOutput = true; }
}

// ---------------------------------------------------------------------------
// Helper: compute byte size from EntryType
// ---------------------------------------------------------------------------
static std::size_t entryByteSize(pdo::EntryType t) noexcept
{
    switch (t) {
        case pdo::EntryType::BoolInput:
        case pdo::EntryType::BoolOutput:
            return 1;
        case pdo::EntryType::Int8Input:
            return sizeof(int8_t);
        case pdo::EntryType::Int16Input:
        case pdo::EntryType::Int16Output:
            return sizeof(int16_t);
        case pdo::EntryType::Int32Input:
        case pdo::EntryType::FloatInput:
        case pdo::EntryType::FloatOutput:
            return sizeof(uint32_t);
        default:
            return 1;  // Safe fallback
    }
}

// ---------------------------------------------------------------------------
// Load JSON definitions, register catalog entries
// ---------------------------------------------------------------------------
bool SimulatedAdapter::loadDefinitions(const std::string& path)
{
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "[SimulatedAdapter] Cannot open '%s'\n", path.c_str());
        return false;
    }

    using json = nlohmann::json;
    auto j = json::parse(f);

    // Set cycle time from JSON if present
    if (j.contains("cycleTimeUs")) {
        cycleNs_ = static_cast<uint32_t>(j["cycleTimeUs"].get<int>()) * 1000u;
    }
    cycleNsD_ = static_cast<double>(cycleNs_);

    if (!j.contains("channels") || catalog_ == nullptr) {
        return false;
    }

    for (const auto& ch : j["channels"]) {
        dynamichardware::pdo::CatalogEntry entry;
        entry.key         = "SIM|" + ch.value("name", "");
        entry.uuid        = ch.value("uuid", "");
        entry.channelType = ch.value("channelType", "BoolInput");
        entry.name        = ch.value("name", "");
        entry.slaveName   = "Simulated";
        entry.isSimulated = true;

        pdo::EntryType resolvedType{};
        bool isOutput{false};
        resolveChannelType(entry.channelType, resolvedType, isOutput);
        entry.isOutput = isOutput;

        if (ch.contains("sim")) {
            const auto& s = ch["sim"];
            // Generic rate-of-change parameters
            entry.sim.togglePeriodMs     = s.value("togglePeriodMs", 0u);
            entry.sim.dutyCyclePercent   = s.value("dutyCyclePercent", 50.0f);
            entry.sim.incrementPerCycle  = s.value("incrementPerCycle", 1);
            entry.sim.minValue           = s.value("minValue", static_cast<int64_t>(INT64_MIN));
            entry.sim.maxValue           = s.value("maxValue", static_cast<int64_t>(INT64_MAX));
            entry.sim.amplitude          = s.value("amplitude", 1.0f);
            entry.sim.frequencyHz        = s.value("frequencyHz", 1.0f);
            entry.sim.offset             = s.value("offset", 0.0f);
            // I/O configuration
            entry.sim.pulseMs            = s.value("pulseMs", 0u);
            entry.sim.debounceMs         = s.value("debounceMs", 0u);
        }

        catalog_->addEntry(std::move(entry));
    }

    std::printf("[SimulatedAdapter] Loaded %zu simulated channels from '%s'\n",
                j["channels"].size(), path.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// IDiscoveryBackend::discover() — trivial validation that catalog has simulated entries.
// Actual catalog population happens via loadDefinitions() called by Context before this.
// ---------------------------------------------------------------------------
bool SimulatedAdapter::discover()
{
    if (!catalog_) {
        std::fprintf(stderr, "[SimulatedAdapter] No catalog attached\n");
        return false;
    }

    const auto& entries = catalog_->entries();
    bool hasSimEntries = false;
    for (const auto& e : entries) {
        if (e.isSimulated) {
            hasSimEntries = true;
            break;
        }
    }

    if (!hasSimEntries && !entries.empty()) {
        std::printf("[SimulatedAdapter] Warning: no simulated entries in catalog — will produce empty RT PDOs\n");
    }

    std::printf("[SimulatedAdapter] Discovery complete: %zu total catalog entries\n", entries.size());
    return true;  // Always succeed even with zero sim entries (buildRT handles gracefully)
}

// ---------------------------------------------------------------------------
// IRTBackend::buildRT() — build PDO from simulated catalog entries and allocate buffers.
// Called after consumer configuration but before freeze().
// NOTE: Do NOT call pdos_[0].freeze() here. Freeze is orchestrated by HardwareRegistry.
// ---------------------------------------------------------------------------
bool SimulatedAdapter::buildRT()
{
    if (!catalog_ || catalog_->empty()) {
        std::printf("[SimulatedAdapter] No catalog or empty catalog — nothing to simulate\n");
        return true;
    }

    const auto& entries = catalog_->entries();

    // Filter simulated entries
    std::vector<const dynamichardware::pdo::CatalogEntry*> simEntries;
    for (const auto& e : entries) {
        if (e.isSimulated) {
            simEntries.push_back(&e);
        }
    }

    if (simEntries.empty()) {
        std::printf("[SimulatedAdapter] No simulated entries in catalog\n");
        return true;
    }

    // First pass: compute total image size based on resolved EntryType
    std::size_t totalImageBytes = 0;
    for (const auto* e : simEntries) {
        pdo::EntryType type{};
        bool dummyOutput{false};
        resolveChannelType(e->channelType, type, dummyOutput);
        totalImageBytes += entryByteSize(type);
    }

    // Build PDO
    pdos_.resize(1);
    pdos_[0].image.resize(totalImageBytes);
    pdos_[0].entries.reserve(simEntries.size());
    simStates_.reserve(simEntries.size());

    double cyclesPerSec = 1e9 / cycleNsD_;

    std::size_t currentOffset = 0;
    for (const auto* e : simEntries) {
        dynamichardware::pdo::PDOEntry entry;
        entry.uuid       = e->uuid;
        entry.byteOffset = static_cast<uint32_t>(currentOffset);

        SimState sim;
        sim.byteOffset = entry.byteOffset;

        pdo::EntryType type{};
        bool isOutput{false};
        resolveChannelType(e->channelType, type, isOutput);

        entry.type      = type;
        if (!isOutput && entryByteSize(type) > 1) {
            entry.bitLength = static_cast<uint8_t>(entryByteSize(type) * 8);
        } else {
            entry.bitLength = 1;
        }

        sim.type = type;

        // Apply I/O configuration from sim params
        if (e->sim.debounceMs > 0)
            entry.configureDebounceMs(e->sim.debounceMs);
        if (e->sim.pulseMs > 0)
            entry.configurePulseMs(e->sim.pulseMs);

        // Configure simulation state based on EntryType category
        if (type == pdo::EntryType::BoolInput || type == pdo::EntryType::BoolOutput) {
            // Boolean: periodic square-wave toggle
            uint32_t periodCycles = 0;
            if (e->sim.togglePeriodMs > 0) {
                periodCycles = static_cast<uint32_t>(
                    e->sim.togglePeriodMs / 1000.0 * cyclesPerSec);
            }
            sim.togglePeriodCycles = periodCycles;
            sim.highCycles = static_cast<uint32_t>(periodCycles * e->sim.dutyCyclePercent / 100.0f);
            sim.lowCycles  = periodCycles - sim.highCycles;
            sim.isHigh     = false;
            sim.tickCount  = 0;
        } else if (entryIsSigned(type) && !isOutput) {
            // Integer input: linear increment with optional bounds
            sim.intValue           = 0;
            sim.incrementPerCycle  = e->sim.incrementPerCycle;
            sim.minValue           = e->sim.minValue;
            sim.maxValue           = e->sim.maxValue;
        } else if ((type & pdo::BASE_FLOAT) && !isOutput) {
            // Float input: sinusoidal oscillation
            sim.floatPhase       = 0.0;
            double hz = e->sim.frequencyHz > 0 ? e->sim.frequencyHz : 1.0;
            sim.phaseIncrement   = 2.0 * M_PI * hz / cyclesPerSec;
            sim.floatAmplitude   = e->sim.amplitude;
            sim.floatOffset      = e->sim.offset;
        }

        currentOffset += entryByteSize(type);

        pdos_[0].entries.push_back(std::move(entry));
        simStates_.push_back(std::move(sim));
    }

    std::printf("[SimulatedAdapter] Built RT: %zu simulated entries in process image\n", simStates_.size());
    return true;
}

// ---------------------------------------------------------------------------
// RT cycle: generate synthetic values for INPUT channels only.
// OUTPUT channels pass through (application writes desired values).
// ---------------------------------------------------------------------------
void SimulatedAdapter::onBeforeReadInputs() noexcept
{
    if (pdos_.empty()) return;
    const std::size_t n = simStates_.size();
    for (std::size_t i = 0; i < n; ++i) {
        auto& sim   = simStates_[i];
        auto& entry = pdos_[0].entries[i];

        // Skip outputs — application controls these
        if (!entryIsInput(sim.type)) continue;

        switch (sim.type) {
            case pdo::EntryType::BoolInput: { // Periodic square-wave toggle
                uint8_t* ptr = entry.image + sim.byteOffset;
                if (sim.togglePeriodCycles > 0) {
                    bool shouldBeHigh = (sim.tickCount < sim.highCycles);
                    if (shouldBeHigh != sim.isHigh) {
                        *ptr       = static_cast<uint8_t>(shouldBeHigh ? 1 : 0);
                        sim.isHigh = shouldBeHigh;
                    }
                    sim.tickCount++;
                    if (sim.tickCount >= sim.togglePeriodCycles)
                        sim.tickCount = 0;
                }
                break;
            }

            case pdo::EntryType::Int8Input:
            case pdo::EntryType::Int16Input:
            case pdo::EntryType::Int32Input: { // Linear increment with optional bounds
                sim.intValue += sim.incrementPerCycle;
                if (sim.intValue > sim.maxValue)
                    sim.intValue = sim.minValue;  // Wrap to min on overflow
                else if (sim.intValue < sim.minValue)
                    sim.intValue = sim.maxValue;  // Wrap to max on underflow

                int32_t val32 = static_cast<int32_t>(sim.intValue);
                switch (entryBitSize(sim.type)) {
                    case pdo::SZ_8:
                        *(reinterpret_cast<int8_t*>(entry.image + sim.byteOffset)) = static_cast<int8_t>(val32);
                        break;
                    case pdo::SZ_16:
                        *(reinterpret_cast<int16_t*>(entry.image + sim.byteOffset)) = static_cast<int16_t>(val32);
                        break;
                    default:
                        *(reinterpret_cast<int32_t*>(entry.image + sim.byteOffset)) = val32;
                        break;
                }
                break;
            }

            case pdo::EntryType::FloatInput: { // Sinusoidal oscillation
                double sinVal = std::sin(sim.floatPhase) * sim.floatAmplitude + sim.floatOffset;
                *(reinterpret_cast<float*>(entry.image + sim.byteOffset)) = static_cast<float>(sinVal);
                sim.floatPhase += sim.phaseIncrement;
                if (sim.floatPhase > 2.0 * M_PI)
                    sim.floatPhase -= 2.0 * M_PI;
                break;
            }

            default:
                break;
        }
    }
}

void SimulatedAdapter::onAfterWriteOutputs() noexcept
{
    // Outputs are written by the application into PDO image during RT cycle.
    // No additional flush needed for simulated backend.
}

} // namespace dynamichardware::simulated
