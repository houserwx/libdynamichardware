#include <catch2/catch_test_macros.hpp>
#include <dynamichardware/dhdo/DHDOFactory.h>
#include <cstring>

using namespace dynamichardware::dhdo;

// ============================================================================
// DHDOFactory stringToEntryType — semantic channelType strings → EntryType
// ============================================================================
TEST_CASE("DHDOFactory stringToEntryType maps IMU types to FloatInput", "[factory]")
{
    REQUIRE(DHDOFactory::stringToEntryType("IMU_GyroX") == EntryType::FloatInput);
    REQUIRE(DHDOFactory::stringToEntryType("IMU_GyroY") == EntryType::FloatInput);
    REQUIRE(DHDOFactory::stringToEntryType("IMU_GyroZ") == EntryType::FloatInput);
    REQUIRE(DHDOFactory::stringToEntryType("IMU_AccelX") == EntryType::FloatInput);
    REQUIRE(DHDOFactory::stringToEntryType("IMU_AccelY") == EntryType::FloatInput);
    REQUIRE(DHDOFactory::stringToEntryType("IMU_AccelZ") == EntryType::FloatInput);
}

TEST_CASE("DHDOFactory stringToEntryType maps GPS types to FloatInput", "[factory]")
{
    REQUIRE(DHDOFactory::stringToEntryType("GPS_Latitude") == EntryType::FloatInput);
    REQUIRE(DHDOFactory::stringToEntryType("GPS_Longitude") == EntryType::FloatInput);
    REQUIRE(DHDOFactory::stringToEntryType("GPS_Altitude") == EntryType::FloatInput);
    REQUIRE(DHDOFactory::stringToEntryType("GPS_Heading") == EntryType::FloatInput);
}

TEST_CASE("DHDOFactory stringToEntryType maps baro/compass to FloatInput", "[factory]")
{
    REQUIRE(DHDOFactory::stringToEntryType("Baro_Pressure") == EntryType::FloatInput);
    REQUIRE(DHDOFactory::stringToEntryType("Baro_Temperature") == EntryType::FloatInput);
    REQUIRE(DHDOFactory::stringToEntryType("Compass_Heading") == EntryType::FloatInput);
}

TEST_CASE("DHDOFactory stringToEntryType maps legacy aliases", "[factory]")
{
    REQUIRE(DHDOFactory::stringToEntryType("DigitalInput") == EntryType::BoolInput);
    REQUIRE(DHDOFactory::stringToEntryType("DigitalOutput") == EntryType::BoolOutput);
    REQUIRE(DHDOFactory::stringToEntryType("Encoder") == EntryType::Int32Input);
    REQUIRE(DHDOFactory::stringToEntryType("AnalogInput") == EntryType::Int16Input);
    REQUIRE(DHDOFactory::stringToEntryType("AnalogOutput") == EntryType::Int16Output);
    REQUIRE(DHDOFactory::stringToEntryType("GPS_FixQuality") == EntryType::Int16Input);
}

TEST_CASE("DHDOFactory stringToEntryType unknown defaults to BoolInput", "[factory]")
{
    REQUIRE(DHDOFactory::stringToEntryType("UnknownType") == EntryType::BoolInput);
    REQUIRE(DHDOFactory::stringToEntryType("") == EntryType::BoolInput);
}

TEST_CASE("DHDOFactory stringToEntryType is case-insensitive", "[factory]")
{
    REQUIRE(DHDOFactory::stringToEntryType("imu_gyrox") == EntryType::FloatInput);
    REQUIRE(DHDOFactory::stringToEntryType("IMU_ACCELZ") == EntryType::FloatInput);
    REQUIRE(DHDOFactory::stringToEntryType("digitalinput") == EntryType::BoolInput);
    REQUIRE(DHDOFactory::stringToEntryType("ENCODER") == EntryType::Int32Input);
}

// ============================================================================
// DHDOFactory entryTypeToString — bitmask composited names
// ============================================================================
TEST_CASE("DHDOFactory entryTypeToString returns correct names for convenience constants", "[factory]")
{
    CHECK(std::string(DHDOFactory::entryTypeToString(EntryType::BoolInput)) == "BoolInput");
    CHECK(std::string(DHDOFactory::entryTypeToString(EntryType::BoolOutput)) == "BoolOutput");
    CHECK(std::string(DHDOFactory::entryTypeToString(EntryType::Int8Input)) == "Int8Input");
    CHECK(std::string(DHDOFactory::entryTypeToString(EntryType::Int16Input)) == "Int16Input");
    CHECK(std::string(DHDOFactory::entryTypeToString(EntryType::Int32Input)) == "Int32Input");
    CHECK(std::string(DHDOFactory::entryTypeToString(EntryType::Int8Output)) == "Int8Output");
    CHECK(std::string(DHDOFactory::entryTypeToString(EntryType::Int16Output)) == "Int16Output");
    CHECK(std::string(DHDOFactory::entryTypeToString(EntryType::Int32Output)) == "Int32Output");
    CHECK(std::string(DHDOFactory::entryTypeToString(EntryType::FloatInput)) == "FloatInput");
    CHECK(std::string(DHDOFactory::entryTypeToString(EntryType::FloatOutput)) == "FloatOutput");
    CHECK(std::string(DHDOFactory::entryTypeToString(EntryType::MessageIn)) == "MessageIn");
    CHECK(std::string(DHDOFactory::entryTypeToString(EntryType::MessageOut)) == "MessageOut");
}

