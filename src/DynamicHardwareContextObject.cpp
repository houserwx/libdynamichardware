// ============================================================================
// DynamicHardwareContextObject — pure RT lifecycle implementation.
// Zero discovery knowledge. Created by HardwareOrchestrator::buildRT().
// ============================================================================

#include "dynamichardware/DynamicHardwareContextObject.h"

#include <cstdio>

namespace dynamichardware {

DynamicHardwareContextObject::DynamicHardwareContextObject(Impl&& impl)
    : impl_(std::move(impl))
{
}

DynamicHardwareContextObject::~DynamicHardwareContextObject()
{
    if (state_ != State::SHUTDOWN) {
        shutdown();
    }
}

bool DynamicHardwareContextObject::freeze()
{
    if (state_ != State::ACTIVE) return false;

    impl_.registry.freezeForRt();
    state_ = State::FROZEN;
    std::printf("[Context] Frozen: %zu total PDO entries ready for RT\n", entryCount());
    return true;
}

void DynamicHardwareContextObject::shutdown()
{
    if (state_ == State::SHUTDOWN) return;
    state_ = State::SHUTDOWN;
    std::printf("[Context] Shutdown\n");
}

// ---- RT cycle ----

void DynamicHardwareContextObject::readAll() noexcept
{
    impl_.registry.readAll();
}

void DynamicHardwareContextObject::writeAll() noexcept
{
    impl_.registry.writeAll();
}

// ---- Channel access ----

dhdo::DHDOEntry* DynamicHardwareContextObject::lookupByUuid(std::string_view uuid) noexcept
{
    return impl_.registry.lookupByUuid(uuid);
}

dhdo::DHDOEntry* DynamicHardwareContextObject::lookupByName(std::string_view name) noexcept
{
    auto it = impl_.nameToUuid.find(std::string{name});
    if (it == impl_.nameToUuid.end()) return nullptr;
    return impl_.registry.lookupByUuid(it->second);
}

const std::vector<dhdo::CatalogEntry>& DynamicHardwareContextObject::catalogEntries() const noexcept
{
    return impl_.catalog.entries();
}

// ---- Typed candidate queries ----

std::vector<DynamicHardwareContextObject::ChannelCandidate>
DynamicHardwareContextObject::getCandidates(uint8_t typeMask) const noexcept
{
    // We filter on channelType string matching.  Build a set of allowed types.
    struct TypeInfo {
        uint8_t mask;
        const char* typeName;
    };
    constexpr TypeInfo kTypes[] = {
        { dhdo::BoolInput,   "BoolInput" },
        { dhdo::BoolOutput,  "BoolOutput" },
        { dhdo::Int8Input,   "Int8Input" },
        { dhdo::Int16Input,  "Int16Input" },
        { dhdo::Int32Input,  "Int32Input" },
        { dhdo::FloatInput,  "FloatInput" },
        { dhdo::BoolOutput,  "BoolOutput" },
        { dhdo::Int8Output,  "Int8Output" },
        { dhdo::Int16Output, "Int16Output" },
        { dhdo::Int32Output, "Int32Output" },
        { dhdo::FloatOutput, "FloatOutput" },
    };

    std::vector<ChannelCandidate> result;
    for (const auto& entry : impl_.catalog.entries()) {
        bool matches = false;
        for (const auto& ti : kTypes) {
            if ((ti.mask & typeMask) && entry.channelType == ti.typeName) {
                matches = true;
                break;
            }
        }
        if (!matches) continue;

       result.push_back({entry.uuid, entry.name, entry.channelType, entry.isOutput});
    }
    return result;
}

std::vector<DynamicHardwareContextObject::ChannelCandidate>
DynamicHardwareContextObject::getBoolInputCandidates() const noexcept
{
    return getCandidates(dhdo::EntryType::BoolInput);
}

std::vector<DynamicHardwareContextObject::ChannelCandidate>
DynamicHardwareContextObject::getBoolOutputCandidates() const noexcept
{
    return getCandidates(dhdo::EntryType::BoolOutput);
}

std::vector<DynamicHardwareContextObject::ChannelCandidate>
DynamicHardwareContextObject::getFloatInputCandidates() const noexcept
{
    return getCandidates(dhdo::EntryType::FloatInput);
}

std::vector<DynamicHardwareContextObject::ChannelCandidate>
DynamicHardwareContextObject::getFloatOutputCandidates() const noexcept
{
    return getCandidates(dhdo::EntryType::FloatOutput);
}

// ---- Health monitoring ----

std::size_t DynamicHardwareContextObject::backendCount() const noexcept
{
    return impl_.registry.backendCount();
}

bool DynamicHardwareContextObject::allBackendsHealthy() const noexcept
{
    return impl_.registry.allBackendsHealthy();
}

std::size_t DynamicHardwareContextObject::entryCount() const noexcept
{
    return impl_.registry.entryCount();
}

// ---- Debug ----

void DynamicHardwareContextObject::printState() const
{
    impl_.registry.printState();
}

} // namespace dynamichardware
