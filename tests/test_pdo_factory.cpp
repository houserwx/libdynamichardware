#include <catch2/catch_test_macros.hpp>
#include <dynamichardware/pdo/PDOFactory.h>
#include <cstring>

using namespace dynamichardware::pdo;

// ============================================================================
// PDOFactory stringToEntryType — semantic channelType strings → EntryType
// ============================================================================
TEST_CASE("PDOFactory stringToEntryType maps IMU types to FloatInput", "[factory]")
{
    REQUIRE(PDOFactory::stringToEntryType("IMU_GyroX") == EntryType::FloatInput);
    REQUIRE(PDOFactory::stringToEntryType("IMU_GyroY") == EntryType::FloatInput);
    REQUIRE(PDOFactory::stringToEntryType("IMU_GyroZ") == EntryType::FloatInput);
    REQUIRE(PDOFactory::stringToEntryType("IMU_AccelX") == EntryType::FloatInput);
    REQUIRE(PDOFactory::stringToEntryType("IMU_AccelY") == EntryType::FloatInput);
    REQUIRE(PDOFactory::stringToEntryType("IMU_AccelZ") == EntryType::FloatInput);
}

TEST_CASE("PDOFactory stringToEntryType maps GPS types to FloatInput", "[factory]")
{
    REQUIRE(PDOFactory::stringToEntryType("GPS_Latitude") == EntryType::FloatInput);
    REQUIRE(PDOFactory::stringToEntryType("GPS_Longitude") == EntryType::FloatInput);
    REQUIRE(PDOFactory::stringToEntryType("GPS_Altitude") == EntryType::FloatInput);
    REQUIRE(PDOFactory::stringToEntryType("GPS_Heading") == EntryType::FloatInput);
}

TEST_CASE("PDOFactory stringToEntryType maps baro/compass to FloatInput", "[factory]")
{
    REQUIRE(PDOFactory::stringToEntryType("Baro_Pressure") == EntryType::FloatInput);
    REQUIRE(PDOFactory::stringToEntryType("Baro_Temperature") == EntryType::FloatInput);
    REQUIRE(PDOFactory::stringToEntryType("Compass_Heading") == EntryType::FloatInput);
}

TEST_CASE("PDOFactory stringToEntryType maps legacy aliases", "[factory]")
{
    REQUIRE(PDOFactory::stringToEntryType("DigitalInput") == EntryType::BoolInput);
    REQUIRE(PDOFactory::stringToEntryType("DigitalOutput") == EntryType::BoolOutput);
    REQUIRE(PDOFactory::stringToEntryType("Encoder") == EntryType::Int32Input);
    REQUIRE(PDOFactory::stringToEntryType("AnalogInput") == EntryType::Int16Input);
    REQUIRE(PDOFactory::stringToEntryType("AnalogOutput") == EntryType::Int16Output);
    REQUIRE(PDOFactory::stringToEntryType("GPS_FixQuality") == EntryType::Int16Input);
}

TEST_CASE("PDOFactory stringToEntryType unknown defaults to BoolInput", "[factory]")
{
    REQUIRE(PDOFactory::stringToEntryType("UnknownType") == EntryType::BoolInput);
    REQUIRE(PDOFactory::stringToEntryType("") == EntryType::BoolInput);
}

TEST_CASE("PDOFactory stringToEntryType is case-insensitive", "[factory]")
{
    REQUIRE(PDOFactory::stringToEntryType("imu_gyrox") == EntryType::FloatInput);
    REQUIRE(PDOFactory::stringToEntryType("IMU_ACCELZ") == EntryType::FloatInput);
    REQUIRE(PDOFactory::stringToEntryType("digitalinput") == EntryType::BoolInput);
    REQUIRE(PDOFactory::stringToEntryType("ENCODER") == EntryType::Int32Input);
}

// ============================================================================
// PDOFactory entryTypeToString — bitmask composited names
// ============================================================================
TEST_CASE("PDOFactory entryTypeToString returns correct names for convenience constants", "[factory]")
{
    CHECK(std::string(PDOFactory::entryTypeToString(EntryType::BoolInput)) == "BoolInput");
    CHECK(std::string(PDOFactory::entryTypeToString(EntryType::BoolOutput)) == "BoolOutput");
    CHECK(std::string(PDOFactory::entryTypeToString(EntryType::Int8Input)) == "Int8Input");
    CHECK(std::string(PDOFactory::entryTypeToString(EntryType::Int16Input)) == "Int16Input");
    CHECK(std::string(PDOFactory::entryTypeToString(EntryType::Int32Input)) == "Int32Input");
    CHECK(std::string(PDOFactory::entryTypeToString(EntryType::Int8Output)) == "Int8Output");
    CHECK(std::string(PDOFactory::entryTypeToString(EntryType::Int16Output)) == "Int16Output");
    CHECK(std::string(PDOFactory::entryTypeToString(EntryType::Int32Output)) == "Int32Output");
    CHECK(std::string(PDOFactory::entryTypeToString(EntryType::FloatInput)) == "FloatInput");
    CHECK(std::string(PDOFactory::entryTypeToString(EntryType::FloatOutput)) == "FloatOutput");
    CHECK(std::string(PDOFactory::entryTypeToString(EntryType::MessageIn)) == "MessageIn");
    CHECK(std::string(PDOFactory::entryTypeToString(EntryType::MessageOut)) == "MessageOut");
}

