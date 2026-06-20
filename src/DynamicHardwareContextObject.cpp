// ============================================================================
// DynamicHardwareContextObject — pure RT lifecycle implementation.
// Zero discovery knowledge. Created by DynamicHardwareContextFactory::buildRT().
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
