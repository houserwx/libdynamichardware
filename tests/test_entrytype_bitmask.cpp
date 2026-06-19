#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <dynamichardware/pdo/PDO.h>
#include <set>

using namespace dynamichardware::pdo;

// ============================================================================
// EntryType bitmask field isolation — verify bit fields don't overlap
// ============================================================================
TEST_CASE("EntryFlag bit fields are non-overlapping", "[entrytype]")
{
    // Direction bits (0-1) don't overlap with anything else
    REQUIRE((DIR_INPUT & ~0x03) == 0);
    REQUIRE((DIR_OUTPUT & ~0x03) == 0);
    REQUIRE(DIR_INPUT != DIR_OUTPUT);
    REQUIRE((DIR_INPUT & DIR_OUTPUT) == 0);

    // Signedness (bit 2) doesn't overlap with direction
    REQUIRE((SIGNED & 0x03) == 0);

    // Base type (bits 3-4) doesn't overlap with direction or signedness
    REQUIRE((BASE_INT & 0x07) == 0);
    REQUIRE((BASE_FLOAT & 0x07) == 0);
    REQUIRE((BASE_MSG & 0x07) == 0);

    // Size (bits 5-6) doesn't overlap with anything below
    REQUIRE((SZ_8 & 0x1F) == 0);
    REQUIRE((SZ_16 & 0x1F) == 0);
    REQUIRE((SZ_32 & 0x1F) == 0);
}

// ============================================================================
// Convenience constants have unique values
// ============================================================================
TEST_CASE("EntryType convenience constants are unique", "[entrytype]")
{
    std::set<uint8_t> values{
        uint8_t(EntryType::BoolInput),
        uint8_t(EntryType::BoolOutput),
        uint8_t(EntryType::Int8Input),
        uint8_t(EntryType::Int16Input),
        uint8_t(EntryType::Int32Input),
        uint8_t(EntryType::Int8Output),
        uint8_t(EntryType::Int16Output),
        uint8_t(EntryType::Int32Output),
        uint8_t(EntryType::FloatInput),
        uint8_t(EntryType::FloatOutput),
        uint8_t(EntryType::MessageIn),
        uint8_t(EntryType::MessageOut),
    };
    // All 12 should be unique
    CHECK(values.size() == 12);
}

// ============================================================================
// entryIsInput extractor
// ============================================================================
TEST_CASE("entryIsInput returns correct values", "[entrytype]")
{
    REQUIRE(entryIsInput(EntryType::BoolInput));
    REQUIRE(entryIsInput(EntryType::Int8Input));
    REQUIRE(entryIsInput(EntryType::Int16Input));
    REQUIRE(entryIsInput(EntryType::Int32Input));
    REQUIRE(entryIsInput(EntryType::FloatInput));
    REQUIRE(entryIsInput(EntryType::MessageIn));

    REQUIRE_FALSE(entryIsInput(EntryType::BoolOutput));
    REQUIRE_FALSE(entryIsInput(EntryType::Int16Output));
    REQUIRE_FALSE(entryIsInput(EntryType::FloatOutput));
    REQUIRE_FALSE(entryIsInput(EntryType::MessageOut));
}

// ============================================================================
// entryIsOutput extractor
// ============================================================================
TEST_CASE("entryIsOutput returns correct values", "[entrytype]")
{
    REQUIRE(entryIsOutput(EntryType::BoolOutput));
    REQUIRE(entryIsOutput(EntryType::Int16Output));
    REQUIRE(entryIsOutput(EntryType::FloatOutput));
    REQUIRE(entryIsOutput(EntryType::MessageOut));

    REQUIRE_FALSE(entryIsOutput(EntryType::BoolInput));
    REQUIRE_FALSE(entryIsOutput(EntryType::Int16Input));
    REQUIRE_FALSE(entryIsOutput(EntryType::FloatInput));
    REQUIRE_FALSE(entryIsOutput(EntryType::MessageIn));
}

// ============================================================================
// entryIsMessage extractor
// ============================================================================
TEST_CASE("entryIsMessage returns correct values", "[entrytype]")
{
    REQUIRE(entryIsMessage(EntryType::MessageIn));
    REQUIRE(entryIsMessage(EntryType::MessageOut));

    REQUIRE_FALSE(entryIsMessage(EntryType::BoolInput));
    REQUIRE_FALSE(entryIsMessage(EntryType::FloatInput));
    REQUIRE_FALSE(entryIsMessage(EntryType::Int16Input));
    REQUIRE_FALSE(entryIsMessage(EntryType::BoolOutput));
}

// ============================================================================
// entryBitSize extractor
// ============================================================================
TEST_CASE("entryBitSize returns correct size field", "[entrytype]")
{
    REQUIRE(entryBitSize(EntryType::BoolInput) == SZ_1);
    REQUIRE(entryBitSize(EntryType::BoolOutput) == SZ_1);
    REQUIRE(entryBitSize(EntryType::Int8Input) == SZ_8);
    REQUIRE(entryBitSize(EntryType::Int16Input) == SZ_16);
    REQUIRE(entryBitSize(EntryType::Int32Input) == SZ_32);
    REQUIRE(entryBitSize(EntryType::FloatInput) == SZ_32);
    REQUIRE(entryBitSize(EntryType::FloatOutput) == SZ_32);
}

