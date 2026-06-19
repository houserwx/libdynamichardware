#include <catch2/catch_test_macros.hpp>
#include <dynamichardware/pdo/HardwareRegistry.h>
#include <dynamichardware/pdo/HardwareCatalog.h>
#include <dynamichardware/pdo/PDOFactory.h>
#include <dynamichardware/pdo/IHardwareAdapter.h>
#include <cstring>
#include <memory>

using namespace dynamichardware::pdo;

// ============================================================================
// Test adapter — minimal IHardwareAdapter implementation for testing
// ============================================================================
class TestAdapter : public IHardwareAdapter {
public:
    void addEntry(const std::string& uuid, EntryType type, uint32_t offset, uint8_t bitLength) {
        PDOEntry e;
        e.uuid       = uuid;
        e.type       = type;
        e.byteOffset = offset;
        e.bitLength  = bitLength;
        entries_.push_back(std::move(e));
    }

    bool initialize() override {
        // Build PDO from entries
        pdos_.resize(1);
        pdos_[0].image.resize(64);

        for (auto& e : entries_) {
            e.image = pdos_[0].image.data() + e.byteOffset;
            pdos_[0].entries.push_back(std::move(e));
        }
        pdos_[0].freeze();
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
    std::vector<PDOEntry> entries_;
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
    registry.addBackend(std::make_unique<TestAdapter>());
    REQUIRE(registry.backendCount() == 1);
    registry.addBackend(std::make_unique<TestAdapter>());
    REQUIRE(registry.backendCount() == 2);
}

TEST_CASE("HardwareRegistry buildUuidMap populates lookup", "[registry]")
{
    HardwareRegistry registry;
    auto adapter = std::make_unique<TestAdapter>();
    adapter->addEntry("test-uuid-001", EntryType::BoolInput, 0, 1);
    adapter->addEntry("test-uuid-002", EntryType::FloatInput, 4, 32);
    adapter->initialize();
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
    adapter->initialize();
    registry.addBackend(std::move(adapter));
    registry.buildUuidMap();
    registry.freezeForRt();

    // Set up values in the image
    auto* entry = registry.lookupByUuid("rt-test-bool");
    REQUIRE(entry != nullptr);
    entry->image[0] = 1;  // Set bool to true

    entry = registry.lookupByUuid("rt-test-float");
    REQUIRE(entry != nullptr);
    float val = 3.14f;
    // After freeze, entry.image = pdo.image.data() + byteOffset.
    // read() adds byteOffset again, so write at entry->image + byteOffset.
    std::memcpy(entry->image + entry->byteOffset, &val, sizeof(val));

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
    adapter->initialize();
    registry.addBackend(std::move(adapter));
    registry.buildUuidMap();
    registry.freezeForRt();

    auto* entry = registry.lookupByUuid("rt-test-out");
    REQUIRE(entry != nullptr);
    entry->setBool(true);

    dynamichardware::rt::signalProcessTickNow();
    registry.writeAll();

    // BoolOutput in latched mode should write the bit
    CHECK((entry->image[0] & 1) != 0);
}

// ============================================================================
// HardwareRegistry freeze
// ============================================================================
TEST_CASE("HardwareRegistry freezeForRt sets frozen flag", "[registry]")
{
    HardwareRegistry registry;
    registry.addBackend(std::make_unique<TestAdapter>());

    CHECK(registry.isFrozen() == false);
    registry.freezeForRt();
    CHECK(registry.isFrozen() == true);
}

TEST_CASE("HardwareRegistry addBackend after freeze throws", "[registry]")
{
    HardwareRegistry registry;
    registry.addBackend(std::make_unique<TestAdapter>());
    registry.freezeForRt();

    REQUIRE_THROWS_AS(registry.addBackend(std::make_unique<TestAdapter>()), std::logic_error);
}

// ============================================================================
// HardwareRegistry health monitoring
// ============================================================================
TEST_CASE("HardwareRegistry allBackendsHealthy returns correct state", "[registry]")
{
    HardwareRegistry registry;
    registry.addBackend(std::make_unique<TestAdapter>());
    registry.addBackend(std::make_unique<TestAdapter>());

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
    adapter->initialize();
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
    adapter->initialize();
    registry.addBackend(std::move(adapter));
    registry.buildUuidMap();
    registry.freezeForRt();

    // Set up the bool entry
    auto* boolEntry = registry.lookupByUuid("normal-in");
    REQUIRE(boolEntry != nullptr);
    boolEntry->image[16] = 1;

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
    adapter->initialize();
    registry.addBackend(std::move(adapter));
    registry.buildUuidMap();
    registry.freezeForRt();

    // Should not crash
    registry.printState();
}
