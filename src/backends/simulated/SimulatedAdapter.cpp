#include "backends/simulated/SimulatedAdapter.h"
#include "dynamichardware/math/Math.h"

#include <cstring>

namespace fc::simulated {

bool SimulatedAdapter::initialize()
{
    cycleNs_ = static_cast<double>(cfg_.cycleNs());

    // Build one PDO with entries matching the config.
    pdos_.resize(1);
    pdos_[0].entries.reserve(cfg_.pdoEntries.size());

    simStates_.reserve(cfg_.pdoEntries.size());

    // First pass: compute total image size based on entry types.
    std::size_t totalImageBytes = 0;
    for (const auto& def : cfg_.pdoEntries) {
        if (def.channelType == "Encoder") {
            totalImageBytes += sizeof(int64_t);
        } else if (def.channelType == "AnalogInput" || def.channelType == "AnalogOutput") {
            totalImageBytes += sizeof(int16_t);
        } else {
            // DigitalInput, DigitalOutput — 1 byte each
            totalImageBytes += 1;
        }
    }
    pdos_[0].image.resize(totalImageBytes);

    // Second pass: build entries with proper byte offsets.
    std::size_t currentOffset = 0;
    for (const auto& def : cfg_.pdoEntries) {
        fc::pdo::PDOEntry entry;
        entry.uuid       = def.hwUuid;
        entry.byteOffset = static_cast<uint32_t>(currentOffset);

        SimState sim;

        if (def.channelType == "Encoder") {
            entry.type = fc::pdo::EntryType::Encoder;
            entry.bitLength = 32;
            sim.type = fc::pdo::EntryType::Encoder;
            sim.byteOffset = entry.byteOffset;
            // Physics path: compute incScaled from SimParams
            if (def.sim.rpm > 0.0f && def.sim.rollerDiamMm > 0.0f) {
                double circumferenceMm = M_PI * def.sim.rollerDiamMm;
                double mmPerSec = circumferenceMm * def.sim.rpm / 60.0;
                double ticksPerSec = mmPerSec * (def.sim.resolutionPpr > 0 ? def.sim.resolutionPpr : 1024.0);
                if (def.sim.quadrature) ticksPerSec *= 4.0;
                sim.incScaled = static_cast<int64_t>(ticksPerSec * (1LL << common::math::FixedShift));
            } else {
                sim.inc = 10;  // legacy fallback
            }
            currentOffset += sizeof(int64_t);
        } else if (def.channelType == "DigitalInput") {
            entry.type = fc::pdo::EntryType::DigitalInput;
            entry.bitLength = 1;
            sim.type = fc::pdo::EntryType::DigitalInput;
            sim.byteOffset = entry.byteOffset;
            if (def.sim.partsPerMin > 0.0f) {
                // Physics path: compute halfHighTicks from partsPerMin
                double secPerPart = 60.0 / def.sim.partsPerMin;
                double cyclesPerSec = 1e9 / cycleNs_;
                double totalTicks = secPerPart * cyclesPerSec;
                sim.halfHighTicks = static_cast<int32_t>(totalTicks * 0.3);
                sim.nominalLowTicks = static_cast<int32_t>(totalTicks * 0.7);
                sim.varianceFraction = def.sim.variancePercent / 100.0f;
                sim.varianceSeed = 1;
            } else {
                sim.toggleEvery = 20;  // legacy fallback
            }
            currentOffset += 1;
        } else if (def.channelType == "DigitalOutput") {
            entry.type = fc::pdo::EntryType::DigitalOutput;
            entry.bitLength = 1;
            entry.configurePulseMs(def.pulseMs);
            sim.type = fc::pdo::EntryType::DigitalOutput;
            sim.byteOffset = entry.byteOffset;
            currentOffset += 1;
        } else if (def.channelType == "AnalogInput") {
            entry.type = fc::pdo::EntryType::AnalogInput;
            entry.bitLength = 16;
            sim.type = fc::pdo::EntryType::AnalogInput;
            sim.byteOffset = entry.byteOffset;
            sim.staticAdc = 0;
            currentOffset += sizeof(int16_t);
        } else if (def.channelType == "AnalogOutput") {
            entry.type = fc::pdo::EntryType::AnalogOutput;
            entry.bitLength = 16;
            sim.type = fc::pdo::EntryType::AnalogOutput;
            sim.byteOffset = entry.byteOffset;
            currentOffset += sizeof(int16_t);
        }

        pdos_[0].entries.push_back(std::move(entry));
        simStates_.push_back(std::move(sim));
    }

    pdos_[0].freeze();
    return true;
}

void SimulatedAdapter::onBeforeReadInputs() noexcept
{
    auto& image = pdos_[0].image;

    for (std::size_t i = 0; i < simStates_.size(); ++i) {
        auto& sim = simStates_[i];

        switch (sim.type) {
            case fc::pdo::EntryType::Encoder: {
                if (sim.incScaled > 0) {
                    // Physics path: fixed-point accumulation
                    sim.accumulator += sim.incScaled;
                    int64_t whole = sim.accumulator >> common::math::FixedShift;
                    sim.accumulator &= (common::math::FixedOne - 1);
                    sim.count += whole;
                } else {
                    sim.count += sim.inc;
                }
                std::memcpy(image.data() + sim.byteOffset, &sim.count, sizeof(sim.count));
                break;
            }
            case fc::pdo::EntryType::DigitalInput: {
                if (sim.halfHighTicks > 0) {
                    // Physics path: variable-width pulse train
                    sim.phaseTick++;
                    int32_t threshold = sim.inHighPhase ? sim.halfHighTicks : sim.halfLowTicks;
                    if (sim.phaseTick >= threshold) {
                        sim.phaseTick = 0;
                        sim.inHighPhase = !sim.inHighPhase;
                        sim.rawBit = sim.inHighPhase;
                        // Vary low phase by ±varianceFraction
                        if (!sim.inHighPhase) {
                            uint64_t r = sim.varianceSeed;
                            r ^= r << 13; r ^= r >> 7; r ^= r << 17;
                            sim.varianceSeed = r;
                            float frac = static_cast<float>(r % 1000) / 1000.0f;
                            sim.halfLowTicks = static_cast<int32_t>(
                                sim.nominalLowTicks * (1.0f + (frac - 0.5f) * 2.0f * sim.varianceFraction));
                        }
                    }
                } else {
                    // Legacy path: simple toggle
                    sim.cycleTick++;
                    if (sim.cycleTick >= sim.toggleEvery) {
                        sim.cycleTick = 0;
                        sim.rawBit = !sim.rawBit;
                    }
                }
                uint8_t* bytePtr = image.data() + sim.byteOffset;
                if (sim.rawBit) *bytePtr |= 1u;
                else            *bytePtr &= ~1u;
                break;
            }
            case fc::pdo::EntryType::AnalogInput: {
                int16_t val = sim.staticAdc;
                std::memcpy(image.data() + sim.byteOffset, &val, sizeof(val));
                break;
            }
            default:
                break;
        }
    }
}

} // namespace fc::simulated
