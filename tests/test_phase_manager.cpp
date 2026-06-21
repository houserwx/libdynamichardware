#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <dynamichardware/config/PhaseManager.h>

using namespace dynamichardware::config;

// ============================================================================
// PhaseManager — lifecycle state machine tests
// Enforces DISCOVERY → MAPPING → BUILD_RT → RUNNING → SHUTDOWN ordering
// ============================================================================

TEST_CASE("PhaseManager starts at DISCOVERY by default", "[phase]") {
    PhaseManager pm;
    CHECK(pm.get() == HardwarePhase::DISCOVERY);
}

TEST_CASE("PhaseManager can start at explicit phase", "[phase]") {
    PhaseManager pm(HardwarePhase::MAPPING);
    CHECK(pm.get() == HardwarePhase::MAPPING);
}

TEST_CASE("PhaseManager advance from DISCOVERY to MAPPING succeeds", "[phase]") {
    PhaseManager pm;
    CHECK(pm.isAt(HardwarePhase::DISCOVERY));
    
    bool result = pm.advance(HardwarePhase::MAPPING);
    CHECK(result == true);
    CHECK(pm.isAt(HardwarePhase::MAPPING));
}

TEST_CASE("PhaseManager advance through full valid sequence", "[phase]") {
    PhaseManager pm;
    
    (void)pm.advance(HardwarePhase::MAPPING);
    CHECK(pm.isAt(HardwarePhase::MAPPING));
    
    (void)pm.advance(HardwarePhase::BUILD_RT);
    CHECK(pm.isAt(HardwarePhase::BUILD_RT));
    
    (void)pm.advance(HardwarePhase::RUNNING);
    CHECK(pm.isAt(HardwarePhase::RUNNING));
    
    // SHUTDOWN is always reachable from any state
    (void)pm.advance(HardwarePhase::SHUTDOWN);
    CHECK(pm.isAt(HardwarePhase::SHUTDOWN));
}

TEST_CASE("PhaseManager cannot go backward in phase sequence", "[phase]") {
    PhaseManager pm;
    (void)pm.advance(HardwarePhase::MAPPING);
    
    REQUIRE_THROWS_AS(pm.advance(HardwarePhase::DISCOVERY), std::invalid_argument);
}

TEST_CASE("PhaseManager cannot skip phases forward", "[phase]") {
    PhaseManager pm;  // Starts at DISCOVERY
    
    // Can't jump directly to BUILD_RT (skips MAPPING)
    REQUIRE_THROWS_AS(pm.advance(HardwarePhase::BUILD_RT), std::invalid_argument);
}

TEST_CASE("PhaseManager can jump to SHUTDOWN from any phase", "[phase]") {
    PhaseManager pm;  // DISCOVERY
    
    // Direct to SHUTDOWN should work
    bool result = pm.advance(HardwarePhase::SHUTDOWN);
    CHECK(result == true);
    CHECK(pm.isAt(HardwarePhase::SHUTDOWN));
}

TEST_CASE("PhaseManager advance same phase fails", "[phase]") {
    PhaseManager pm;  // DISCOVERY
    
    // Advancing TO the current phase is invalid (must move forward)
    REQUIRE_THROWS_AS(pm.advance(HardwarePhase::DISCOVERY), std::invalid_argument);
}

TEST_CASE("PhaseManager error message includes phase names", "[phase]") {
    PhaseManager pm;  // DISCOVERY
    
   try {
        (void)pm.advance(HardwarePhase::RUNNING);  // Skip two phases ahead — invalid; cast to void silences nodiscard since we expect throw
        FAIL("Expected exception was not thrown");
    } catch (const std::invalid_argument& ex) {
        std::string msg = ex.what();
        CHECK(msg.find("DISCOVERY") != std::string::npos);
        CHECK(msg.find("RUNNING") != std::string::npos);
    }
}

TEST_CASE("PhaseManager isAt returns correct value for all phases", "[phase]") {
    PhaseManager pm;  // DISCOVERY
    
    CHECK(pm.isAt(HardwarePhase::DISCOVERY) == true);
    CHECK(pm.isAt(HardwarePhase::MAPPING) == false);
    
    (void)pm.advance(HardwarePhase::MAPPING);
    CHECK(pm.isAt(HardwarePhase::DISCOVERY) == false);
    CHECK(pm.isAt(HardwarePhase::MAPPING) == true);
}

