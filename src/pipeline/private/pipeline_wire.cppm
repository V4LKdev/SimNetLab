module;

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

module simnet.pipeline:wire;

import :types;
import simnet.core;
import simnet.snapshot;

namespace simnet::pipeline_wire
{
    inline constexpr std::uint32_t packet_magic = 0x534E504Cu;
    inline constexpr std::uint16_t protocol_version = 1;
    inline constexpr std::uint16_t schema_version = 1;
    inline constexpr std::uint32_t u8_bytes = 1;
    inline constexpr std::uint32_t u16_bytes = 2;
    inline constexpr std::uint32_t u32_bytes = 4;
    inline constexpr std::uint32_t u64_bytes = 8;
    inline constexpr std::uint32_t f32_bytes = 4;
    inline constexpr std::uint32_t vec3_bytes = 3 * f32_bytes;
    inline constexpr std::uint32_t raw_record_bytes = u32_bytes + vec3_bytes + vec3_bytes + u8_bytes;
    inline constexpr std::uint32_t delete_record_bytes = u32_bytes;
    inline constexpr std::uint32_t header_bytes = u32_bytes + u16_bytes + u16_bytes + u8_bytes + u8_bytes
        + u32_bytes + u64_bytes + u32_bytes + u32_bytes + u32_bytes + u32_bytes + u32_bytes;

    static_assert(raw_record_bytes == 29);
    static_assert(delete_record_bytes == 4);
    static_assert(header_bytes == 42);

    /// Private packet header serialized field-by-field in network byte order.
    struct PacketHeader
    {
        std::uint32_t magic {};
        std::uint16_t protocol {};
        std::uint16_t schema {};
        PipelinePacketKind packet_kind { PipelinePacketKind::Snapshot };
        SnapshotKind snapshot_kind { SnapshotKind::FullReplace };
        PipelinePacketFlags flags { PipelinePacketFlags::None };
        Tick tick {};
        SequenceId sequence {};
        SequenceId baseline_sequence {};
        std::uint32_t upsert_count {};
        std::uint32_t delete_count {};
        std::uint32_t payload_bytes {};
    };

    void write_u8(std::vector<std::byte>& bytes, std::uint8_t value)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }

    void write_u16(std::vector<std::byte>& bytes, std::uint16_t value)
    {
        write_u8(bytes, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        write_u8(bytes, static_cast<std::uint8_t>(value & 0xFFU));
    }

    void write_u32(std::vector<std::byte>& bytes, std::uint32_t value)
    {
        write_u8(bytes, static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
        write_u8(bytes, static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
        write_u8(bytes, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        write_u8(bytes, static_cast<std::uint8_t>(value & 0xFFU));
    }

    void write_u64(std::vector<std::byte>& bytes, std::uint64_t value)
    {
        write_u32(bytes, static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFULL));
        write_u32(bytes, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    }

    void write_f32(std::vector<std::byte>& bytes, float value)
    {
        write_u32(bytes, std::bit_cast<std::uint32_t>(value));
    }

    void write_header(std::vector<std::byte>& bytes, PacketHeader const& header)
    {
        write_u32(bytes, header.magic);
        write_u16(bytes, header.protocol);
        write_u16(bytes, header.schema);
        write_u8(bytes, static_cast<std::uint8_t>(header.packet_kind));
        write_u8(bytes, static_cast<std::uint8_t>(header.snapshot_kind));
        write_u32(bytes, static_cast<std::uint32_t>(header.flags));
        write_u64(bytes, header.tick);
        write_u32(bytes, header.sequence);
        write_u32(bytes, header.baseline_sequence);
        write_u32(bytes, header.upsert_count);
        write_u32(bytes, header.delete_count);
        write_u32(bytes, header.payload_bytes);
    }

    void write_vec3(std::vector<std::byte>& bytes, Vec3f value)
    {
        write_f32(bytes, value.x);
        write_f32(bytes, value.y);
        write_f32(bytes, value.z);
    }

    bool read_u8(std::span<const std::byte> bytes, std::size_t& offset, std::uint8_t& value)
    {
        if (offset + 1U > bytes.size()) {
            return false;
        }
        value = std::to_integer<std::uint8_t>(bytes[offset]);
        ++offset;
        return true;
    }

    bool read_u16(std::span<const std::byte> bytes, std::size_t& offset, std::uint16_t& value)
    {
        auto b0 = std::uint8_t {};
        auto b1 = std::uint8_t {};
        if (!read_u8(bytes, offset, b0) || !read_u8(bytes, offset, b1)) {
            return false;
        }
        value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(b0) << 8U) | b1);
        return true;
    }

    bool read_u32(std::span<const std::byte> bytes, std::size_t& offset, std::uint32_t& value)
    {
        auto b0 = std::uint8_t {};
        auto b1 = std::uint8_t {};
        auto b2 = std::uint8_t {};
        auto b3 = std::uint8_t {};
        if (!read_u8(bytes, offset, b0)
            || !read_u8(bytes, offset, b1)
            || !read_u8(bytes, offset, b2)
            || !read_u8(bytes, offset, b3)) {
            return false;
        }
        value = (static_cast<std::uint32_t>(b0) << 24U)
            | (static_cast<std::uint32_t>(b1) << 16U)
            | (static_cast<std::uint32_t>(b2) << 8U)
            | static_cast<std::uint32_t>(b3);
        return true;
    }

    bool read_u64(std::span<const std::byte> bytes, std::size_t& offset, std::uint64_t& value)
    {
        auto hi = std::uint32_t {};
        auto lo = std::uint32_t {};
        if (!read_u32(bytes, offset, hi) || !read_u32(bytes, offset, lo)) {
            return false;
        }
        value = (static_cast<std::uint64_t>(hi) << 32U) | lo;
        return true;
    }

    bool read_f32(std::span<const std::byte> bytes, std::size_t& offset, float& value)
    {
        auto bits = std::uint32_t {};
        if (!read_u32(bytes, offset, bits)) {
            return false;
        }
        value = std::bit_cast<float>(bits);
        return true;
    }

    bool read_header(std::span<const std::byte> bytes, PacketHeader& header)
    {
        auto offset = std::size_t {};
        auto packet_kind = std::uint8_t {};
        auto snapshot_kind = std::uint8_t {};
        auto flags = std::uint32_t {};

        if (!read_u32(bytes, offset, header.magic)
            || !read_u16(bytes, offset, header.protocol)
            || !read_u16(bytes, offset, header.schema)
            || !read_u8(bytes, offset, packet_kind)
            || !read_u8(bytes, offset, snapshot_kind)
            || !read_u32(bytes, offset, flags)
            || !read_u64(bytes, offset, header.tick)
            || !read_u32(bytes, offset, header.sequence)
            || !read_u32(bytes, offset, header.baseline_sequence)
            || !read_u32(bytes, offset, header.upsert_count)
            || !read_u32(bytes, offset, header.delete_count)
            || !read_u32(bytes, offset, header.payload_bytes)) {
            return false;
        }

        header.packet_kind = static_cast<PipelinePacketKind>(packet_kind);
        header.snapshot_kind = static_cast<SnapshotKind>(snapshot_kind);
        header.flags = static_cast<PipelinePacketFlags>(flags);
        return true;
    }

    bool read_vec3(std::span<const std::byte> bytes, std::size_t& offset, Vec3f& value)
    {
        return read_f32(bytes, offset, value.x)
            && read_f32(bytes, offset, value.y)
            && read_f32(bytes, offset, value.z);
    }
}
