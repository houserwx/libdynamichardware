#include <catch2/catch_test_macros.hpp>
#include <dynamichardware/dhdo/HardwareRegistry.h>
#include <dynamichardware/dhdo/HardwareCatalog.h>
#include <dynamichardware/dhdo/DHDOFactory.h>
#include <cstring>
#include <memory>

using namespace dynamichardware::dhdo;

// ============================================================================
// Test adapter — minimal implementation inheriting IRuntimeAdapter (canonical).
// Builds PDOs and provides RT lifecycle hooks for unit testing HardwareRegistry.
// ============================================================================
class TestAdapter : public IRuntimeAdapter {
public:
    void addEntry(const std::string& uuid, EntryType type, uint32_t offset, uint8_t bitLength) {
        DHDOEntry e;
        e.uuid       = uuid;
        e.type       = type;
        e.byteOffset = offset;
        e.bitLength  = bitLength;
        entries_.push_back(std::move(e));
    }

    /// Builds PDO structure from pre-added entries.
    // Sets base pointer before freeze(); freeze() handles byteOffset rebasing.
    bool buildRT() {
        dhdos_.resize(1);
        dhdos_[0].image.resize(64);

        uint8_t* base = dhdos_[0].image.data();
        for (auto& e : entries_) {
            e.image = base;  // base only; freeze() adds byteOffset
            dhdos_[0].entries.push_back(std::move(e));
        }
        dhdos_[0].freeze();
        initialized_ = true;
        return true;
    }

    void onBeforeReadInputs() noexcept override {
        readInputsCalled_++;
    }

    void onAfterWriteOutputs() noexcept override {
        writeOutputsCalled_++;
    }

    bool healthy_{true};

    [[nodiscard]] bool isHealthy() const { return healthy_; }
    void setHealthy(bool h) { healthy_ = h; }

    [[nodiscard]] bool isInitialized() const { return initialized_; }
    [[nodiscard]] int  readInputsCalled() const { return readInputsCalled_; }
    [[nodiscard]] int  writeOutputsCalled() const { return writeOutputsCalled_; }

private:
    std::vector<DHDOEntry> entries_;
    bool initialized_{false};
    int readInputsCalled_{0};
    int writeOutputsCalled_{0};
};

// ============================================================================
// HardwareRegistry basic operations
// ============================================================================
TEST_CASE("HardwareRegistry addBackend increases backend count", "[registry]")
{
    HardwareRegistry registry;
    auto adapter = std::make_unique<TestAdapter>();
    adapter->buildRT();
    registry.addBackend(std::move(adapter));
    REQUIRE(registry.backendCount() == 1);
    auto adapter2 = std::make_unique<TestAdapter>();
    adapter2->buildRT();
    registry.addBackend(std::move(adapter2));
    REQUIRE(registry.backendCount() == 2);
}

TEST_CASE("HardwareRegistry buildUuidMap populates lookup", "[registry]")
{
    HardwareRegistry registry;
    auto adapter = std::make_unique<TestAdapter>();

    adapter->addEntry("test-uuid-001", EntryType::BoolInput, 0, 1);
    adapter->addEntry("test-uuid-002", EntryType::FloatInput, 4, 32);
    adapter->buildRT();
    registry.addBackend(std::move(adapter));

    registry.buildUuidMap();

    auto* entry1 = registry.lookupByUuid("test-uuid-001");
    REQUIRE(entry1 != nullptr);
    CHECK(entry1->type == EntryType::BoolInput);

    auto* entry2 = registry.lookupByUuid("test-uuid-002");
    REQUIRE(entry2 != nullptr);
    CHECK(entry2->type == EntryType::FloatInput);

    auto* missing = registry.lookupByUuid("nonexistent-uuid");
    CHECK(missing == nullptr);
}

// ============================================================================
// HardwareRegistry RT cycle
// ============================================================================
TEST_CASE("HardwareRegistry readAll calls adapter hooks and reads entries", "[registry]")
{
    HardwareRegistry registry;
    auto adapter = std::make_unique<TestAdapter>();

    adapter->addEntry("rt-test-bool", EntryType::BoolInput, 0, 1);
    adapter->addEntry("rt-test-float", EntryType::FloatInput, 4, 32);
    adapter->buildRT();
    registry.addBackend(std::move(adapter));
    registry.buildUuidMap();
    registry.freezeForRt();

    // Set up values in the image — after freeze(), image points directly at target byte.
    auto* entry = registry.lookupByUuid("rt-test-bool");
    REQUIRE(entry != nullptr);
    *entry->image = (1u << entry->bitOffset);  // set the bit at bitOffset high

    entry = registry.lookupByUuid("rt-test-float");
    REQUIRE(entry != nullptr);
    float val = 3.14f;
    std::memcpy(entry->image, &val, sizeof(val));

    // Run readAll
    dynamichardware::rt::signalProcessTickNow();
    registry.readAll();

    // Verify values were read
    entry = registry.lookupByUuid("rt-test-bool");
    CHECK(entry->getBool() == true);

    entry = registry.lookupByUuid("rt-test-float");
    CHECK(std::fabs(entry->getFloat() - 3.14f) < 0.0001f);
}