TEST_CASE("PDOFactory entryTypeToString composes names for custom bitmasks", "[factory]")
{
    // Unsigned 8-bit input: DIR_INPUT | BASE_INT | SZ_8 (no SIGNED bit)
    uint8_t ui8in = DIR_INPUT | BASE_INT | SZ_8;
    auto name = PDOFactory::entryTypeToString(static_cast<EntryType>(ui8in));
    REQUIRE(name != nullptr);
    // Should contain "Int", "8", and "In"
    CHECK(std::string(name).find("Int") != std::string::npos);
    CHECK(std::string(name).find("In") != std::string::npos);
}

// ============================================================================
// PDOFactory defaultBitLength
// ============================================================================
TEST_CASE("PDOFactory defaultBitLength returns correct bit widths", "[factory]")
{
    REQUIRE(PDOFactory::defaultBitLength(EntryType::BoolInput) == 1);
    REQUIRE(PDOFactory::defaultBitLength(EntryType::BoolOutput) == 1);
    REQUIRE(PDOFactory::defaultBitLength(EntryType::Int8Input) == 8);
    REQUIRE(PDOFactory::defaultBitLength(EntryType::Int16Input) == 16);
    REQUIRE(PDOFactory::defaultBitLength(EntryType::Int32Input) == 32);
    REQUIRE(PDOFactory::defaultBitLength(EntryType::Int16Output) == 16);
    REQUIRE(PDOFactory::defaultBitLength(EntryType::FloatInput) == 32);
    REQUIRE(PDOFactory::defaultBitLength(EntryType::FloatOutput) == 32);
    REQUIRE(PDOFactory::defaultBitLength(EntryType::MessageIn) == 0);
    REQUIRE(PDOFactory::defaultBitLength(EntryType::MessageOut) == 0);
}

TEST_CASE("PDOFactory defaultBitLength works with composed unsigned types", "[factory]")
{
    uint8_t ui16 = DIR_INPUT | BASE_INT | SZ_16;
    REQUIRE(PDOFactory::defaultBitLength(static_cast<EntryType>(ui16)) == 16);

    uint8_t ui32 = DIR_OUTPUT | BASE_INT | SZ_32;
    REQUIRE(PDOFactory::defaultBitLength(static_cast<EntryType>(ui32)) == 32);
}

// ============================================================================
// PDOFactory fromCatalogEntry — integration test
// ============================================================================
TEST_CASE("PDOFactory fromCatalogEntry creates correct PDOEntry from IMU entry", "[factory]")
{
    CatalogEntry ce;
    ce.channelType = "IMU_GyroX";
    ce.uuid        = "test-imu-gyro-001";
    ce.isSimulated = false;

    auto entry = PDOFactory::fromCatalogEntry(ce);
    REQUIRE(entry.type == EntryType::FloatInput);
    REQUIRE(entry.uuid == "test-imu-gyro-001");
    REQUIRE(entry.bitLength == 32);
}

TEST_CASE("PDOFactory fromCatalogEntry creates correct PDOEntry from simulated entry", "[factory]")
{
    CatalogEntry ce;
    ce.channelType = "Encoder";
    ce.uuid        = "virt-enc-001";
    ce.isSimulated = true;
    ce.sim.pulseMs    = 100;
    ce.sim.debounceMs = 5;

    auto entry = PDOFactory::fromCatalogEntry(ce);
    REQUIRE(entry.type == EntryType::Int32Input);
    REQUIRE(entry.bitLength == 32);
}

TEST_CASE("PDOFactory create with explicit parameters", "[factory]")
{
    auto entry = PDOFactory::create(
        EntryType::Int16Input,
        "explicit-test-uuid",
        0,    // pulseMs
        10,   // debounceMs
        16    // bitLength
    );

    REQUIRE(entry.type == EntryType::Int16Input);
    REQUIRE(entry.uuid == "explicit-test-uuid");
    REQUIRE(entry.bitLength == 16);
}

TEST_CASE("PDOFactory create with 0 bitLength uses default", "[factory]")
{
    auto entry = PDOFactory::create(
        EntryType::BoolOutput,
        "zero-bitlen-uuid",
        0, 0, 0
    );

    REQUIRE(entry.type == EntryType::BoolOutput);
    REQUIRE(entry.bitLength == 1);  // BoolOutput default
}
