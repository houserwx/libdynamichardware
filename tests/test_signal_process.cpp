#include <catch2/catch_test_macros.hpp>
#include <dynamichardware/rt/SignalProcess.h>
#include <thread>
#include <chrono>

using namespace dynamichardware::rt;

// ============================================================================
// Cycle timestamp cache
// ============================================================================
TEST_CASE("signalProcessTickNow returns monotonic time", "[signal]")
{
    uint64_t t1 = signalProcessTickNow();
    REQUIRE(t1 > 0);

    // Verify cached value matches
    uint64_t cached = signalProcessNowNs();
    REQUIRE(cached == t1);
}

TEST_CASE("signalProcessNowNs returns cached value without syscall", "[signal]")
{
    uint64_t t1 = signalProcessTickNow();
    uint64_t t2 = signalProcessNowNs();
    uint64_t t3 = signalProcessNowNs();

    REQUIRE(t1 == t2);
    REQUIRE(t2 == t3);  // Same cached value, no new clock_gettime
}

TEST_CASE("signalProcessTickNow updates cache", "[signal]")
{
    uint64_t t1 = signalProcessTickNow();

    // Small delay
    std::this_thread::sleep_for(std::chrono::microseconds(100));

    uint64_t t2 = signalProcessTickNow();
    REQUIRE(t2 >= t1);
}

// ============================================================================
// PulseMachine
// ============================================================================
TEST_CASE("PulseMachine latched mode", "[signal]")
{
    PulseMachine pm;
    pm.configure(0);  // Latched mode

    uint64_t now = 1'000'000'000ULL;

    pm.arm(true, now);
    REQUIRE(pm.isHighOrLatched() == true);
    REQUIRE(pm.tick(now) == true);

    pm.arm(false, now);
    REQUIRE(pm.isHighOrLatched() == false);
    REQUIRE(pm.tick(now) == false);
}

TEST_CASE("PulseMachine pulse mode", "[signal]")
{
    PulseMachine pm;
    pm.configure(100);  // 100ms pulse

    uint64_t now = 1'000'000'000ULL;

    pm.arm(true, now);
    REQUIRE(pm.isHighOrLatched() == true);

    // Within pulse duration (50ms)
    REQUIRE(pm.tick(now + 50'000'000ULL) == true);
    REQUIRE(pm.isHighOrLatched() == true);

    // After pulse expires (150ms) — tick returns false and clears active_
    REQUIRE(pm.tick(now + 150'000'000ULL) == false);
    REQUIRE(pm.isHighOrLatched() == false);
}

TEST_CASE("PulseMachine arm false doesn't start pulse", "[signal]")
{
    PulseMachine pm;
    pm.configure(100);

    uint64_t now = 1'000'000'000ULL;

    pm.arm(false, now);
    REQUIRE(pm.isHighOrLatched() == false);
    REQUIRE(pm.tick(now + 200'000'000ULL) == false);
}

TEST_CASE("PulseMachine re-arm after expire", "[signal]")
{
    PulseMachine pm;
    pm.configure(100);

    uint64_t now = 1'000'000'000ULL;

    pm.arm(true, now);
    REQUIRE(pm.isHighOrLatched() == true);

    // Let pulse expire first
    pm.tick(now + 200'000'000ULL);  // 200ms > 100ms, expires
    REQUIRE(pm.isHighOrLatched() == false);

    // Now re-arm (active_ is false, so arm() will engage)
    pm.arm(true, now + 200'000'000ULL);
    REQUIRE(pm.isHighOrLatched() == true);
    REQUIRE(pm.tick(now + 250'000'000ULL) == true);  // 50ms into new pulse
}

TEST_CASE("PulseMachine isPulseMode", "[signal]")
{
    PulseMachine pmLatched;
    pmLatched.configure(0);
    REQUIRE(pmLatched.isPulseMode() == false);

    PulseMachine pmPulse;
    pmPulse.configure(50);
    REQUIRE(pmPulse.isPulseMode() == true);
}

// ============================================================================
// DebounceMachine
// ============================================================================
TEST_CASE("DebounceMachine no debounce", "[signal]")
{
    DebounceMachine dm;
    dm.configure(0);

    uint64_t now = 1'000'000'000ULL;

    // With 0ms debounce, input stabilizes immediately
    REQUIRE(dm.filter(false, now) == false);
    REQUIRE(dm.filter(true, now) == true);
    REQUIRE(dm.filter(false, now) == false);
}

TEST_CASE("DebounceMachine filters bounce", "[signal]")
{
    DebounceMachine dm;
    dm.configure(10);  // 10ms debounce

    uint64_t now = 1'000'000'000ULL;

    // Input goes high
    REQUIRE(dm.filter(true, now) == false);  // Not yet settled

    // Still bouncing within debounce window
    REQUIRE(dm.filter(false, now + 1'000'000ULL) == false);  // Still false (waiting)
    // Actually after edge change, it resets. Let me trace through:
    // filter(true, now): raw!=rawInput_ (true!=false) → rawInput_=true, settleStart=now, settled=false
    //   settled=false so return settledV_ (false default)
    // filter(false, now+1ms): raw!=rawInput_ (false!=true) → rawInput_=false, settleStart=now+1ms, settled=false
    //   settled=false so return settledV_ (false default)
    // We need to keep input stable for debounce period

    // Start fresh: input goes true and stays true for 10ms+
    dm.configure(10);
    REQUIRE(dm.filter(true, now) == false);  // Edge detected, waiting
    REQUIRE(dm.filter(true, now + 5'000'000ULL) == false);  // 5ms — not settled
    REQUIRE(dm.filter(true, now + 10'000'000ULL) == true);  // 10ms — settled
    REQUIRE(dm.filter(true, now + 15'000'000ULL) == true);  // Still true
}

TEST_CASE("DebounceMachine settles on low", "[signal]")
{
    DebounceMachine dm;
    dm.configure(5);

    uint64_t now = 1'000'000'000ULL;

    // Start high (simulate initial state)
    dm.filter(true, now);

    // Stay high for debounce period to settle
    dm.filter(true, now + 5'000'000ULL);
    REQUIRE(dm.filter(true, now + 6'000'000ULL) == true);

    // Now go low
    REQUIRE(dm.filter(false, now + 7'000'000ULL) == true);  // Still settled true, waiting

    // Stay low for debounce period
    REQUIRE(dm.filter(false, now + 11'000'000ULL) == true);  // 4ms at low — not yet settled, still returns old true
    REQUIRE(dm.filter(false, now + 12'000'000ULL) == false);  // 5ms at low — settled low
}