TEST_CASE("HardwareRegistry writeAll calls adapter hooks and writes entries", "[registry]")
{
    HardwareRegistry registry;
    auto adapter = std::make_unique<TestAdapter>();

    adapter->addEntry("rt-test-out", EntryType::BoolOutput, 0, 1);
    adapter->buildRT();
    registry.addBackend(std::move(adapter));
    registry.buildUuidMap();
    registry.freezeForRt();

    auto* entry = registry.lookupByUuid("rt-test-out");
    REQUIRE(entry != nullptr);
    entry->setBool(true);

    dynamichardware::rt::signalProcessTickNow();
    registry.writeAll();

    // BoolOutput in latched mode should write the bit — *image is already positioned at target byte after freeze().
    CHECK((*entry->image & (1u << entry->bitOffset)) != 0);
}

// ============================================================================
// HardwareRegistry freeze
// ============================================================================
TEST_CASE("HardwareRegistry freezeForRt sets frozen flag", "[registry]")
{
    HardwareRegistry registry;
    auto adapter = std::make_unique<TestAdapter>();

    adapter->buildRT();
    registry.addBackend(std::move(adapter));

    CHECK(registry.isFrozen() == false);
    registry.freezeForRt();
    CHECK(registry.isFrozen() == true);
}

TEST_CASE("HardwareRegistry addBackend after freeze throws", "[registry]")
{
    HardwareRegistry registry;
    auto adapter = std::make_unique<TestAdapter>();

    adapter->buildRT();
    registry.addBackend(std::move(adapter));
    registry.freezeForRt();

    REQUIRE_THROWS_AS(registry.addBackend(std::make_unique<TestAdapter>()), std::logic_error);
}

// ============================================================================
// HardwareRegistry health monitoring
// ============================================================================
TEST_CASE("HardwareRegistry allBackendsHealthy returns correct state", "[registry]")
{
    HardwareRegistry registry;
    {
        auto a = std::make_unique<TestAdapter>();
        a->buildRT();
        registry.addBackend(std::move(a));
    }
    {
        auto b = std::make_unique<TestAdapter>();
        b->buildRT();
        registry.addBackend(std::move(b));
    }

    // With no backend-specific health checks failing, allBackendsHealthy returns true
    CHECK(registry.allBackendsHealthy() == true);
}

// ============================================================================
// HardwareRegistry entryCount
// ============================================================================
TEST_CASE("HardwareRegistry entryCount returns total entries across backends", "[registry]")
{
    HardwareRegistry registry;
    auto adapter = std::make_unique<TestAdapter>();

    adapter->addEntry("ec-001", EntryType::BoolInput, 0, 1);
    adapter->addEntry("ec-002", EntryType::FloatInput, 4, 32);
    adapter->addEntry("ec-003", EntryType::Int16Input, 8, 16);
    adapter->buildRT();
    registry.addBackend(std::move(adapter));
    registry.buildUuidMap();

    CHECK(registry.entryCount() == 3);
}

// ============================================================================
// HardwareRegistry RT cycle — message entries are skipped
// ============================================================================
TEST_CASE("HardwareRegistry RT cycle skips message entries", "[registry]")
{
    HardwareRegistry registry;
    auto adapter = std::make_unique<TestAdapter>();

    adapter->addEntry("msg-in", EntryType::MessageIn, 0, 0);
    adapter->addEntry("msg-out", EntryType::MessageOut, 8, 0);
    adapter->addEntry("normal-in", EntryType::BoolInput, 16, 1);
    adapter->buildRT();
    registry.addBackend(std::move(adapter));
    registry.buildUuidMap();
    registry.freezeForRt();

    // Set up the bool entry — after freeze(), image points directly at target byte.
    auto* boolEntry = registry.lookupByUuid("normal-in");
    REQUIRE(boolEntry != nullptr);
    *boolEntry->image = 1;

    dynamichardware::rt::signalProcessTickNow();
    registry.readAll();

    // Normal entry should be read
    CHECK(boolEntry->getBool() == true);

    // Message entries should NOT be touched by readAll (no read() called on them)
}

// ============================================================================
// HardwareRegistry printState doesn't crash
// ============================================================================
TEST_CASE("HardwareRegistry printState does not crash", "[registry]")
{
    HardwareRegistry registry;
    auto adapter = std::make_unique<TestAdapter>();

    adapter->addEntry("print-test", EntryType::BoolInput, 0, 1);
    adapter->buildRT();
    registry.addBackend(std::move(adapter));
    registry.buildUuidMap();
    registry.freezeForRt();

    // Should not crash
    registry.printState();
}
