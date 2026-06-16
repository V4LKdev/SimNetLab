#pragma once
#include <cstdint>
#include <cstring>

/**
 *  Lowest-level , endian-aware reading and writing of primitive types
 *
 *  All values are written in network byte order (big-endian)
 */
namespace simnet::net {
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

    inline void write_float(std::uint8_t *dest, const float value) noexcept
    {
        // reinterpret the float bits as uint32_t and use big-endian write
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        write_u32(dest, bits);
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

    inline float read_float(const std::uint8_t *src) noexcept
    {
        const std::uint32_t bits = read_u32(src);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
}
