module;

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

/// @brief Core byte types and network-order operations.
export module simnet.core:bytes;

export namespace simnet
{
    /// Raw byte value.
    using Byte = std::byte;

    /// Non-owning immutable byte view. The caller keeps the referenced storage alive.
    using ByteSpan = std::span<Byte const>;

    inline void append_byte(std::vector<Byte>& bytes, std::uint8_t value)
    {
        bytes.push_back(static_cast<Byte>(value));
    }

    /// Appends an unsigned integer in big-endian byte order.
    inline void append_big_endian(std::vector<Byte>& bytes, std::uint16_t value)
    {
        append_byte(bytes, static_cast<std::uint8_t>(value >> 8U));
        append_byte(bytes, static_cast<std::uint8_t>(value));
    }

    /// Appends an unsigned integer in big-endian byte order.
    inline void append_big_endian(std::vector<Byte>& bytes, std::uint32_t value)
    {
        append_byte(bytes, static_cast<std::uint8_t>(value >> 24U));
        append_byte(bytes, static_cast<std::uint8_t>(value >> 16U));
        append_byte(bytes, static_cast<std::uint8_t>(value >> 8U));
        append_byte(bytes, static_cast<std::uint8_t>(value));
    }

    /// Appends an unsigned integer in big-endian byte order.
    inline void append_big_endian(std::vector<Byte>& bytes, std::uint64_t value)
    {
        append_big_endian(bytes, static_cast<std::uint32_t>(value >> 32U));
        append_big_endian(bytes, static_cast<std::uint32_t>(value));
    }

    /// Appends an IEEE-754 binary32 value in big-endian byte order.
    inline void append_float32_big_endian(std::vector<Byte>& bytes, float value)
    {
        static_assert(
            std::numeric_limits<float>::is_iec559 && sizeof(float) == sizeof(std::uint32_t)
        );
        append_big_endian(bytes, std::bit_cast<std::uint32_t>(value));
    }

    /// Reads one byte. Failure leaves the offset and destination unchanged.
    [[nodiscard]] inline bool
    read_byte(ByteSpan bytes, std::size_t& offset, std::uint8_t& value) noexcept
    {
        if (offset >= bytes.size())
        {
            return false;
        }

        auto const decoded = std::to_integer<std::uint8_t>(bytes[offset]);
        ++offset;
        value = decoded;
        return true;
    }

    /// Reads a big-endian integer. Failure leaves the offset and destination unchanged.
    [[nodiscard]] inline bool
    read_big_endian(ByteSpan bytes, std::size_t& offset, std::uint16_t& value) noexcept
    {
        if (offset > bytes.size() || bytes.size() - offset < sizeof(value))
        {
            return false;
        }

        auto const decoded = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
            static_cast<std::uint16_t>(bytes[offset + 1U])
        );
        offset += sizeof(value);
        value = decoded;
        return true;
    }

    /// Reads a big-endian integer. Failure leaves the offset and destination unchanged.
    [[nodiscard]] inline bool
    read_big_endian(ByteSpan bytes, std::size_t& offset, std::uint32_t& value) noexcept
    {
        if (offset > bytes.size() || bytes.size() - offset < sizeof(value))
        {
            return false;
        }

        auto const decoded = (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
                             (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
                             (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
                             static_cast<std::uint32_t>(bytes[offset + 3U]);
        offset += sizeof(value);
        value = decoded;
        return true;
    }

    /// Reads a big-endian integer. Failure leaves the offset and destination unchanged.
    [[nodiscard]] inline bool
    read_big_endian(ByteSpan bytes, std::size_t& offset, std::uint64_t& value) noexcept
    {
        if (offset > bytes.size() || bytes.size() - offset < sizeof(value))
        {
            return false;
        }

        auto const decoded = (static_cast<std::uint64_t>(bytes[offset]) << 56U) |
                             (static_cast<std::uint64_t>(bytes[offset + 1U]) << 48U) |
                             (static_cast<std::uint64_t>(bytes[offset + 2U]) << 40U) |
                             (static_cast<std::uint64_t>(bytes[offset + 3U]) << 32U) |
                             (static_cast<std::uint64_t>(bytes[offset + 4U]) << 24U) |
                             (static_cast<std::uint64_t>(bytes[offset + 5U]) << 16U) |
                             (static_cast<std::uint64_t>(bytes[offset + 6U]) << 8U) |
                             static_cast<std::uint64_t>(bytes[offset + 7U]);
        offset += sizeof(value);
        value = decoded;
        return true;
    }

    /// Reads an IEEE-754 binary32 value. Failure leaves the offset and destination unchanged.
    [[nodiscard]] inline bool
    read_float32_big_endian(ByteSpan bytes, std::size_t& offset, float& value) noexcept
    {
        static_assert(
            std::numeric_limits<float>::is_iec559 && sizeof(float) == sizeof(std::uint32_t)
        );
        auto bits = std::uint32_t{};
        auto next_offset = offset;
        if (!read_big_endian(bytes, next_offset, bits))
        {
            return false;
        }

        offset = next_offset;
        value = std::bit_cast<float>(bits);
        return true;
    }
}
