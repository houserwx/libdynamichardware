#include <catch2/catch_test_macros.hpp>
#include <dynamichardware/config/BackendRegistry.h>

using namespace dynamichardware;
using namespace dynamichardware::config;

// ============================================================================
// BackendRegistration — global backend module registration system tests
// ============================================================================

TEST_CASE("BackendRegistry getAll returns valid list", "[backend-registry]") {
    auto all = BackendRegistry::getAll();
    // Note: static initializers may have already registered backends if linked in.
    // At minimum this verifies getAll() doesn't crash.
    (void)all;  // Suppress unused variable warning
}

static int g_testCounter = 0;
TEST_CASE("BackendRegistry registerBackend adds a new entry", "[backend-registry]") {
    // Register a test-only backend that creates empty scanner+adapter pair
    std::string testName = "TestOnlyBackend_" + std::to_string(++g_testCounter);
    
    BackendRegistry::registerBackend(testName, [] () -> 
        std::pair<std::unique_ptr<dhdo::IBackendScanner>, 
                  std::unique_ptr<dhdo::IRuntimeAdapter>> {
        
        return {nullptr, nullptr};  // Empty for test — we just verify registration works
    });
    
    auto creatorPtr = BackendRegistry::getCreator(testName);
    REQUIRE(creatorPtr != nullptr);
}

TEST_CASE("BackendRegistry getCreator returns null for unknown name", "[backend-registry]") {
    auto creatorPtr = BackendRegistry::getCreator("__NonExistent_Backend_Name__");
    CHECK(creatorPtr == nullptr);
}

TEST_CASE("BackendRegistry createAll produces correct count", "[backend-registry]") {
    size_t beforeCount = BackendRegistry::getAll().size();
    
    // Register a temporary backend
    std::string tempName = "TempCreateAllTest_" + std::to_string(++g_testCounter);
    int createCallCount = 0;
    
    BackendRegistry::registerBackend(tempName, [&createCallCount]() -> 
        std::pair<std::unique_ptr<dhdo::IBackendScanner>, 
                  std::unique_ptr<dhdo::IRuntimeAdapter>> {
        ++createCallCount;
        return {nullptr, nullptr};
    });
    
    auto pairs = BackendRegistry::createAll();
    
    // Should have called all registered creators (including the one we just added)
    CHECK(createCallCount >= 1);
    CHECK(pairs.size() >= beforeCount + 1);
}

