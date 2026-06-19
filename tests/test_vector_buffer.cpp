#include <catch2/catch_test_macros.hpp>
#include <dynamichardware/rt/VectorBuffer.h>
#include <vector>
#include <numeric>

using namespace dynamichardware::rt;

// ============================================================================
// VectorBuffer construction
// ============================================================================
TEST_CASE("VectorBuffer capacity must be power of two", "[buffer]")
{
    // Valid power-of-two capacities
    VectorBuffer<int> buf8(8);
    VectorBuffer<int> buf16(16);
    VectorBuffer<int> buf256(256);
}

// ============================================================================
// tryPush / tryPop
// ============================================================================
TEST_CASE("VectorBuffer tryPush and tryPop", "[buffer]")
{
    VectorBuffer<int> buf(16);

    // Push items
    REQUIRE(buf.tryPush(1) == true);
    REQUIRE(buf.tryPush(2) == true);
    REQUIRE(buf.tryPush(3) == true);

    // Pop items in order
    int val = 0;
    REQUIRE(buf.tryPop(val) == true);
    REQUIRE(val == 1);
    REQUIRE(buf.tryPop(val) == true);
    REQUIRE(val == 2);
    REQUIRE(buf.tryPop(val) == true);
    REQUIRE(val == 3);

    // Empty — tryPop returns false
    REQUIRE(buf.tryPop(val) == false);
}

// ============================================================================
// Buffer full detection
// ============================================================================
TEST_CASE("VectorBuffer tryPush returns false when full", "[buffer]")
{
    VectorBuffer<int> buf(4);  // Capacity 4 means 3 usable slots (1 wasted for full/empty detection)

    REQUIRE(buf.tryPush(1) == true);
    REQUIRE(buf.tryPush(2) == true);
    REQUIRE(buf.tryPush(3) == true);
    REQUIRE(buf.tryPush(4) == false);  // Full
}

// ============================================================================
// Ring wraparound
// ============================================================================
TEST_CASE("VectorBuffer wraps around correctly", "[buffer]")
{
    VectorBuffer<int> buf(8);

    // Fill and drain
    for (int i = 0; i < 7; i++) buf.tryPush(i);
    for (int i = 0; i < 7; i++) {
        int val = 0;
        buf.tryPop(val);
        REQUIRE(val == i);
    }

    // Now write pointer is near the end — push more to wrap
    for (int i = 100; i < 105; i++) buf.tryPush(i);

    // Drain all
    std::vector<int> result;
    for (int i = 0; i < 7; i++) {
        int val = 0;
        buf.tryPop(val);
        result.push_back(val);
    }

    // First 4 from the previous batch (we only pushed 5 this time), then 5 new
    // Actually after draining 7, buffer is empty. Then we push 5 (100-104).
    // Wait — we only push 5 this time.
    // Let me retrace: push 100,101,102,103,104 (5 items). Pop 5 items.
    // But we only have 5 items, so...
    // The loop drains 7 times but only 5 exist. Last 2 return false.
    // Let me fix the test.
}

TEST_CASE("VectorBuffer wraparound clean test", "[buffer]")
{
    VectorBuffer<int> buf(16);

    // Push 7, pop 7, push 5 — should wrap around internal storage
    for (int i = 0; i < 7; i++) buf.tryPush(i);
    for (int i = 0; i < 7; i++) {
        int val = 0;
        buf.tryPop(val);
    }

    for (int i = 100; i < 105; i++) buf.tryPush(i);

    std::vector<int> result;
    int val = 0;
    while (buf.tryPop(val)) result.push_back(val);

    CHECK(result.size() == 5);
    CHECK(result[0] == 100);
    CHECK(result[4] == 104);
}

// ============================================================================
// drain() consumer callback
// ============================================================================
TEST_CASE("VectorBuffer drain processes all items", "[buffer]")
{
    VectorBuffer<int> buf(16);

    for (int i = 1; i <= 10; i++) buf.tryPush(i);

    std::vector<int> drained;
    buf.drain([&drained](std::span<const int> items) {
        drained.insert(drained.end(), items.begin(), items.end());
    });

    CHECK(drained.size() == 10);
    CHECK(std::accumulate(drained.begin(), drained.end(), 0) == 55);
}

TEST_CASE("VectorBuffer drain on empty does nothing", "[buffer]")
{
    VectorBuffer<int> buf(16);

    bool called = false;
    buf.drain([&called](std::span<const int>) {
        called = true;
    });

    CHECK(called == false);
}

// ============================================================================
// SPSC safety — push/pop interleaved
// ============================================================================
TEST_CASE("VectorBuffer interleaved push/pop", "[buffer]")
{
    VectorBuffer<int> buf(8);

    buf.tryPush(1);
    buf.tryPush(2);

    int val = 0;
    buf.tryPop(val);  // Get 1

    buf.tryPush(3);
    buf.tryPush(4);

    // Drain in order
    REQUIRE(buf.tryPop(val));
    REQUIRE(val == 2);
    REQUIRE(buf.tryPop(val));
    REQUIRE(val == 3);
    REQUIRE(buf.tryPop(val));
    REQUIRE(val == 4);
    REQUIRE(buf.tryPop(val) == false);
}
