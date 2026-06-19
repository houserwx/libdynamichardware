#include <catch2/catch_test_macros.hpp>
#include <dynamichardware/pdo/PDO.h>
#include <cstring>
#include <cmath>

using namespace dynamichardware::pdo;

// ============================================================================
// PDO freeze — image pointer rebasing
// ============================================================================
TEST_CASE("PDO freeze re-bases entry image pointers", "[pdo]")
{
    PDO pdo;
    pdo.image.resize(32);

    PDOEntry entry1;
    entry1.type       = EntryType::BoolInput;
    entry1.byteOffset = 4;
    entry1.image      = pdo.image.data() + 4;

    PDOEntry entry2;
    entry2.type       = EntryType::Int16Input;
    entry2.byteOffset = 8;
    entry2.image      = pdo.image.data() + 8;

    pdo.entries.push_back(std::move(entry1));
    pdo.entries.push_back(std::move(entry2));

    pdo.freeze();

    // After freeze, entry image pointers should point into the frozen image
    CHECK(pdo.entries[0].image >= pdo.image.data());
    CHECK(pdo.entries[0].image < pdo.image.data() + pdo.image.size());
    CHECK(pdo.entries[1].image >= pdo.image.data());
    CHECK(pdo.entries[1].image < pdo.image.data() + pdo.image.size());
}

// ============================================================================
// PDO freeze — entry offset preservation
// ============================================================================
TEST_CASE("PDO freeze preserves byte offsets", "[pdo]")
{
    PDO pdo;
    pdo.image.resize(32);

    PDOEntry entry;
    entry.type       = EntryType::FloatInput;
    entry.byteOffset = 12;
    entry.bitLength  = 32;
    entry.image      = pdo.image.data() + 12;

    pdo.entries.push_back(std::move(entry));
    pdo.freeze();

    CHECK(pdo.entries[0].byteOffset == 12);
    CHECK(pdo.entries[0].bitLength == 32);

    // After freeze, entry.image = pdo.image.data() + byteOffset.
    // read() adds byteOffset again, so write at image + byteOffset.
    float val = 3.14f;
    std::memcpy(pdo.entries[0].image + entry.byteOffset, &val, sizeof(val));
    pdo.entries[0].read();
    CHECK(std::fabs(pdo.entries[0].getFloat() - 3.14f) < 0.0001f);
}

// ============================================================================
// PDO with empty image (backend-owned, like EtherCAT) — freeze is a no-op for image pointers
// ============================================================================
TEST_CASE("PDO freeze with empty image leaves entry pointers untouched", "[pdo]")
{
    PDO pdo;
    // No image — simulating backend-owned memory

    // Simulate a pointer into "backend-owned" memory
    uint8_t externalBuf[16] = {0};

    PDOEntry entry;
    entry.type       = EntryType::BoolInput;
    entry.byteOffset = 0;
    entry.bitOffset  = 2;
    entry.bitLength  = 1;
    entry.image      = externalBuf;  // Points to external buffer directly

    pdo.entries.push_back(std::move(entry));
    pdo.freeze();

    // Image pointer should still point to the external buffer
    CHECK(pdo.entries[0].image == externalBuf);

    // Verify it works
    externalBuf[0] = (1 << 2);
    pdo.entries[0].read();
    CHECK(pdo.entries[0].getBool() == true);
}

// ============================================================================
// PDO freeze — multiple entries
// ============================================================================
TEST_CASE("PDO freeze with multiple entries", "[pdo]")
{
    PDO pdo;
    pdo.image.resize(64);

    // Create entries at various offsets
    std::vector<uint32_t> offsets = {0, 4, 8, 12, 20, 32, 48};
    std::vector<EntryType> types = {
        EntryType::BoolInput,
        EntryType::Int32Input,
        EntryType::Int16Input,
        EntryType::FloatInput,
        EntryType::BoolOutput,
        EntryType::FloatOutput,
        EntryType::Int16Output
    };

    for (size_t i = 0; i < offsets.size(); i++) {
        PDOEntry entry;
        entry.type       = types[i];
        entry.byteOffset = offsets[i];
        entry.bitLength  = 32;
        entry.image      = pdo.image.data() + offsets[i];
        pdo.entries.push_back(std::move(entry));
    }

    CHECK(pdo.entries.size() == offsets.size());
    pdo.freeze();

    // All entries should still be valid and point within the image
    for (size_t i = 0; i < offsets.size(); i++) {
        CHECK(pdo.entries[i].image >= pdo.image.data());
        CHECK(pdo.entries[i].image < pdo.image.data() + pdo.image.size());
        CHECK(pdo.entries[i].byteOffset == offsets[i]);
    }
}
