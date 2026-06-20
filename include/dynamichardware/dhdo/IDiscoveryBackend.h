#pragma once

// ============================================================
// IDiscoveryBackend — one-shot hardware scan interface.
//
// Separation rationale (ISP): Discovery is a transient init-time concern.
// After build() completes, the discovery object can be discarded entirely,
// leaving only the IRTBackend responsible for deterministic RT cycles.
//
// Lifecycle:
//   1. Context creates discovery object per enabled transport type
//   2. Calls setCatalog(&catalog) so discovered channels get registered
//   3. Calls discover() → populates HardwareCatalog with all available pins/slaves/devices
//   4. Discovery object may be destroyed after consumer configuration phase
//      (no state survives into frozen mode)
//
// SOLID notes:
//   - Interface Segregation: consumers who only need catalog population never see RT hooks.
//     Backends implement both interfaces but they serve different lifecycle owners.
//   - Dependency Inversion: this interface depends on abstractions (HardwareCatalog),
//     not concrete transport libraries (libgpiod, ethercat master, etc.).
// ============================================================

namespace dynamichardware::dhdo {

class HardwareCatalog;

class IDiscoveryBackend {
public:
    virtual ~IDiscoveryBackend() = default;

    IDiscoveryBackend(const IDiscoveryBackend&)            = delete;
    IDiscoveryBackend& operator=(const IDiscoveryBackend&) = delete;
    IDiscoveryBackend(IDiscoveryBackend&&)                 = delete;
    IDiscoveryBackend& operator=(IDiscoveryBackend&&)      = delete;

    /// Attach the target catalog — discovery will register entries here.
    void setCatalog(HardwareCatalog* catalog) noexcept { catalog_ = catalog; }

    /// Scan hardware and populate the attached catalog with all available channels.
    /// This is a one-time call during build().  Returns true if at least one channel found.
    /// Does NOT create DHDOEntry objects or request hardware handles — pure catalog population.
    [[nodiscard]] virtual bool discover() = 0;

protected:
    IDiscoveryBackend() = default;

    HardwareCatalog* catalog_{nullptr};
};

} // namespace dynamichardware::dhdo
