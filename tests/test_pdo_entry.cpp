#include <catch2/catch_test_macros.hpp>
#include <dynamichardware/pdo/PDO.h>
#include <cstring>
#include <cmath>

using namespace dynamichardware::pdo;

// ============================================================================
// PDOEntry bool input read/debounce
// ============================================================================
TEST_CASE("PDOEntry BoolInput reads raw bit from image", "[pdoentry]")
{
    uint8_t image[8] = {0};
    PDOEntry entry;
    entry.type       = EntryType::BoolInput;
    entry.image      = image;
    entry.bitOffset  = 0;
    entry.bitLength  = 1;
    entry.debounce.configure(0);  // No debounce

    image[0] = 0;
    entry.read();
    REQUIRE(entry.getBool() == false);

    image[0] = 1;
    entry.read();
    REQUIRE(entry.getBool() == true);
}

TEST_CASE("PDOEntry BoolInput reads specific bit offset", "[pdoentry]")
{
    uint8_t image[8] = {0};
    PDOEntry entry;
    entry.type       = EntryType::BoolInput;
    entry.image      = image;
    entry.bitOffset  = 3;
    entry.bitLength  = 1;
    entry.debounce.configure(0);

    image[0] = (1 << 3);  // bit 3 set
    entry.read();
    REQUIRE(entry.getBool() == true);

    image[0] = 0;
    entry.read();
    REQUIRE(entry.getBool() == false);
}

TEST_CASE("PDOEntry BoolOutput writes bit to image", "[pdoentry]")
{
    uint8_t image[8] = {0};
    PDOEntry entry;
    entry.type       = EntryType::BoolOutput;
    entry.image      = image;
    entry.bitOffset  = 2;
    entry.bitLength  = 1;
    entry.configurePulseMs(0);  // Latched mode

    entry.setBool(true);
    entry.write();
    REQUIRE((image[0] & (1 << 2)) != 0);

    image[0] = 0xFF;
    entry.setBool(false);
    entry.write();
    REQUIRE((image[0] & (1 << 2)) == 0);
}

TEST_CASE("PDOEntry BoolOutput pulse mode", "[pdoentry]")
{
    uint8_t image[8] = {0};
    PDOEntry entry;
    entry.type       = EntryType::BoolOutput;
    entry.image      = image;
    entry.bitOffset  = 0;
    entry.bitLength  = 1;
    entry.configurePulseMs(100);  // 100ms pulse

    uint64_t now = 1000000000ULL;  // 1 second in ns
    dynamichardware::rt::gSignalProcessNowNs = now;

    // Arm pulse
    entry.setBool(true);
    REQUIRE(entry.getBool() == true);
    entry.write();
    REQUIRE((image[0] & 0x01) != 0);

    // Within pulse duration — still high
    dynamichardware::rt::gSignalProcessNowNs = now + 50'000'000ULL;  // 50ms later
    REQUIRE(entry.getBool() == true);
    entry.write();
    REQUIRE((image[0] & 0x01) != 0);

    // After pulse expires — tick() in write() clears active_
    dynamichardware::rt::gSignalProcessNowNs = now + 200'000'000ULL;  // 200ms later
    entry.write();  // tick() expires, active_ = false, output cleared
    REQUIRE(entry.getBool() == false);  // pulse has expired
    REQUIRE((image[0] & 0x01) == 0);   // byte cleared
}

// ============================================================================
// PDOEntry int32 input (encoder)
// ============================================================================
TEST_CASE("PDOEntry Int32Input reads 32-bit LE value", "[pdoentry]")
{
    uint8_t image[8] = {0};
    PDOEntry entry;
    entry.type       = EntryType::Int32Input;
    entry.image      = image;
    entry.byteOffset = 0;
    entry.bitLength  = 32;

    int32_t val = 0x01020304;
    std::memcpy(image, &val, sizeof(val));
    entry.read();
    REQUIRE(entry.getInt32() == val);
}

