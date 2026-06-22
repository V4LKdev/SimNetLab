#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstdint>

#include "core/net/net_buffer.hpp"
#include "core/net/net_types.hpp"

using simnet::core::net::internal::NetBuffer;


// write
TEST_CASE("NetBuffer: write/read uint8_t", "[net_buffer]")
{
    NetBuffer buf;
    buf.write(static_cast<uint8_t>(0xAB));
    REQUIRE(buf.size() == 1);
    uint8_t val = buf.read<uint8_t>();
    REQUIRE(val == 0xAB);
}

TEST_CASE("NetBuffer: write/read uint16_t", "[net_buffer]")
{
    NetBuffer buf;
    buf.write(static_cast<uint16_t>(0x1234));
    REQUIRE(buf.size() == 2);
    uint16_t val = buf.read<uint16_t>();
    REQUIRE(val == 0x1234);
}

TEST_CASE("NetBuffer: write/read uint32_t", "[net_buffer]")
{
    NetBuffer buf;
    buf.write(static_cast<uint32_t>(0xDEADBEEF));
    REQUIRE(buf.size() == 4);
    uint32_t val = buf.read<uint32_t>();
    REQUIRE(val == 0xDEADBEEF);
}

TEST_CASE("NetBuffer: write/read uint64_t", "[net_buffer]")
{
    NetBuffer buf;
    buf.write(static_cast<uint64_t>(0x12345678));
    REQUIRE(buf.size() == 8);
    uint64_t val = buf.read<uint64_t>();
    REQUIRE(val == 0x12345678);
}

TEST_CASE("NetBuffer: write/read float", "[net_buffer]")
{
    NetBuffer buf;
    const float original = 3.14159f;
    buf.write(original);
    REQUIRE(buf.size() == 4);
    float val = buf.read<float>();
    REQUIRE(val == Catch::Approx(original));
}

TEST_CASE("NetBuffer: write/read bool as uint8_t", "[net_buffer]")
{
    NetBuffer buf;
    buf.write(true);
    buf.write(false);
    REQUIRE(buf.size() == 2);
    REQUIRE(buf.read<uint8_t>() == 1u);
    REQUIRE(buf.read<uint8_t>() == 0u);
}

// Mixed multi‑value sequence
TEST_CASE("NetBuffer: mixed types sequential read", "[net_buffer]")
{
    NetBuffer buf;
    buf.write(static_cast<uint16_t>(0xAABB));
    buf.write(static_cast<uint32_t>(0x12345678));
    buf.write(6.25f);
    buf.write(static_cast<uint8_t>(0x42));

    REQUIRE(buf.size() == 2 + 4 + 4 + 1);

    REQUIRE(buf.read<uint16_t>() == 0xAABB);
    REQUIRE(buf.read<uint32_t>() == 0x12345678);
    REQUIRE(buf.read<float>() == Catch::Approx(6.25f));
    REQUIRE(buf.read<uint8_t>() == 0x42);
}

// Endianness correctness (big‑endian wire format)
TEST_CASE("NetBuffer: big-endian uint16_t byte order", "[net_buffer]")
{
    NetBuffer buf;
    buf.write(static_cast<uint16_t>(0x1234));
    const uint8_t *data = buf.data();
    REQUIRE(data[0] == 0x12); // high byte first
    REQUIRE(data[1] == 0x34);
}

TEST_CASE("NetBuffer: big-endian uint32_t byte order", "[net_buffer]")
{
    NetBuffer buf;
    buf.write(static_cast<uint32_t>(0x12345678));
    const uint8_t *data = buf.data();
    REQUIRE(data[0] == 0x12);
    REQUIRE(data[1] == 0x34);
    REQUIRE(data[2] == 0x56);
    REQUIRE(data[3] == 0x78);
}

TEST_CASE("NetBuffer: big-endian float byte order", "[net_buffer]")
{
    NetBuffer buf;
    float value = 1.0f; // IEEE 754: 0x3F800000
    buf.write(value);
    const uint8_t *data = buf.data();
    REQUIRE(data[0] == 0x3F);
    REQUIRE(data[1] == 0x80);
    REQUIRE(data[2] == 0x00);
    REQUIRE(data[3] == 0x00);
}

// Buffer lifecycle: size, data pointer, reset, clear
TEST_CASE("NetBuffer: size and data after writes", "[net_buffer]")
{
    NetBuffer buf;
    REQUIRE(buf.size() == 0);
    buf.write(static_cast<uint32_t>(42));
    REQUIRE(buf.size() == 4);
    REQUIRE(buf.data() != nullptr);
}

TEST_CASE("NetBuffer: reset_data allows re‑reading", "[net_buffer]")
{
    NetBuffer buf;
    buf.write(static_cast<uint16_t>(0x9999));
    uint16_t a = buf.read<uint16_t>();
    buf.reset_data(); // rewind
    uint16_t b = buf.read<uint16_t>();
    REQUIRE(a == 0x9999);
    REQUIRE(b == 0x9999);
}

TEST_CASE("NetBuffer: Clear resets entirely", "[net_buffer]")
{
    NetBuffer buf;
    buf.write(static_cast<uint32_t>(123456));
    buf.clear();
    REQUIRE(buf.size() == 0);
    // After Clear, a fresh write should produce the same result as original.
    buf.write(static_cast<uint32_t>(123456));
    REQUIRE(buf.read<uint32_t>() == 123456);
}

// Error handling: reading past end
TEST_CASE("NetBuffer: read past end throws", "[net_buffer]")
{
    NetBuffer buf;
    buf.write<uint8_t>(0x01);
    buf.read<uint8_t>(); // consume the only byte
    REQUIRE_THROWS_AS(buf.read<uint8_t>(), std::out_of_range);
}
