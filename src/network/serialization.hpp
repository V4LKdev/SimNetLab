#pragma once
#include <cstdint>

namespace simnet::net {
    // --- Endianness helpers ---
    // All in Big Endian
    inline void write_u8(std::uint8_t *dest, const std::uint8_t value) noexcept
    {
        *dest = value;
    }

    inline void write_u16(std::uint8_t *dest, const std::uint16_t value) noexcept
    {
        dest[0] = static_cast<std::uint8_t>(value >> 8u) & 0xFFu;
        dest[1] = static_cast<std::uint8_t>(value & 0xFFu);
    }

    inline void write_u32(std::uint8_t *dest, const std::uint32_t value) noexcept
    {
        dest[0] = static_cast<std::uint8_t>(value >> 24u) & 0xFFu;
        dest[1] = static_cast<std::uint8_t>(value >> 16u) & 0xFFu;
        dest[2] = static_cast<std::uint8_t>(value >> 8u) & 0xFFu;
        dest[3] = static_cast<std::uint8_t>(value & 0xFFu);
    }

    inline std::uint8_t read_u8(const std::uint8_t *src) noexcept
    {
        return *src;
    }

    inline std::uint16_t read_u16(const std::uint8_t *src) noexcept
    {
        return (static_cast<std::uint16_t>(src[0]) << 8u) | static_cast<std::uint16_t>(src[1]);
    }

    inline std::uint32_t read_u32(const std::uint8_t *src) noexcept
    {
        return (static_cast<std::uint32_t>(src[0]) << 24u) |
               (static_cast<std::uint32_t>(src[1]) << 16u) |
               (static_cast<std::uint32_t>(src[2]) << 8u) |
               static_cast<std::uint32_t>(src[3]);
    }
}
