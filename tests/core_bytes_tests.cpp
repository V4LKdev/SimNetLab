#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

import simnet.core;

TEST_CASE("core bytes preserve exact network order", "[core][bytes]")
{
    auto bytes = std::vector<simnet::Byte>{simnet::Byte{0xAAU}};
    simnet::append_byte(bytes, 0x7FU);
    simnet::append_big_endian(bytes, std::uint16_t{0x1234U});
    simnet::append_big_endian(bytes, std::uint32_t{0x10203040U});
    simnet::append_big_endian(bytes, std::uint64_t{0x0102030405060708ULL});
    simnet::append_float32_big_endian(bytes, 1.0F);
    simnet::append_float32_big_endian(bytes, -2.5F);
    simnet::append_float32_big_endian(bytes, -0.0F);

    auto constexpr expected = std::array{
        simnet::Byte{0xAAU}, simnet::Byte{0x7FU}, simnet::Byte{0x12U}, simnet::Byte{0x34U},
        simnet::Byte{0x10U}, simnet::Byte{0x20U}, simnet::Byte{0x30U}, simnet::Byte{0x40U},
        simnet::Byte{0x01U}, simnet::Byte{0x02U}, simnet::Byte{0x03U}, simnet::Byte{0x04U},
        simnet::Byte{0x05U}, simnet::Byte{0x06U}, simnet::Byte{0x07U}, simnet::Byte{0x08U},
        simnet::Byte{0x3FU}, simnet::Byte{0x80U}, simnet::Byte{0x00U}, simnet::Byte{0x00U},
        simnet::Byte{0xC0U}, simnet::Byte{0x20U}, simnet::Byte{0x00U}, simnet::Byte{0x00U},
        simnet::Byte{0x80U}, simnet::Byte{0x00U}, simnet::Byte{0x00U}, simnet::Byte{0x00U},
    };
    CHECK(bytes == std::vector<simnet::Byte>{expected.begin(), expected.end()});

    auto offset = std::size_t{1U};
    auto byte = std::uint8_t{};
    auto value16 = std::uint16_t{};
    auto value32 = std::uint32_t{};
    auto value64 = std::uint64_t{};
    auto float32 = 0.0F;
    auto negative_float32 = 0.0F;
    auto negative_zero = 0.0F;
    REQUIRE(simnet::read_byte(bytes, offset, byte));
    REQUIRE(simnet::read_big_endian(bytes, offset, value16));
    REQUIRE(simnet::read_big_endian(bytes, offset, value32));
    REQUIRE(simnet::read_big_endian(bytes, offset, value64));
    REQUIRE(simnet::read_float32_big_endian(bytes, offset, float32));
    REQUIRE(simnet::read_float32_big_endian(bytes, offset, negative_float32));
    REQUIRE(simnet::read_float32_big_endian(bytes, offset, negative_zero));
    CHECK(byte == 0x7FU);
    CHECK(value16 == 0x1234U);
    CHECK(value32 == 0x10203040U);
    CHECK(value64 == 0x0102030405060708ULL);
    CHECK(float32 == 1.0F);
    CHECK(negative_float32 == -2.5F);
    CHECK(std::signbit(negative_zero));
    CHECK(offset == bytes.size());
}

TEST_CASE("core byte reads fail atomically", "[core][bytes]")
{
    auto const bytes = std::array{
        simnet::Byte{0x01U},
        simnet::Byte{0x02U},
        simnet::Byte{0x03U},
        simnet::Byte{0x04U},
        simnet::Byte{0x05U},
        simnet::Byte{0x06U},
        simnet::Byte{0x07U},
    };

    auto offset = bytes.size();
    auto byte = std::uint8_t{0xA5U};
    CHECK_FALSE(simnet::read_byte(bytes, offset, byte));
    CHECK(offset == bytes.size());
    CHECK(byte == 0xA5U);

    offset = bytes.size() - 1U;
    auto value16 = std::uint16_t{0xA5A5U};
    CHECK_FALSE(simnet::read_big_endian(bytes, offset, value16));
    CHECK(offset == bytes.size() - 1U);
    CHECK(value16 == 0xA5A5U);

    offset = bytes.size() - 3U;
    auto value32 = std::uint32_t{0xA5A5A5A5U};
    CHECK_FALSE(simnet::read_big_endian(bytes, offset, value32));
    CHECK(offset == bytes.size() - 3U);
    CHECK(value32 == 0xA5A5A5A5U);

    offset = 0U;
    auto value64 = std::uint64_t{0xA5A5A5A5A5A5A5A5ULL};
    CHECK_FALSE(simnet::read_big_endian(bytes, offset, value64));
    CHECK(offset == 0U);
    CHECK(value64 == 0xA5A5A5A5A5A5A5A5ULL);

    offset = bytes.size() - 3U;
    auto float32 = -1.0F;
    CHECK_FALSE(simnet::read_float32_big_endian(bytes, offset, float32));
    CHECK(offset == bytes.size() - 3U);
    CHECK(float32 == -1.0F);

    offset = std::numeric_limits<std::size_t>::max();
    CHECK_FALSE(simnet::read_big_endian(bytes, offset, value32));
    CHECK(offset == std::numeric_limits<std::size_t>::max());
    CHECK(value32 == 0xA5A5A5A5U);
}
