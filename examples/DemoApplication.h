#pragma once

// ============================================================================
// DemoApplication.h — Development-only hardware smoke-test thread.
//
// Purpose:
//   Runs HardwareDemoRoutine (walk/flip/pulse/ExF patterns) in an RT cycle
//   to validate hardware layer, EtherCAT adapter, simulated adapter, and
//   PulseMachine/DebounceMachine behaviour.
//
//   NOT for production use.  The production RT shell is Application.
//
// Lifecycle (called from src/main_demo.cpp):
//   1. Construct DemoApplication(registry, cycleNs)
//   2. Call start()        — spawns the OS thread
//   3. Call join()         — blocks until the RT loop exits
//   (requestStop() is callable from signal handler at any point after start)
//
// Thread safety:
//   - running_ is an atomic<bool> — requestStop() is safe from any thread.
//   - All other members are accessed exclusively on the RT thread.
//   - Diagnostic accessors use relaxed atomics — valid after join().
// ============================================================================

#include "services/thread/Threadrunner.h"
#include "HardwareDemoRoutine.h"
#include "HardwareRegistry.h"

#include <atomic>
#include <cstdint>

namespace civ_control {

class DemoApplication final : public Threadrunner {
public:
    /// @param registry  Hardware registry frozen and ready for RT use.
    /// @param cycleNs   Target cycle time in nanoseconds (e.g. 500'000 = 500 µs).
    DemoApplication(pdomodel::HardwareRegistry& registry, uint32_t cycleNs);

    ~DemoApplication() override = default;

    // -----------------------------------------------------------------------
    // Control
    // -----------------------------------------------------------------------

    /// Signal the RT loop to exit cleanly on the next iteration.
    /// Async-signal-safe: writes one atomic bool.
    void requestStop() noexcept;

    // -----------------------------------------------------------------------
    // Read-only diagnostics (valid after join())
    // -----------------------------------------------------------------------
    [[nodiscard]] uint64_t cycleCount()   const noexcept;
    [[nodiscard]] int      overrunCount() const noexcept;
    [[nodiscard]] int64_t  maxOverrunNs() const noexcept;

    // -----------------------------------------------------------------------
    // Threadrunner override — the RT thread body.
    // Called by Threadrunner::execute() after SCHED_FIFO / CPU-pin setup.
    // -----------------------------------------------------------------------
    void run() override;

private:
    // -----------------------------------------------------------------------
    // Channel ID constants — used by logDiagnostics() only (not RT-hot-path).
    // Match SimulatedAdapter defaults and EL1084/EL2409 slave layout.
    // These will be removed when the UUID direct-reference redesign lands.
    // -----------------------------------------------------------------------
    static constexpr int64_t  kIdDiFirst =  14;    // EL1084 ch1
    static constexpr int64_t  kIdDiLast  =  17;    // EL1084 ch4
    static constexpr int64_t  kIdSimDiA  =  40;    // Sim DI A  (debounce 50 ms)
    static constexpr int64_t  kIdSimDiB  =  41;    // Sim DI B  (debounce 200 ms)
    static constexpr int64_t  kIdSimDoA  =  50;    // Sim DO A  (pulse 750 ms)
    static constexpr int64_t  kIdSimDoB  =  51;    // Sim DO B  (pulse 2000 ms)

    // -----------------------------------------------------------------------
    // Per-cycle demo logic.  Called between readAll() and writeAll().
    // -----------------------------------------------------------------------
    void rtCycle() noexcept;

    /// Emit a one-line diagnostic to stdout (called every ~500 ms).
    void logDiagnostics() const noexcept;

    // -----------------------------------------------------------------------
    // Dependencies (dependency-injected; not owned)
    // -----------------------------------------------------------------------
    pdomodel::HardwareRegistry& registry_;
    uint32_t                    cycleNs_;

    // -----------------------------------------------------------------------
    // Demo entity (owned)
    // -----------------------------------------------------------------------
    HardwareDemoRoutine demoRoutine_;

    // -----------------------------------------------------------------------
    // RT loop state
    // -----------------------------------------------------------------------
    std::atomic<bool> running_{false};
    uint64_t  cycleCount_   {0};
    int64_t   maxOverrunNs_ {0};
    int64_t   totalOverNs_  {0};
    int       overrunCount_ {0};
};

} // namespace civ_control
