#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

import simnet.core;

TEST_CASE("core bytes use exact big-endian network order", "[core][bytes]")
{
    auto bytes = std::vector<simnet::Byte>{};
    simnet::append_big_endian(bytes, std::uint16_t{0x1234U});
    simnet::append_big_endian(bytes, std::uint32_t{0x10203040U});
    simnet::append_big_endian(bytes, std::uint64_t{0x0102030405060708ULL});
    simnet::append_float32_big_endian(bytes, 1.0F);

    auto constexpr expected = std::array{
        simnet::Byte{0x12U},
        simnet::Byte{0x34U},
        simnet::Byte{0x10U},
        simnet::Byte{0x20U},
        simnet::Byte{0x30U},
        simnet::Byte{0x40U},
        simnet::Byte{0x01U},
        simnet::Byte{0x02U},
        simnet::Byte{0x03U},
        simnet::Byte{0x04U},
        simnet::Byte{0x05U},
        simnet::Byte{0x06U},
        simnet::Byte{0x07U},
        simnet::Byte{0x08U},
        simnet::Byte{0x3FU},
        simnet::Byte{0x80U},
        simnet::Byte{0x00U},
        simnet::Byte{0x00U},
    };
    CHECK(bytes == std::vector<simnet::Byte>{expected.begin(), expected.end()});

    auto offset = std::size_t{};
    auto value16 = std::uint16_t{};
    auto value32 = std::uint32_t{};
    auto value64 = std::uint64_t{};
    auto float32 = 0.0F;

    REQUIRE(simnet::read_big_endian(bytes, offset, value16));
    REQUIRE(simnet::read_big_endian(bytes, offset, value32));
    REQUIRE(simnet::read_big_endian(bytes, offset, value64));
    REQUIRE(simnet::read_float32_big_endian(bytes, offset, float32));

    CHECK(value16 == 0x1234U);
    CHECK(value32 == 0x10203040U);
    CHECK(value64 == 0x0102030405060708ULL);
    CHECK(float32 == 1.0F);
    CHECK(offset == bytes.size());
}

TEST_CASE("truncated core byte reads leave state unchanged", "[core][bytes]")
{
    auto const bytes = std::array{
        simnet::Byte{0x01U},
        simnet::Byte{0x02U},
        simnet::Byte{0x03U},
    };

    auto offset = std::size_t{};
    auto value = std::uint32_t{0xA5A5A5A5U};

    CHECK_FALSE(simnet::read_big_endian(bytes, offset, value));
    CHECK(offset == 0U);
    CHECK(value == 0xA5A5A5A5U);
}