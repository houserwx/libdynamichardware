#include "dynamichardware/backends/simulated/SimulatedAdapter.h"

#include <nlohmann/json.hpp>
#include <cstring>
#include <cmath>
#include <fstream>
#include <cstdio>

// Inline fixed-point constants (20-bit fractional)
static constexpr int kFixedShift = 20;
static constexpr int64_t kFixedOne = 1LL << kFixedShift;

namespace dynamichardware::simulated {

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
        entry.channelType = ch.value("channelType", "DigitalInput");
        entry.name        = ch.value("name", "");
        entry.slaveName   = "Simulated";
        entry.isSimulated = true;
        entry.isOutput    = (entry.channelType == "DigitalOutput" ||
                             entry.channelType == "AnalogOutput");

        if (ch.contains("sim")) {
            const auto& s = ch["sim"];
            entry.sim.rpm             = s.value("rpm", 0.0f);
            entry.sim.rollerDiamMm    = s.value("rollerDiamMm", 0.0f);
            entry.sim.resolutionPpr   = s.value("resolutionPpr", 0u);
            entry.sim.quadrature      = s.value("quadrature", false);
            entry.sim.partsPerMin     = s.value("partsPerMin", 0.0f);
            entry.sim.partWidthMm     = s.value("partWidthMm", 0.0f);
            entry.sim.variancePercent = s.value("variancePercent", 0.0f);
            entry.sim.pulseMs         = s.value("pulseMs", 0u);
            entry.sim.debounceMs      = s.value("debounceMs", 0u);
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

    // First pass: compute total image size
    std::size_t totalImageBytes = 0;
    for (const auto* e : simEntries) {
        if (e->channelType == "Encoder") {
            totalImageBytes += sizeof(int64_t);
        } else if (e->channelType == "AnalogInput" || e->channelType == "AnalogOutput") {
            totalImageBytes += sizeof(int16_t);
        } else {
            totalImageBytes += 1;
        }
    }

    // Build PDO
    pdos_.resize(1);
    pdos_[0].image.resize(totalImageBytes);
    pdos_[0].entries.reserve(simEntries.size());
    simStates_.reserve(simEntries.size());

    std::size_t currentOffset = 0;
    for (const auto* e : simEntries) {
        dynamichardware::pdo::PDOEntry entry;
        entry.uuid       = e->uuid;
        entry.byteOffset = static_cast<uint32_t>(currentOffset);

        SimState sim;
        sim.byteOffset = entry.byteOffset;

        if (e->channelType == "Encoder") {
            entry.type      = dynamichardware::pdo::EntryType::Int32Input;
            entry.bitLength = 32;
            sim.type        = dynamichardware::pdo::EntryType::Int32Input;
            if (e->sim.rpm > 0.0f && e->sim.rollerDiamMm > 0.0f) {
                double circ       = M_PI * e->sim.rollerDiamMm;
                double mmPerSec   = circ * e->sim.rpm / 60.0;
                double ticksPerSec = mmPerSec * (e->sim.resolutionPpr > 0 ? e->sim.resolutionPpr : 1024.0);
                if (e->sim.quadrature) ticksPerSec *= 4.0;
                sim.incScaled     = static_cast<int64_t>(ticksPerSec * kFixedOne);
            } else {
                sim.inc = 10;
            }
            currentOffset += sizeof(int64_t);
        } else if (e->channelType == "DigitalInput") {
            entry.type      = dynamichardware::pdo::EntryType::BoolInput;
            entry.bitLength = 1;
            entry.configureDebounceMs(e->sim.debounceMs);
            sim.type        = dynamichardware::pdo::EntryType::BoolInput;
            if (e->sim.partsPerMin > 0.0f) {
                double secPerPart   = 60.0 / e->sim.partsPerMin;
                double cyclesPerSec = 1e9 / cycleNsD_;
                double totalTicks   = secPerPart * cyclesPerSec;
                sim.halfHighTicks    = static_cast<int32_t>(totalTicks * 0.3);
                sim.nominalLowTicks  = static_cast<int32_t>(totalTicks * 0.7);
                sim.varianceFraction = e->sim.variancePercent / 100.0f;
                sim.varianceSeed     = 1;
            }
            currentOffset += 1;
        } else if (e->channelType == "DigitalOutput") {
            entry.type      = dynamichardware::pdo::EntryType::BoolOutput;
            entry.bitLength = 1;
            entry.configurePulseMs(e->sim.pulseMs);
            sim.type        = dynamichardware::pdo::EntryType::BoolOutput;
            currentOffset   += 1;
        } else if (e->channelType == "AnalogInput") {
            entry.type      = dynamichardware::pdo::EntryType::Int16Input;
            entry.bitLength = 16;
            sim.type        = dynamichardware::pdo::EntryType::Int16Input;
            currentOffset   += sizeof(int16_t);
        } else if (e->channelType == "AnalogOutput") {
            entry.type      = dynamichardware::pdo::EntryType::Int16Output;
            entry.bitLength = 16;
            sim.type        = dynamichardware::pdo::EntryType::Int16Output;
            currentOffset   += sizeof(int16_t);
        }

        pdos_[0].entries.push_back(std::move(entry));
        simStates_.push_back(std::move(sim));
    }

    std::printf("[SimulatedAdapter] Built RT: %zu simulated entries in process image\n", simStates_.size());
    return true;
}

// ---------------------------------------------------------------------------
// RT cycle: generate synthetic values
// ---------------------------------------------------------------------------
void SimulatedAdapter::onBeforeReadInputs() noexcept
{
    if (pdos_.empty()) return;
    const std::size_t n = simStates_.size(); // Should match pdos_[0].entries.size()
    for (std::size_t i = 0; i < n; ++i) {
        auto& sim    = simStates_[i];
        auto& entry  = pdos_[0].entries[i];

        switch (sim.type) {
            case dynamichardware::pdo::EntryType::Int32Input: { // Encoder
                int64_t* ptr       = reinterpret_cast<int64_t*>(entry.image + sim.byteOffset);
                if (sim.incScaled != 0) {
                    sim.accumulator += sim.incScaled >> kFixedShift;
                    *ptr += static_cast<int64_t>(sim.accumulator >> kFixedShift);
                    sim.accumulator &= ((int64_t(1) << kFixedShift) - 1);
                } else {
                    (*ptr) += sim.inc;
                }
                break;
            }
            case dynamichardware::pdo::EntryType::BoolInput: { // Digital input — pulse generator with variance
                uint8_t* ptr     = entry.image + sim.byteOffset;
                sim.cycleTick++;
                if (!sim.toggle && sim.halfHighTicks > 0) {
                    if (sim.cycleTick >= sim.halfHighTicks) {
                        *ptr      = 1; // Rising edge
                        sim.toggle = true;
                        // Apply jitter to low phase
                        double randFrac   = (static_cast<double>(sim.varianceSeed % 1000)) / 1000.0;
                        int32_t jittered  = static_cast<int32_t>(sim.nominalLowTicks * (1.0 + (randFrac - 0.5) * sim.varianceFraction));
                        sim.cycleTick     = -jittered; // Start counting down for low phase
                        sim.varianceSeed  = sim.varianceSeed * 1103515245 + 12345;
                    }
                } else if (sim.toggle) {
                    if (sim.cycleTick >= 0) {
                        *ptr       = 0; // Falling edge
                        sim.toggle = false;
                        sim.cycleTick = -sim.halfHighTicks; // Start counting down for high phase (with possible jitter on next cycle)
                    }
                }
                break;
            }
            case dynamichardware::pdo::EntryType::Int16Input: { // Analog input — sine wave simulation
                int16_t* ptr      = reinterpret_cast<int16_t*>(entry.image + sim.byteOffset);
                sim.cycleTick++;
                double angle      = (sim.cycleTick % 1000) / 1000.0 * 2.0 * M_PI;
                *ptr              = static_cast<int16_t>(std::sin(angle) * 32767.0 * 0.8);
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
