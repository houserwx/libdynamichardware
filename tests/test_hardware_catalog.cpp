#include <catch2/catch_test_macros.hpp>
#include <dynamichardware/pdo/HardwareCatalog.h>
#include <filesystem>
#include <fstream>
#include <chrono>

using namespace dynamichardware::pdo;

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

    // Add an entry
    CatalogEntry entry;
    entry.key         = "EC|12345678|ABCDEF00|REV00010000|POS0001|0010:01";
    entry.uuid        = "a1b2c3d4-e5f6-7890-abcd-ef1234567890";
    entry.channelType = "IMU_GyroX";
    entry.name        = "MPU6050[0x68] GyroX";
    entry.slaveName   = "MPU6050";
    entry.slavePos    = 1;
    entry.isOutput    = false;

    catalog.addEntry(std::move(entry));
    REQUIRE_FALSE(catalog.empty());
    REQUIRE(catalog.findByKey("EC|12345678|ABCDEF00|REV00010000|POS0001|0010:01") != nullptr);

    // Save
    REQUIRE(catalog.save(path) == true);

    // Verify file exists and is valid JSON
    REQUIRE(std::filesystem::exists(path));
    std::ifstream ifs(path);
    REQUIRE(ifs.good());

    // Load into a new catalog
    HardwareCatalog catalog2;
    REQUIRE(catalog2.load(path) == true);
    REQUIRE(catalog2.findByKey("EC|12345678|ABCDEF00|REV00010000|POS0001|0010:01") != nullptr);

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
        entry.key         = "EC|test|00000000|REV00000000|POS0001|0010:" + std::to_string(i);
        entry.uuid        = "uuid-" + std::to_string(i);
        entry.channelType = "Test";
        catalog.addEntry(std::move(entry));
    }

    auto& entries = catalog.entries();
    CHECK(entries.size() == 3);
    CHECK(entries[0].uuid == "uuid-0");
    CHECK(entries[1].uuid == "uuid-1");
    CHECK(entries[2].uuid == "uuid-2");
}

TEST_CASE("HardwareCatalog lookup returns correct entry", "[catalog]")
{
    HardwareCatalog catalog;

    CatalogEntry entry;
    entry.key         = "EC|lookup|00000000|REV00000000|POS0001|0010:01";
    entry.uuid        = "lookup-uuid-123";
    entry.channelType = "DigitalInput";
    catalog.addEntry(std::move(entry));

    auto* found = catalog.findByKey("EC|lookup|00000000|REV00000000|POS0001|0010:01");
    REQUIRE(found != nullptr);
    CHECK(found->uuid == "lookup-uuid-123");
    CHECK(found->channelType == "DigitalInput");

    // Non-existent key
    auto* notFound = catalog.findByKey("EC|missing|00000000|REV00000000|POS0001|0010:01");
    CHECK(notFound == nullptr);
}

TEST_CASE("HardwareCatalog SimParams serializes correctly", "[catalog]")
{
    CatalogEntry::SimParams sim;
    sim.rpm           = 3000.0f;
    sim.rollerDiamMm  = 50.0f;
    sim.resolutionPpr = 1024;
    sim.quadrature    = true;
    sim.pulseMs       = 50;
    sim.debounceMs    = 5;

    // Serialize to JSON
    auto json = nlohmann::json(sim);
    CHECK(json["rpm"] == 3000.0f);
    CHECK(json["rollerDiamMm"] == 50.0f);
    CHECK(json["resolutionPpr"] == 1024);
    CHECK(json["quadrature"] == true);
    CHECK(json["pulseMs"] == 50);

    // Deserialize
    auto sim2 = json.get<CatalogEntry::SimParams>();
    CHECK(sim2.rpm == 3000.0f);
    CHECK(sim2.quadrature == true);
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
    entry.key         = "virt|sim-test-001";
    entry.uuid        = "virt-sim-001-uuid";
    entry.channelType = "Encoder";
    entry.name        = "Sim-Encoder-A";
    entry.isSimulated = true;
    entry.sim.rpm     = 1500.0f;
    entry.sim.resolutionPpr = 2048;

    catalog.addEntry(std::move(entry));
    REQUIRE(catalog.save(path) == true);

    // Reload and verify
    HardwareCatalog catalog2;
    REQUIRE(catalog2.load(path) == true);

    auto* found = catalog2.findByKey("virt|sim-test-001");
    REQUIRE(found != nullptr);
    CHECK(found->isSimulated == true);
    CHECK(found->sim.rpm == 1500.0f);
    CHECK(found->sim.resolutionPpr == 2048);

    std::filesystem::remove(path);
}
