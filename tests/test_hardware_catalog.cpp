#include <catch2/catch_test_macros.hpp>
#include <climits>
#include <cmath>
#include <dynamichardware/dhdo/HardwareCatalog.h>
#include <filesystem>
#include <fstream>
#include <chrono>

using namespace dynamichardware::dhdo;

// ============================================================================
// HardwareCatalog save/load
// ============================================================================
TEST_CASE("HardwareCatalog saves and loads entries", "[catalog]")
{
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string path = "/tmp/test_catalog_" + std::to_string(now) + ".json";

    // Remove any leftover file
    std::filesystem::remove(path);

    HardwareCatalog catalog;
    catalog.load(path);  // Load non-existent file — should not crash
    REQUIRE(catalog.empty());

    // Add an entry (populate backend-specific data via variant)
    CatalogEntry entry;
    entry.channelType = "IMU_GyroX";
    entry.name        = "MPU6050[0x68] GyroX";
    entry.slaveName   = "MPU6050";
    entry.isOutput    = false;
    entry.backend     = BackendType::ETHERCAT;
    entry.backendData = EthercatBackendData{
        0x12345678, /* vendorId */
        0xABCDEF00, /* productCode */
        0x00010000, /* revisionNumber */
        1,          /* slavePos */
        0x10,       /* pdoIndex */
        1           /* pdoSubindex */
    };

    catalog.addEntry(std::move(entry));
    REQUIRE_FALSE(catalog.empty());
    // UUID is deterministic hash of backend data — verify it's non-empty and we can find by it.
    const auto& added = catalog.entries()[0];
    REQUIRE(!added.uuid.empty());
    REQUIRE(catalog.findByUuid(added.uuid) != nullptr);

    // Save
    REQUIRE(catalog.save(path) == true);

    // Verify file exists and is valid JSON
    REQUIRE(std::filesystem::exists(path));
    std::ifstream ifs(path);
    REQUIRE(ifs.good());

    // Load into a new catalog
    HardwareCatalog catalog2;
    REQUIRE(catalog2.load(path) == true);
    REQUIRE(catalog2.findByUuid(catalog.entries()[0].uuid) != nullptr);

    // Clean up
    std::filesystem::remove(path);
}

TEST_CASE("HardwareCatalog load of non-existent file is not fatal", "[catalog]")
{
    HardwareCatalog catalog;
    // Should return false but not throw
    auto result = catalog.load("/tmp/nonexistent_catalog_12345.json");
    CHECK(result == true);  // Non-existent file is not an error — returns true ("starting fresh")
    CHECK(catalog.empty());
}

TEST_CASE("HardwareCatalog entries vector is accessible", "[catalog]")
{
    HardwareCatalog catalog;

    for (int i = 0; i < 3; i++) {
        CatalogEntry entry;
        entry.channelType = "Test";
        entry.backend     = BackendType::ETHERCAT;
        // Give each entry distinct backend data so UUIDs differ
        entry.backendData = EthercatBackendData{
            0x12345678, /* vendorId */
            0xABCDEF00, /* productCode */
            0x00010000, /* revisionNumber */
            1,          /* slavePos */
            static_cast<uint16_t>(i),   /* pdoIndex - varies per entry */
            1           /* pdoSubindex */
        };
        catalog.addEntry(std::move(entry));
    }

    auto& entries = catalog.entries();
    CHECK(entries.size() == 3);
    // UUIDs are deterministic hashes of the key — verify they're non-empty and stable.
    CHECK(!entries[0].uuid.empty());
    CHECK(!entries[1].uuid.empty());
    CHECK(!entries[2].uuid.empty());
    // Each unique key produces a unique UUID.
    CHECK(entries[0].uuid != entries[1].uuid);
    CHECK(entries[1].uuid != entries[2].uuid);
}

TEST_CASE("HardwareCatalog lookup returns correct entry", "[catalog]")
{
    HardwareCatalog catalog;

    CatalogEntry entry;
    entry.channelType = "DigitalInput";
    entry.backend     = BackendType::ETHERCAT;
    entry.backendData = EthercatBackendData{
        0x12345678, /* vendorId */
        0xABCDEF00, /* productCode */
        0x00010000, /* revisionNumber */
        1,          /* slavePos */
        0x10,       /* pdoIndex */
        1           /* pdoSubindex */
    };
    catalog.addEntry(std::move(entry));

    const std::string expectedUuid = catalog.entries()[0].uuid;
    auto* found = catalog.findByUuid(expectedUuid);
    REQUIRE(found != nullptr);
    CHECK(!found->uuid.empty());
    CHECK(found->channelType == "DigitalInput");

    // Non-existent UUID should return null
    auto* notFound = catalog.findByUuid("non-existent-uuid-12345");
    CHECK(notFound == nullptr);
}

