#include <catch2/catch_test_macros.hpp>
#include <dynamichardware/dhdo/DHDO.h>
#include <cstring>
#include <cmath>

using namespace dynamichardware::dhdo;

// ============================================================================
// DHDO freeze — image pointer rebasing
// ============================================================================
TEST_CASE("DHDO freeze re-bases entry image pointers", "[dhdo]")
{
    DHDO dhdo;
    dhdo.image.resize(32);

    DHDOEntry entry1;
    entry1.type       = EntryType::BoolInput;
    entry1.byteOffset = 4;
    entry1.image      = dhdo.image.data() + 4;

    DHDOEntry entry2;
    entry2.type       = EntryType::Int16Input;
    entry2.byteOffset = 8;
    entry2.image      = dhdo.image.data() + 8;

    dhdo.entries.push_back(std::move(entry1));
    dhdo.entries.push_back(std::move(entry2));

    dhdo.freeze();

    // After freeze, entry image pointers should point into the frozen image
    CHECK(dhdo.entries[0].image >= dhdo.image.data());
    CHECK(dhdo.entries[0].image < dhdo.image.data() + dhdo.image.size());
    CHECK(dhdo.entries[1].image >= dhdo.image.data());
    CHECK(dhdo.entries[1].image < dhdo.image.data() + dhdo.image.size());
}

// ============================================================================
// DHDO freeze — entry offset preservation
// ============================================================================
TEST_CASE("DHDO freeze preserves byte offsets", "[dhdo]")
{
    DHDO dhdo;
    dhdo.image.resize(32);

    DHDOEntry entry;
    entry.type       = EntryType::FloatInput;
    entry.byteOffset = 12;
    entry.bitLength  = 32;
    entry.image      = dhdo.image.data() + 12;

    dhdo.entries.push_back(std::move(entry));
    dhdo.freeze();

    CHECK(dhdo.entries[0].byteOffset == 12);
    CHECK(dhdo.entries[0].bitLength == 32);

    // After freeze, entry.image = dhdo.image.data() + byteOffset — already positioned at target byte.
    // read() reads directly from *image without adding any offset.
    float val = 3.14f;
    std::memcpy(dhdo.entries[0].image, &val, sizeof(val));
    dhdo.entries[0].read();
    CHECK(std::fabs(dhdo.entries[0].getFloat() - 3.14f) < 0.0001f);
}

// ============================================================================
// DHDO with empty image (backend-owned, like EtherCAT) — freeze is a no-op for image pointers
// ============================================================================
TEST_CASE("DHDO freeze with empty image leaves entry pointers untouched", "[dhdo]")
{
    DHDO dhdo;
    // No image — simulating backend-owned memory

    // Simulate a pointer into "backend-owned" memory
    uint8_t externalBuf[16] = {0};

    DHDOEntry entry;
    entry.type       = EntryType::BoolInput;
    entry.byteOffset = 0;
    entry.bitOffset  = 2;
    entry.bitLength  = 1;
    entry.image      = externalBuf;  // Points to external buffer directly

    dhdo.entries.push_back(std::move(entry));
    dhdo.freeze();

    // Image pointer should still point to the external buffer
    CHECK(dhdo.entries[0].image == externalBuf);

    // Verify it works
    externalBuf[0] = (1 << 2);
    dhdo.entries[0].read();
    CHECK(dhdo.entries[0].getBool() == true);
}

// ============================================================================
// DHDO freeze — multiple entries
// ============================================================================
TEST_CASE("DHDO freeze with multiple entries", "[dhdo]")
{
    DHDO dhdo;
    dhdo.image.resize(64);

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
        DHDOEntry entry;
        entry.type       = types[i];
        entry.byteOffset = offsets[i];
        entry.bitLength  = 32;
        entry.image      = dhdo.image.data() + offsets[i];
        dhdo.entries.push_back(std::move(entry));
    }

    CHECK(dhdo.entries.size() == offsets.size());
    dhdo.freeze();

    // All entries should still be valid and point within the image
    for (size_t i = 0; i < offsets.size(); i++) {
        CHECK(dhdo.entries[i].image >= dhdo.image.data());
        CHECK(dhdo.entries[i].image < dhdo.image.data() + dhdo.image.size());
        CHECK(dhdo.entries[i].byteOffset == offsets[i]);
    }
}
