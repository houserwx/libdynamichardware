#include "dynamichardware/backends/simulated/SimulatedRTBackend.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>

namespace dynamichardware::simulated {

// ---------------------------------------------------------------------------
// Helper: map channelType string → EntryType enum + isOutput flag
// ---------------------------------------------------------------------------
static void resolveChannelType(const std::string& type, dhdo::EntryType& outType, bool& outIsOutput) noexcept
{
    outIsOutput = false;

    if (type == "BoolInput")   outType = dhdo::EntryType::BoolInput;
    else if (type == "BoolOutput") { outType = dhdo::EntryType::BoolOutput; outIsOutput = true; }
    else if (type == "Int8Input")  outType = dhdo::EntryType::Int8Input;
    else if (type == "Int16Input") outType = dhdo::EntryType::Int16Input;
    else if (type == "Int32Input") outType = dhdo::EntryType::Int32Input;
    else if (type == "Int16Output") { outType = dhdo::EntryType::Int16Output; outIsOutput = true; }
    else if (type == "FloatInput")  outType = dhdo::EntryType::FloatInput;
    else if (type == "FloatOutput") { outType = dhdo::EntryType::FloatOutput; outIsOutput = true; }
}

// ---------------------------------------------------------------------------
// Helper: compute byte size from EntryType
// ---------------------------------------------------------------------------
static std::size_t entryByteSize(dhdo::EntryType t) noexcept
{
    switch (t) {
        case dhdo::EntryType::BoolInput:
        case dhdo::EntryType::BoolOutput:
            return 1;
        case dhdo::EntryType::Int8Input:
            return sizeof(int8_t);
        case dhdo::EntryType::Int16Input:
        case dhdo::EntryType::Int16Output:
            return sizeof(int16_t);
        case dhdo::EntryType::Int32Input:
        case dhdo::EntryType::FloatInput:
        case dhdo::EntryType::FloatOutput:
            return sizeof(uint32_t);
        default:
            return 1;  // Safe fallback
    }
}

SimulatedRTBackend::SimulatedRTBackend(std::string definitionsPath)
    : definitionsPath_(std::move(definitionsPath)) {}

// ---------------------------------------------------------------------------
// loadDefinitions() — parse JSON and extract cycle time + channel entries.
// Called from buildRT(). Fully self-contained: reads definitionsPath_ directly,
// same source file that SimulatedDiscovery parses independently for catalog.
// Returns parsed channel data in a local vector (no external state dependency).
// ---------------------------------------------------------------------------

bool SimulatedRTBackend::loadDefinitions(
        std::vector<SimChannelDef>& outChannels) noexcept
{
    std::ifstream f(definitionsPath_);
    if (!f) {
        std::fprintf(stderr, "[Simulated-RT] Cannot open '%s'\n", definitionsPath_.c_str());
        return false;
    }

    using json = nlohmann::json;
    auto j = json::parse(f);

    if (j.contains("cycleTimeUs")) {
        cycleNs_ = static_cast<uint32_t>(j["cycleTimeUs"].get<int>()) * 1000u;
    } else if (j.contains("cycleTimeNs")) {
        cycleNs_ = j["cycleTimeNs"].get<uint32_t>();
    }
    cycleNsD_ = static_cast<double>(cycleNs_);

    // Parse channel entries from JSON — same source Discovery reads for catalog,
    // but we own our own copy here (no shared state between discovery and RT).
    if (j.contains("channels") && j["channels"].is_array()) {
        for (const auto& ch : j["channels"]) {
            SimChannelDef def{};
            def.uuid       = ch.value("uuid", "SIM|unknown");
            
            dhdo::EntryType type{};
            bool isOutput{false};
            std::string chanTypeStr = ch.value("channelType", "BoolInput");
            resolveChannelType(chanTypeStr, type, isOutput);
            def.type     = type;
            def.isOutput = isOutput;

            // Extract simulation parameters (same fields as CatalogEntry::SimParams)
            const json* sim = nullptr;
            if (ch.contains("simParams")) sim = &ch["simParams"];
            
            if (sim) {
                def.togglePeriodMs      = (*sim).value("togglePeriodMs", 100u);
                def.dutyCyclePercent    = (*sim).value("dutyCyclePercent", 50.0f);
                def.incrementPerCycle   = (*sim).value("incrementPerCycle", 1);
                def.minValue            = (*sim).value("minValue", INT64_MIN);
                def.maxValue            = (*sim).value("maxValue", INT64_MAX);
                def.amplitude           = (*sim).value("amplitude", 1.0f);
                def.frequencyHz         = (*sim).value("frequencyHz", 1.0);
                def.offset              = (*sim).value("offset", 0.0f);
                def.debounceMs          = (*sim).value("debounceMs", 0u);
                def.pulseMs             = (*sim).value("pulseMs", 0u);
            }

            outChannels.push_back(std::move(def));
        }
    } else if (j.contains("entries") && j["entries"].is_array()) {
        // Fallback: older JSON format with "entries" key
        for (const auto& ch : j["entries"]) {
            SimChannelDef def{};
            def.uuid       = ch.value("uuid", ch.value("name", "SIM|unknown"));
            
            dhdo::EntryType type{};
            bool isOutput{false};
            std::string chanTypeStr = ch.value("channelType", ch.value("type", "BoolInput"));
            resolveChannelType(chanTypeStr, type, isOutput);
            def.type     = type;
            def.isOutput = isOutput;

            const json* sim = nullptr;
            if (ch.contains("simParams")) sim = &ch["simParams"];
            else if (ch.contains("sim"))   sim = &ch["sim"];
            
            if (sim) {
                def.togglePeriodMs      = (*sim).value("togglePeriodMs", 100u);
                def.dutyCyclePercent    = (*sim).value("dutyCyclePercent", 50.0f);
                def.incrementPerCycle   = (*sim).value("incrementPerCycle", 1);
                def.minValue            = (*sim).value("minValue", INT64_MIN);
                def.maxValue            = (*sim).value("maxValue", INT64_MAX);
                def.amplitude           = (*sim).value("amplitude", 1.0f);
                def.frequencyHz         = (*sim).value("frequencyHz", 1.0);
                def.offset              = (*sim).value("offset", 0.0f);
                def.debounceMs          = (*sim).value("debounceMs", 0u);
                def.pulseMs             = (*sim).value("pulseMs", 0u);
            }

            outChannels.push_back(std::move(def));
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// IRTBackend::buildRT() — build DHDO from JSON simdef entries + alloc buffers.
// Fully self-contained: parses definitionsPath_ directly (same source file that
// SimulatedDiscovery reads for catalog, but we own our own copy here).
// NOTE: Do NOT call dhdos_[0].freeze() here. Freeze is orchestrated by HardwareRegistry.
// ---------------------------------------------------------------------------
bool SimulatedRTBackend::buildRT()
{
    // Parse all channel definitions from JSON (cycle time + channel list)
    std::vector<SimChannelDef> channels;
    if (!loadDefinitions(channels)) {
        std::fprintf(stderr, "[Simulated-RT] Failed to load definitions\n");
        return false;
    }

    if (channels.empty()) {
        std::printf("[Simulated-RT] No simulated channels in '%s'\n", definitionsPath_.c_str());
        return true;
    }

    // First pass: compute total image size based on resolved EntryType
    std::size_t totalImageBytes = 0;
    for (const auto& c : channels) {
        totalImageBytes += entryByteSize(c.type);
    }

    // Build DHDO
    dhdos_.resize(1);
    dhdos_[0].image.resize(totalImageBytes);
    dhdos_[0].entries.reserve(channels.size());
    simStates_.reserve(channels.size());

    double cyclesPerSec = 1e9 / cycleNsD_;

    std::size_t currentOffset = 0;
    for (const auto& c : channels) {
        dynamichardware::dhdo::DHDOEntry entry{};
        entry.uuid       = c.uuid;
        entry.byteOffset = static_cast<uint32_t>(currentOffset);

        SimState sim{};
        sim.byteOffset = entry.byteOffset;

        dhdo::EntryType type     = c.type;
        bool isOutput            = c.isOutput;

        entry.type      = type;
        if (!isOutput && entryByteSize(type) > 1) {
            entry.bitLength = static_cast<uint8_t>(entryByteSize(type) * 8);
        } else {
            entry.bitLength = 1;
        }

        sim.type = type;

        // Apply I/O configuration from sim params
        if (c.debounceMs > 0)
            entry.configureDebounceMs(c.debounceMs);
        if (c.pulseMs > 0)
            entry.configurePulseMs(c.pulseMs);

        // Configure simulation state based on EntryType category
        if (type == dhdo::EntryType::BoolInput || type == dhdo::EntryType::BoolOutput) {
            uint32_t periodCycles = 0;
            if (c.togglePeriodMs > 0) {
                periodCycles = static_cast<uint32_t>(
                    c.togglePeriodMs / 1000.0 * cyclesPerSec);
            }
            sim.togglePeriodCycles = periodCycles;
            sim.highCycles = static_cast<uint32_t>(periodCycles * c.dutyCyclePercent / 100.0f);
            sim.lowCycles  = periodCycles - sim.highCycles;
            sim.isHigh     = false;
            sim.tickCount  = 0;
        } else if (dhdo::entryIsSigned(type) && !isOutput) {
            sim.intValue           = 0;
            sim.incrementPerCycle  = c.incrementPerCycle;
            sim.minValue           = c.minValue;
            sim.maxValue           = c.maxValue;
        } else if ((type & dhdo::BASE_FLOAT) && !isOutput) {
            sim.floatPhase       = 0.0;
            double hz = c.frequencyHz > 0 ? c.frequencyHz : 1.0;
            sim.phaseIncrement   = 2.0 * M_PI * hz / cyclesPerSec;
            sim.floatAmplitude   = c.amplitude;
            sim.floatOffset      = c.offset;
        }

        currentOffset += entryByteSize(type);

        dhdos_[0].entries.push_back(std::move(entry));
        simStates_.push_back(std::move(sim));
    }

    std::printf("[Simulated-RT] Built RT: %zu simulated channels in process image\n", simStates_.size());
    return true;
}

// ---------------------------------------------------------------------------
// RT cycle: generate synthetic values for INPUT channels only.
// OUTPUT channels pass through (application writes desired values).
// ---------------------------------------------------------------------------
void SimulatedRTBackend::onBeforeReadInputs() noexcept
{
    if (dhdos_.empty()) return;
    const std::size_t n = simStates_.size();
    for (std::size_t i = 0; i < n; ++i) {
        auto& sim   = simStates_[i];
        auto& entry = dhdos_[0].entries[i];

        // Skip outputs — application controls these
        if (!dhdo::entryIsInput(sim.type)) continue;

        switch (sim.type) {
            case dhdo::EntryType::BoolInput: { // Periodic square-wave toggle
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

            case dhdo::EntryType::Int8Input:
            case dhdo::EntryType::Int16Input:
            case dhdo::EntryType::Int32Input: { // Linear increment with optional bounds
                sim.intValue += sim.incrementPerCycle;
                if (sim.intValue > sim.maxValue)
                    sim.intValue = sim.minValue;  // Wrap to min on overflow
                else if (sim.intValue < sim.minValue)
                    sim.intValue = sim.maxValue;  // Wrap to max on underflow

                int32_t val32 = static_cast<int32_t>(sim.intValue);
                switch (dhdo::entryBitSize(sim.type)) {
                    case dhdo::SZ_8:
                        *(reinterpret_cast<int8_t*>(entry.image + sim.byteOffset)) = static_cast<int8_t>(val32);
                        break;
                    case dhdo::SZ_16:
                        *(reinterpret_cast<int16_t*>(entry.image + sim.byteOffset)) = static_cast<int16_t>(val32);
                        break;
                    default:
                        *(reinterpret_cast<int32_t*>(entry.image + sim.byteOffset)) = val32;
                        break;
                }
                break;
            }

            case dhdo::EntryType::FloatInput: { // Sinusoidal oscillation
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

void SimulatedRTBackend::onAfterWriteOutputs() noexcept
{
    // Outputs are written by the application into PDO image during RT cycle.
    // No additional flush needed for simulated backend.
}

} // namespace dynamichardware::simulated