TEST_CASE("HardwareCatalog SimParams serializes correctly", "[catalog]")
{
    CatalogEntry::SimParams sim;
    sim.togglePeriodMs     = 100;
    sim.dutyCyclePercent   = 60.0f;
    sim.incrementPerCycle  = 10;
    sim.minValue           = -1000;
    sim.maxValue           = 1000;
    sim.amplitude          = 5.0f;
    sim.frequencyHz        = 2.0f;
    sim.pulseMs       = 50;
    sim.debounceMs    = 5;

    // Serialize to JSON
    auto json = nlohmann::json(sim);
    CHECK(json["togglePeriodMs"] == 100);
    CHECK(std::abs(json["dutyCyclePercent"].get<float>() - 60.0f) < 0.001f);
    CHECK(json["incrementPerCycle"] == 10);
    CHECK(json["minValue"] == -1000);
    CHECK(json["maxValue"] == 1000);
    CHECK(std::abs(json["amplitude"].get<float>() - 5.0f) < 0.001f);
    CHECK(std::abs(json["frequencyHz"].get<float>() - 2.0f) < 0.001f);
    CHECK(json["pulseMs"] == 50);
    CHECK(json["debounceMs"] == 5);

    // Deserialize
    auto sim2 = json.get<CatalogEntry::SimParams>();
    CHECK(sim2.togglePeriodMs == 100);
    CHECK(std::abs(sim2.dutyCyclePercent - 60.0f) < 0.001f);
    CHECK(sim2.incrementPerCycle == 10);
    CHECK(sim2.minValue == -1000);
    CHECK(sim2.maxValue == 1000);
    CHECK(std::abs(sim2.amplitude - 5.0f) < 0.001f);
    CHECK(std::abs(sim2.frequencyHz - 2.0f) < 0.001f);
    CHECK(sim2.pulseMs == 50);
}

TEST_CASE("HardwareCatalog simulated entry round-trip", "[catalog]")
{
    auto now2 = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string path = "/tmp/test_sim_catalog_" + std::to_string(now2) + ".json";
    std::filesystem::remove(path);

    HardwareCatalog catalog;
    catalog.load(path);

    CatalogEntry entry;
    entry.channelType = "Int32Input";
    entry.name        = "Sim-Encoder-A";
    entry.isSimulated = true;
    entry.backend     = BackendType::SIMULATED;
    entry.backendData = SimulatedBackendData{};  // no runtime state for simulated
    entry.sim.incrementPerCycle = 5;
    entry.sim.minValue          = INT64_MIN;
    entry.sim.maxValue          = INT64_MAX;

    catalog.addEntry(std::move(entry));
    REQUIRE(catalog.save(path) == true);

    // Reload and verify
    HardwareCatalog catalog2;
    REQUIRE(catalog2.load(path) == true);

    const std::string savedUuid = catalog.entries()[0].uuid;
    auto* found = catalog2.findByUuid(savedUuid);
    REQUIRE(found != nullptr);
    CHECK(found->isSimulated == true);
    CHECK(found->channelType == "Int32Input");
    CHECK(found->sim.incrementPerCycle == 5);
    CHECK(found->sim.minValue == static_cast<int64_t>(INT64_MIN));
    CHECK(found->sim.maxValue == static_cast<int64_t>(INT64_MAX));

    std::filesystem::remove(path);
}

// ============================================================================
// HardwareCatalog — write lockdown tests (Phase 2 / Issue B)
// endDiscovery() locks catalog; isWritable() guards mutation
// ============================================================================

TEST_CASE("HardwareCatalog is writable by default", "[catalog][lock]") {
    HardwareCatalog catalog;
    CHECK(catalog.isWritable() == true);
}

TEST_CASE("HardwareCatalog endDiscovery locks against writes", "[catalog][lock]") {
    HardwareCatalog catalog;
    CHECK(catalog.isWritable() == true);
    
    catalog.endDiscovery();
    CHECK(catalog.isWritable() == false);
}

TEST_CASE("HardwareCatalog beginDiscovery clears write lock", "[catalog][lock]") {
    HardwareCatalog catalog;
    catalog.endDiscovery();
    CHECK(catalog.isWritable() == false);
    
    catalog.beginDiscovery();
    CHECK(catalog.isWritable() == true);
}

TEST_CASE("HardwareCatalog addEntry respects write lock", "[catalog][lock]") {
    HardwareCatalog catalog;
    
    // Add entry while writable — should succeed
    CatalogEntry entry1;
    entry1.channelType = "BoolInput";
    entry1.name        = "TestPin1";
    entry1.backend     = BackendType::GPIO;
    catalog.addEntry(std::move(entry1));
    REQUIRE(catalog.entries().size() == 1);
    
    // Lock catalog
    catalog.endDiscovery();
    CHECK(catalog.isWritable() == false);
    
    // Try to add another entry while locked — should be silently rejected
    CatalogEntry entry2;
    entry2.channelType = "FloatOutput";
    entry2.name        = "TestPin2";
    entry2.backend     = BackendType::GPIO;
    catalog.addEntry(std::move(entry2));
    
    // Entry count unchanged (locked additions are no-ops)
    CHECK(catalog.entries().size() == 1);
}
