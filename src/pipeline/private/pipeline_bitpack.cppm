module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

/// @brief MSB-first bit stream primitives for packed record layouts.
module simnet.pipeline:bitpack;

import simnet.core;

namespace simnet::pipeline_bitpack
{
    /// Bit-level append cursor over a byte vector. MSB-first within each byte.
    struct BitWriter
    {
        /// Reference to the byte vector to append to.
        std::vector<Byte>& bytes;
        /// Scratch byte for accumulating bits before flushing to the vector.
        std::uint8_t scratch {};
        /// Number of bits currently stored in the scratch byte (0-7).
        std::uint8_t used_bits {};
    };

    /// Writes 'bit-count' bits from the MSB of 'value'.
    void write_bits(BitWriter& writer, std::uint32_t value, std::uint8_t bit_count)
    {
        for (auto bit_index = bit_count; bit_index > 0U; --bit_index) {

            auto const bit = static_cast<std::uint8_t>((value >> (bit_index - 1U)) & 1U);
            writer.scratch = static_cast<std::uint8_t>((writer.scratch << 1U) | bit);

            ++writer.used_bits;

            if (writer.used_bits == 8U) {
                writer.bytes.push_back(static_cast<Byte>(writer.scratch));
                writer.scratch = 0;
                writer.used_bits = 0;
            }
        }
    }

    /// Flushes any remaining bits in the scratch byte to the vector, padding with zeros if necessary.
    void flush_bits(BitWriter& writer)
    {
        if (writer.used_bits == 0U) {
            return;
        }
        
        writer.scratch = static_cast<std::uint8_t>(writer.scratch << (8U - writer.used_bits));
        writer.bytes.push_back(static_cast<Byte>(writer.scratch));
        writer.scratch = 0;
        writer.used_bits = 0;
    }

    /// Bit-level read cursor over a byte span. MSB-first within each byte.
    struct BitReader
    {
        /// Reference to the byte span to read from.
        ByteSpan bytes;
        /// Current bit offset from the start of the span (0-based).
        std::size_t bit_offset {};
    };

    /// Reads 'bit_count' bits from the current bit offset, storing the result in 'value'.
    /// Advances the bit offset by 'bit_count'. Returns false if the read would overflow.
    [[nodiscard]] bool read_bits(BitReader& reader, std::uint8_t bit_count, std::uint32_t& value)
    {
        if (reader.bit_offset + bit_count > reader.bytes.size() * 8U) {
            return false;
        }

        value = 0;
        for (std::uint8_t index = 0; index < bit_count; ++index) {

            auto const absolute_bit = reader.bit_offset + index;
            auto const byte_index = absolute_bit / 8U;
            auto const bit_in_byte = 7U - (absolute_bit % 8U);
            auto const byte = std::to_integer<std::uint8_t>(reader.bytes[byte_index]);

            value = (value << 1U) | ((byte >> bit_in_byte) & 1U);
        }
        reader.bit_offset += bit_count;
        return true;
    }
}