// ============================================================================
// entryIsSigned extractor
// ============================================================================
TEST_CASE("entryIsSigned returns correct values", "[entrytype]")
{
    // Convenience constants for integers are signed by default
    REQUIRE(entryIsSigned(EntryType::Int8Input));
    REQUIRE(entryIsSigned(EntryType::Int16Input));
    REQUIRE(entryIsSigned(EntryType::Int32Input));
    REQUIRE(entryIsSigned(EntryType::Int8Output));
    REQUIRE(entryIsSigned(EntryType::Int16Output));
    REQUIRE(entryIsSigned(EntryType::Int32Output));

    // Float is always signed (SIGNED bit not set in FloatInput convenience const)
    REQUIRE_FALSE(entryIsSigned(EntryType::FloatInput));
    REQUIRE_FALSE(entryIsSigned(EntryType::FloatOutput));

    // Bool has no signedness
    REQUIRE_FALSE(entryIsSigned(EntryType::BoolInput));
    REQUIRE_FALSE(entryIsSigned(EntryType::BoolOutput));
}

// ============================================================================
// Bitmask composition — unsigned integer types via composition
// ============================================================================
TEST_CASE("Composed unsigned integer types work correctly", "[entrytype]")
{
    uint8_t ui8in = DIR_INPUT | BASE_INT | SZ_8;
    REQUIRE_FALSE(entryIsSigned(ui8in));
    REQUIRE(entryIsInput(ui8in));
    REQUIRE(entryBitSize(ui8in) == SZ_8);

    uint8_t ui16in = DIR_INPUT | BASE_INT | SZ_16;
    REQUIRE_FALSE(entryIsSigned(ui16in));
    REQUIRE(entryIsInput(ui16in));
    REQUIRE(entryBitSize(ui16in) == SZ_16);

    uint8_t ui32out = DIR_OUTPUT | BASE_INT | SZ_32;
    REQUIRE_FALSE(entryIsSigned(ui32out));
    REQUIRE(entryIsOutput(ui32out));
    REQUIRE(entryBitSize(ui32out) == SZ_32);
}

// ============================================================================
// Bitmask composition — Float types
// ============================================================================
TEST_CASE("Composed float types are correct", "[entrytype]")
{
    uint8_t fin = DIR_INPUT | BASE_FLOAT | SZ_32;
    REQUIRE(entryIsInput(fin));
    REQUIRE_FALSE(entryIsMessage(fin));
    REQUIRE(entryBitSize(fin) == SZ_32);

    uint8_t fout = DIR_OUTPUT | BASE_FLOAT | SZ_32;
    REQUIRE(entryIsOutput(fout));
    REQUIRE(entryBitSize(fout) == SZ_32);
}

// ============================================================================
// Bitmask composition — Bool types
// ============================================================================
TEST_CASE("Composed bool types are correct", "[entrytype]")
{
    uint8_t bin = DIR_INPUT | BASE_BOOL | SZ_1;
    REQUIRE(entryIsInput(bin));
    REQUIRE(entryBitSize(bin) == SZ_1);
    REQUIRE_FALSE(entryIsMessage(bin));

    uint8_t bout = DIR_OUTPUT | BASE_BOOL | SZ_1;
    REQUIRE(entryIsOutput(bout));
    REQUIRE(entryBitSize(bout) == SZ_1);
}

// ============================================================================
// Bitmask composition — Message types
// ============================================================================
TEST_CASE("Composed message types are correct", "[entrytype]")
{
    uint8_t msgin = DIR_INPUT | BASE_MSG;
    REQUIRE(entryIsInput(msgin));
    REQUIRE(entryIsMessage(msgin));

    uint8_t msgout = DIR_OUTPUT | BASE_MSG;
    REQUIRE(entryIsOutput(msgout));
    REQUIRE(entryIsMessage(msgout));
}

// ============================================================================
// No collisions between composed and convenience constants
// ============================================================================
TEST_CASE("Composed types match convenience constants", "[entrytype]")
{
    REQUIRE((DIR_INPUT | BASE_BOOL | SZ_1) == uint8_t(EntryType::BoolInput));
    REQUIRE((DIR_OUTPUT | BASE_BOOL | SZ_1) == uint8_t(EntryType::BoolOutput));
    REQUIRE((DIR_INPUT | SIGNED | BASE_INT | SZ_16) == uint8_t(EntryType::Int16Input));
    REQUIRE((DIR_OUTPUT | SIGNED | BASE_INT | SZ_32) == uint8_t(EntryType::Int32Output));
    REQUIRE((DIR_INPUT | BASE_FLOAT | SZ_32) == uint8_t(EntryType::FloatInput));
    REQUIRE((DIR_OUTPUT | BASE_FLOAT | SZ_32) == uint8_t(EntryType::FloatOutput));
    REQUIRE((DIR_INPUT | BASE_MSG) == uint8_t(EntryType::MessageIn));
    REQUIRE((DIR_OUTPUT | BASE_MSG) == uint8_t(EntryType::MessageOut));
}

// ============================================================================
// entryValueFormat extractor
// ============================================================================
TEST_CASE("entryValueFormat returns base type + size fields", "[entrytype]")
{
    // Mask 0x78 = bits 3,4,5,6 (base type [3-4] + bit size [5-6])
    // 0x78 = 01111000 = 0x08 + 0x10 + 0x20 + 0x40
    REQUIRE(entryValueFormat(EntryType::BoolInput) == (BASE_BOOL | SZ_1));
    REQUIRE(entryValueFormat(EntryType::Int16Input) == (BASE_INT | SZ_16));
    REQUIRE(entryValueFormat(EntryType::FloatInput) == (BASE_FLOAT | SZ_32));
}