TEST_CASE("PDOEntry Int16Input reads 16-bit LE value", "[pdoentry]")
{
    uint8_t image[8] = {0};
    PDOEntry entry;
    entry.type       = EntryType::Int16Input;
    entry.image      = image;
    entry.byteOffset = 0;
    entry.bitLength  = 16;

    int16_t val = -1000;
    std::memcpy(image, &val, sizeof(val));
    entry.read();
    REQUIRE(entry.getInt16() == val);
}

TEST_CASE("PDOEntry FloatInput reads 32-bit float", "[pdoentry]")
{
    uint8_t image[8] = {0};
    PDOEntry entry;
    entry.type       = EntryType::FloatInput;
    entry.image      = image;
    entry.byteOffset = 0;
    entry.bitLength  = 32;

    float val = 3.14159f;
    std::memcpy(image, &val, sizeof(val));
    entry.read();
    REQUIRE(std::fabs(entry.getFloat() - val) < 0.0001f);
}

// ============================================================================
// PDOEntry write outputs
// ============================================================================
TEST_CASE("PDOEntry Int16Output writes 16-bit value", "[pdoentry]")
{
    uint8_t image[8] = {0};
    PDOEntry entry;
    entry.type       = EntryType::Int16Output;
    entry.image      = image;
    entry.byteOffset = 0;
    entry.bitLength  = 16;

    entry.setInt16(32000);
    entry.write();

    int16_t val;
    std::memcpy(&val, image, sizeof(val));
    REQUIRE(val == 32000);
}

TEST_CASE("PDOEntry FloatOutput writes 32-bit float", "[pdoentry]")
{
    uint8_t image[8] = {0};
    PDOEntry entry;
    entry.type       = EntryType::FloatOutput;
    entry.image      = image;
    entry.byteOffset = 0;
    entry.bitLength  = 32;

    entry.setFloat(2.718f);
    entry.write();

    float val;
    std::memcpy(&val, image, sizeof(val));
    REQUIRE(std::fabs(val - 2.718f) < 0.00001f);
}

// ============================================================================
// PDOEntry read() with no image — no crash
// ============================================================================
TEST_CASE("PDOEntry read/write with null image does nothing", "[pdoentry]")
{
    PDOEntry entry;
    entry.type  = EntryType::BoolInput;
    entry.image = nullptr;

    // Should not crash
    entry.read();
    entry.write();

    REQUIRE(entry.getBool() == false);  // default value
}

// ============================================================================
// PDOEntry message slot operations
// ============================================================================
TEST_CASE("PDOEntry message arm/consume works", "[pdoentry]")
{
    PDOEntry entry;
    entry.type = EntryType::MessageOut;

    struct TestMsg { int x; float y; };
    TestMsg msg{42, 3.14f};

    entry.armOutMessage(msg);
    REQUIRE(entry.msgSlot_.pending == true);
    REQUIRE(entry.msgSlot_.size == sizeof(TestMsg));

    TestMsg consumed{};
    REQUIRE(entry.tryConsumeOutMessage(consumed));
    REQUIRE(consumed.x == 42);
    REQUIRE(std::fabs(consumed.y - 3.14f) < 0.001f);
    REQUIRE(entry.msgSlot_.pending == false);
}

TEST_CASE("PDOEntry in-message set/consume works", "[pdoentry]")
{
    PDOEntry entry;
    entry.type = EntryType::MessageIn;

    struct TestMsg { double value; };
    TestMsg msg{1.23};

    entry.setInMessageRaw(&msg, sizeof(msg));
    REQUIRE(entry.hasInMessage() == true);

    TestMsg consumed{};
    REQUIRE(entry.tryGetInMessage(consumed));
    REQUIRE(std::fabs(consumed.value - 1.23) < 0.001);

    entry.consumeInMessage();
    REQUIRE(entry.hasInMessage() == false);
}
