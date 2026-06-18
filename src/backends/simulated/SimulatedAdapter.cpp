#include "backends/simulated/SimulatedAdapter.h"

#include <nlohmann/json.hpp>
#include <cstring>
#include <cmath>
#include <fstream>
#include <cstdio>

// Inline fixed-point constants (20-bit fractional)
static constexpr int kFixedShift = 20;
static constexpr int64_t kFixedOne = 1LL << kFixedShift;

namespace fc::simulated {

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
        fc::pdo::CatalogEntry entry;
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
// Build PDO from catalog entries
// ---------------------------------------------------------------------------
bool SimulatedAdapter::initialize()
{
    if (!catalog_ || catalog_->empty()) {
        std::printf("[SimulatedAdapter] No catalog or empty catalog — nothing to simulate\n");
        return true;
    }

    const auto& entries = catalog_->entries();

    // Filter simulated entries
    std::vector<const fc::pdo::CatalogEntry*> simEntries;
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
        fc::pdo::PDOEntry entry;
        entry.uuid       = e->uuid;
        entry.byteOffset = static_cast<uint32_t>(currentOffset);

        SimState sim;
        sim.byteOffset = entry.byteOffset;

        if (e->channelType == "Encoder") {
            entry.type = fc::pdo::EntryType::Encoder;
            entry.bitLength = 32;
            sim.type = fc::pdo::EntryType::Encoder;
            if (e->sim.rpm > 0.0f && e->sim.rollerDiamMm > 0.0f) {
                double circ = M_PI * e->sim.rollerDiamMm;
                double mmPerSec = circ * e->sim.rpm / 60.0;
                double ticksPerSec = mmPerSec * (e->sim.resolutionPpr > 0 ? e->sim.resolutionPpr : 1024.0);
                if (e->sim.quadrature) ticksPerSec *= 4.0;
                sim.incScaled = static_cast<int64_t>(ticksPerSec * kFixedOne);
            } else {
                sim.inc = 10;
            }
            currentOffset += sizeof(int64_t);
        } else if (e->channelType == "DigitalInput") {
            entry.type = fc::pdo::EntryType::DigitalInput;
            entry.bitLength = 1;
            entry.configureDebounceMs(e->sim.debounceMs);
            sim.type = fc::pdo::EntryType::DigitalInput;
            if (e->sim.partsPerMin > 0.0f) {
                double secPerPart = 60.0 / e->sim.partsPerMin;
                double cyclesPerSec = 1e9 / cycleNsD_;
                double totalTicks = secPerPart * cyclesPerSec;
                sim.halfHighTicks = static_cast<int32_t>(totalTicks * 0.3);
                sim.nominalLowTicks = static_cast<int32_t>(totalTicks * 0.7);
                sim.varianceFraction = e->sim.variancePercent / 100.0f;
                sim.varianceSeed = 1;
            }
            currentOffset += 1;
        } else if (e->channelType == "DigitalOutput") {
            entry.type = fc::pdo::EntryType::DigitalOutput;
            entry.bitLength = 1;
            entry.configurePulseMs(e->sim.pulseMs);
            sim.type = fc::pdo::EntryType::DigitalOutput;
            currentOffset += 1;
        } else if (e->channelType == "AnalogInput") {
            entry.type = fc::pdo::EntryType::AnalogInput;
            entry.bitLength = 16;
            sim.type = fc::pdo::EntryType::AnalogInput;
            currentOffset += sizeof(int16_t);
        } else if (e->channelType == "AnalogOutput") {
            entry.type = fc::pdo::EntryType::AnalogOutput;
            entry.bitLength = 16;
            sim.type = fc::pdo::EntryType::AnalogOutput;
            currentOffset += sizeof(int16_t);
        }

        pdos_[0].entries.push_back(std::move(entry));
        simStates_.push_back(std::move(sim));
    }

    pdos_[0].freeze();
    std::printf("[SimulatedAdapter] Built PDO with %zu simulated entries\n", simStates_.size());
    return true;
}

// ---------------------------------------------------------------------------
// RT cycle: generate synthetic values
// ---------------------------------------------------------------------------
void SimulatedAdapter::onBeforeReadInputs() noexcept
{
    auto& image = pdos_[0].image;

    for (std::size_t i = 0; i < simStates_.size(); ++i) {
        auto& sim = simStates_[i];

        switch (sim.type) {
            case fc::pdo::EntryType::Encoder: {
                if (sim.incScaled > 0) {
                    sim.accumulator += sim.incScaled;
                    int64_t whole = sim.accumulator >> kFixedShift;
                    sim.accumulator &= (kFixedOne - 1);
                    sim.count += whole;
                } else {
                    sim.count += sim.inc;
                }
                std::memcpy(image.data() + sim.byteOffset, &sim.count, sizeof(sim.count));
                break;
            }
            case fc::pdo::EntryType::DigitalInput: {
                sim.cycleTick++;
                if (sim.halfHighTicks > 0) {
                    // Physics path: variable-width pulse
                    if (sim.cycleTick > (sim.toggle ? sim.halfHighTicks : sim.halfLowTicks)) {
                        sim.cycleTick = 0;
                        sim.toggle = !sim.toggle;
                        if (!sim.toggle && sim.varianceFraction > 0.0f) {
                            uint64_t r = sim.varianceSeed;
                            r ^= r << 13; r ^= r >> 7; r ^= r << 17;
                            sim.varianceSeed = r;
                            float frac = static_cast<float>(r % 1000) / 1000.0f;
                            sim.halfLowTicks = static_cast<int32_t>(
                                sim.nominalLowTicks * (1.0f + (frac - 0.5f) * 2.0f * sim.varianceFraction));
                        }
                    }
                } else {
                    // Simple toggle every 20 cycles
                    if (sim.cycleTick >= 20) {
                        sim.cycleTick = 0;
                        sim.toggle = !sim.toggle;
                    }
                }
                uint8_t* bytePtr = image.data() + sim.byteOffset;
                if (sim.toggle) *bytePtr |= 1u;
                else            *bytePtr &= ~1u;
                break;
            }
            case fc::pdo::EntryType::AnalogInput: {
                int16_t val = sim.adc;
                std::memcpy(image.data() + sim.byteOffset, &val, sizeof(val));
                break;
            }
            default:
                break;
        }
    }
}

void SimulatedAdapter::onAfterWriteOutputs() noexcept
{
    // Outputs are written into the PDO image by the RT cycle;
    // simulated adapter doesn't need to flush anywhere.
}

} // namespace fc::simulated
