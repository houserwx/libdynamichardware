// ============================================================================
// DemoApplication.cpp — Development-only hardware smoke-test thread.
//
// Runs HardwareDemoRoutine (walk / flip / pulse re-arm / position-gated
// trigger) in a hard RT cycle to validate PDO model plumbing.
// This file is only compiled into the pdo_model_demo target.
// ============================================================================

#include "DemoApplication.h"
#include "HardwareRegistry.h"
#include "SignalProcess.h"
#include "services/log/LogHelper.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <time.h>

namespace {

[[nodiscard]] struct timespec addNsToTs(struct timespec ts, int64_t ns) noexcept
{
    ts.tv_nsec += static_cast<long>(ns);
    while (ts.tv_nsec >= 1'000'000'000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1'000'000'000L;
    }
    while (ts.tv_nsec < 0L) {
        ts.tv_sec--;
        ts.tv_nsec += 1'000'000'000L;
    }
    return ts;
}

[[nodiscard]] int64_t diffNs(const struct timespec& a, const struct timespec& b) noexcept
{
    return ((a.tv_sec - b.tv_sec) * 1'000'000'000LL)
         + static_cast<int64_t>(a.tv_nsec - b.tv_nsec);
}

} // anonymous namespace

namespace civ_control {

DemoApplication::DemoApplication(pdomodel::HardwareRegistry& registry, uint32_t cycleNs)
    : Threadrunner(ThreadConfiguration{
          .name               = "DemoApplication",
          .cpuCore            = -1,
          .priority           = 85,
          .useRealtime        = true,
          .stackPrefaultBytes = 0UL
      })
    , registry_(registry)
    , cycleNs_(cycleNs)
    , demoRoutine_(registry_)
{
}

uint64_t DemoApplication::cycleCount()   const noexcept { return cycleCount_; }
int      DemoApplication::overrunCount() const noexcept { return overrunCount_; }
int64_t  DemoApplication::maxOverrunNs() const noexcept { return maxOverrunNs_; }

void DemoApplication::requestStop() noexcept
{
    running_.store(false, std::memory_order_release);
}

void DemoApplication::run()
{
    threadLoggerInit(true);

    running_.store(true, std::memory_order_release);

    std::printf("[DemoApplication] Starting RT loop @ %u ns/cycle — press Ctrl-C to stop\n",
                cycleNs_);
    std::printf("-------------------------------------------------------------------\n");
    std::fflush(stdout);

    struct timespec nextWakeup{};
    clock_gettime(CLOCK_MONOTONIC, &nextWakeup);
    nextWakeup = addNsToTs(nextWakeup, 100'000LL);

    while (running_.load(std::memory_order_acquire)) {
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &nextWakeup, nullptr);
        nextWakeup = addNsToTs(nextWakeup, static_cast<int64_t>(cycleNs_));
        ++cycleCount_;

        struct timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        const int64_t overNs = diffNs(now, nextWakeup);
        if (overNs > 0) {
            ++overrunCount_;
            totalOverNs_ += overNs;
            maxOverrunNs_ = std::max(overNs, maxOverrunNs_);
        }

        pdomodel::signalProcessTickNow();

        registry_.readAll();
        rtCycle();
        registry_.writeAll();

        if (cycleNs_ > 0) {
            const uint64_t logEvery = 500'000'000ULL / cycleNs_;
            if (logEvery > 0 && (cycleCount_ % logEvery) == 0) {
                logDiagnostics();
            }

            const uint64_t dumpEvery = 5'000'000'000ULL / cycleNs_;
            if (dumpEvery > 0 && (cycleCount_ % dumpEvery) == 0) {
                std::printf("\n=== State dump @ cycle %llu ===\n",
                            static_cast<unsigned long long>(cycleCount_));
                registry_.printState();
            }
        }
    }

    std::printf("\n=== [DemoApplication] Stopped after %llu cycles ===\n",
                static_cast<unsigned long long>(cycleCount_));
    std::printf("Timing: overruns=%d  max=%lld ns  avg=%lld ns\n",
                overrunCount_,
                static_cast<long long>(maxOverrunNs_),
                (overrunCount_ > 0)
                    ? static_cast<long long>(totalOverNs_ / static_cast<int64_t>(overrunCount_))
                    : 0LL);
    std::printf("\nFinal hardware state:\n");
    std::fflush(stdout);
    registry_.printState();
}

void DemoApplication::rtCycle() noexcept
{
    demoRoutine_.tick(cycleCount_, pdomodel::signalProcessNowNs());
}

void DemoApplication::logDiagnostics() const noexcept
{
    const bool di1    = registry_.getDigitalInput(kIdDiFirst);
    const bool di2    = registry_.getDigitalInput(kIdDiFirst + 1);
    const bool di3    = registry_.getDigitalInput(kIdDiFirst + 2);
    const bool di4    = registry_.getDigitalInput(kIdDiLast);
    const bool simDiA = registry_.getDigitalInput(kIdSimDiA);
    const bool simDiB = registry_.getDigitalInput(kIdSimDiB);
    const bool simDoA = registry_.getDigitalOutput(kIdSimDoA);
    const bool simDoB = registry_.getDigitalOutput(kIdSimDoB);

    std::printf("[Cycle %7llu] DI=%d%d%d%d  simDI=%d%d "
                "simDO=%d%d  exFn.triggers=%lld  overruns=%d max=%lld ns\n",
                static_cast<unsigned long long>(cycleCount_),
                static_cast<int>(di1), static_cast<int>(di2),
                static_cast<int>(di3), static_cast<int>(di4),
                static_cast<int>(simDiA), static_cast<int>(simDiB),
                static_cast<int>(simDoA), static_cast<int>(simDoB),
                static_cast<long long>(demoRoutine_.triggerCount()),
                overrunCount_,
                static_cast<long long>(maxOverrunNs_));
}

} // namespace civ_control