TEST_CASE("DHDOFactory entryTypeToString composes names for custom bitmasks", "[factory]")
{
    // Unsigned 8-bit input: DIR_INPUT | BASE_INT | SZ_8 (no SIGNED bit)
    uint8_t ui8in = DIR_INPUT | BASE_INT | SZ_8;
    auto name = DHDOFactory::entryTypeToString(static_cast<EntryType>(ui8in));
    REQUIRE(!name.empty());
    // Should contain "Int" and "In"
    CHECK(name.find("Int") != std::string::npos);
    CHECK(name.find("In") != std::string::npos);
}

// ============================================================================
// DHDOFactory defaultBitLength
// ============================================================================
TEST_CASE("DHDOFactory defaultBitLength returns correct bit widths", "[factory]")
{
    REQUIRE(DHDOFactory::defaultBitLength(EntryType::BoolInput) == 1);
    REQUIRE(DHDOFactory::defaultBitLength(EntryType::BoolOutput) == 1);
    REQUIRE(DHDOFactory::defaultBitLength(EntryType::Int8Input) == 8);
    REQUIRE(DHDOFactory::defaultBitLength(EntryType::Int16Input) == 16);
    REQUIRE(DHDOFactory::defaultBitLength(EntryType::Int32Input) == 32);
    REQUIRE(DHDOFactory::defaultBitLength(EntryType::Int16Output) == 16);
    REQUIRE(DHDOFactory::defaultBitLength(EntryType::FloatInput) == 32);
    REQUIRE(DHDOFactory::defaultBitLength(EntryType::FloatOutput) == 32);
    REQUIRE(DHDOFactory::defaultBitLength(EntryType::MessageIn) == 0);
    REQUIRE(DHDOFactory::defaultBitLength(EntryType::MessageOut) == 0);
}

TEST_CASE("DHDOFactory defaultBitLength works with composed unsigned types", "[factory]")
{
    uint8_t ui16 = DIR_INPUT | BASE_INT | SZ_16;
    REQUIRE(DHDOFactory::defaultBitLength(static_cast<EntryType>(ui16)) == 16);

    uint8_t ui32 = DIR_OUTPUT | BASE_INT | SZ_32;
    REQUIRE(DHDOFactory::defaultBitLength(static_cast<EntryType>(ui32)) == 32);
}

// ============================================================================
// DHDOFactory fromCatalogEntry — integration test
// ============================================================================
TEST_CASE("DHDOFactory fromCatalogEntry creates correct DHDOEntry from IMU entry", "[factory]")
{
    CatalogEntry ce;
    ce.channelType = "IMU_GyroX";
    ce.uuid        = "test-imu-gyro-001";
    ce.isSimulated = false;

    auto entry = DHDOFactory::fromCatalogEntry(ce);
    REQUIRE(entry.type == EntryType::FloatInput);
    REQUIRE(entry.uuid == "test-imu-gyro-001");
    REQUIRE(entry.bitLength == 32);
}

TEST_CASE("DHDOFactory fromCatalogEntry creates correct DHDOEntry from simulated entry", "[factory]")
{
    CatalogEntry ce;
    ce.channelType = "Encoder";
    ce.uuid        = "virt-enc-001";
    ce.isSimulated = true;
    ce.sim.pulseMs    = 100;
    ce.sim.debounceMs = 5;

    auto entry = DHDOFactory::fromCatalogEntry(ce);
    REQUIRE(entry.type == EntryType::Int32Input);
    REQUIRE(entry.bitLength == 32);
}

TEST_CASE("DHDOFactory create with explicit parameters", "[factory]")
{
    auto entry = DHDOFactory::create(
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

TEST_CASE("DHDOFactory create with 0 bitLength uses default", "[factory]")
{
    auto entry = DHDOFactory::create(
        EntryType::BoolOutput,
        "zero-bitlen-uuid",
        0, 0, 0
    );

    REQUIRE(entry.type == EntryType::BoolOutput);
    REQUIRE(entry.bitLength == 1);  // BoolOutput default
}
