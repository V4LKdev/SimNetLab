module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

/// @brief Unified per-entity record layout resolver and reader/writer.
module simnet.pipeline:records;

import :wire;
import :quantize;
import :bitpack;
import :types;
import simnet.core;
import simnet.snapshot;

namespace simnet::pipeline_records
{
    /// Record format resolved once per encode/decode call, out of the entity loop.
    struct RecordLayout
    {
        bool bitpacked {};
        bool quantized {};
        bool oct_heading {};
        std::uint32_t record_bytes {};
        Aabb3f bounds {};
    };

    /// Resolves the record layout for a given pipeline definition, determining the codec and techniques used.
    [[nodiscard]] RecordLayout resolve_record_layout(PipelineDefinition const& pipeline) noexcept
    {
        auto const bitpacked = has_all_flags(pipeline.techniques, PipelineTechniqueFlags::BitPacking);
        auto const quantized = has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Quantization);
        auto const oct_heading = has_all_flags(pipeline.techniques, PipelineTechniqueFlags::OctHeading);

        auto record_bytes = pipeline_wire::raw_record_bytes;
        if (bitpacked) {
            record_bytes = pipeline_wire::bitpacked_quantized_oct_record_bytes;
        } else if (oct_heading) {
            record_bytes = pipeline_wire::quantized_oct_record_bytes;
        } else if (quantized) {
            record_bytes = pipeline_wire::quantized_record_bytes;
        }

        return {
            .bitpacked = bitpacked,
            .quantized = quantized,
            .oct_heading = oct_heading,
            .record_bytes = record_bytes,
            .bounds = pipeline.quantization.position_bounds,
        };
    }

    /// Writes a 3D vector as three quantized 16-bit unsigned integers, given the specified bounds.
    void write_quantized_vec3(std::vector<Byte>& bytes, Vec3f value, Aabb3f bounds)
    {
        pipeline_wire::write_u16(bytes, pipeline_quantize::quantize_unorm16(value.x, bounds.min.x, bounds.max.x));
        pipeline_wire::write_u16(bytes, pipeline_quantize::quantize_unorm16(value.y, bounds.min.y, bounds.max.y));
        pipeline_wire::write_u16(bytes, pipeline_quantize::quantize_unorm16(value.z, bounds.min.z, bounds.max.z));
    }

    /// Writes a 3D heading vector as three quantized 16-bit signed integers in the range [-1, 1].
    void write_quantized_heading(std::vector<Byte>& bytes, Vec3f value)
    {
        pipeline_wire::write_u16(bytes, pipeline_quantize::quantize_snorm16(value.x));
        pipeline_wire::write_u16(bytes, pipeline_quantize::quantize_snorm16(value.y));
        pipeline_wire::write_u16(bytes, pipeline_quantize::quantize_snorm16(value.z));
    }

    /// Writes a 3D heading vector as two octant-encoded 16-bit unsigned integers.
    void write_oct_heading(std::vector<Byte>& bytes, Vec3f value)
    {
        auto const [x, y] = pipeline_quantize::encode_oct_heading(value);
        pipeline_wire::write_u16(bytes, x);
        pipeline_wire::write_u16(bytes, y);
    }

    /// Reads a 3D vector from three quantized 16-bit unsigned integers, given the specified bounds.
    /// Returns false on truncation.
    [[nodiscard]] bool read_quantized_vec3(
        ByteSpan bytes,
        std::size_t& offset,
        Aabb3f bounds,
        Vec3f& value
    )
    {
        auto x = std::uint16_t {};
        auto y = std::uint16_t {};
        auto z = std::uint16_t {};
        if (!pipeline_wire::read_u16(bytes, offset, x)
            || !pipeline_wire::read_u16(bytes, offset, y)
            || !pipeline_wire::read_u16(bytes, offset, z)) {
            return false;
        }

        value = {
            .x = pipeline_quantize::dequantize_unorm16(x, bounds.min.x, bounds.max.x),
            .y = pipeline_quantize::dequantize_unorm16(y, bounds.min.y, bounds.max.y),
            .z = pipeline_quantize::dequantize_unorm16(z, bounds.min.z, bounds.max.z),
        };
        return true;
    }

    /// Reads a 3D heading vector from three quantized 16-bit signed integers in the range [-1, 1].
    /// Returns false on truncation.
    [[nodiscard]] bool read_quantized_heading(
        ByteSpan bytes,
        std::size_t& offset,
        Vec3f& value
    )
    {
        auto x = std::uint16_t {};
        auto y = std::uint16_t {};
        auto z = std::uint16_t {};
        if (!pipeline_wire::read_u16(bytes, offset, x)
            || !pipeline_wire::read_u16(bytes, offset, y)
            || !pipeline_wire::read_u16(bytes, offset, z)) {
            return false;
        }

        value = normalize_or(
            {
                .x = pipeline_quantize::dequantize_snorm16(x),
                .y = pipeline_quantize::dequantize_snorm16(y),
                .z = pipeline_quantize::dequantize_snorm16(z),
            },
            { .x = 1.0F, .y = 0.0F, .z = 0.0F }
        );
        return true;
    }

    /// Reads a 3D heading vector from two octant-encoded 16-bit unsigned integers.
    /// Returns false on truncation.
    [[nodiscard]] bool read_oct_heading(
        ByteSpan bytes,
        std::size_t& offset,
        Vec3f& value
    )
    {
        auto x = std::uint16_t {};
        auto y = std::uint16_t {};
        if (!pipeline_wire::read_u16(bytes, offset, x)
            || !pipeline_wire::read_u16(bytes, offset, y)) {
            return false;
        }

        value = pipeline_quantize::decode_oct_heading(x, y);
        return true;
    }

    /// Writes one entity record in the bit-packed layout.
    void write_bitpacked_record(
        std::vector<Byte>& bytes,
        EntityNetId id,
        Vec3f position,
        Vec3f heading,
        std::uint8_t hue,
        Aabb3f bounds
    )
    {
        auto writer = pipeline_bitpack::BitWriter { .bytes = bytes };
        auto const [oct_x, oct_y] = pipeline_quantize::encode_oct_heading(heading);
        pipeline_bitpack::write_bits(writer, id, 32);
        pipeline_bitpack::write_bits(writer, pipeline_quantize::quantize_unorm16(position.x, bounds.min.x, bounds.max.x), 16);
        pipeline_bitpack::write_bits(writer, pipeline_quantize::quantize_unorm16(position.y, bounds.min.y, bounds.max.y), 16);
        pipeline_bitpack::write_bits(writer, pipeline_quantize::quantize_unorm16(position.z, bounds.min.z, bounds.max.z), 16);
        pipeline_bitpack::write_bits(writer, oct_x, 16);
        pipeline_bitpack::write_bits(writer, oct_y, 16);
        pipeline_bitpack::write_bits(writer, hue, 8);
        pipeline_bitpack::flush_bits(writer);
    }

    /// Reads one entity record in the bit-packed layout, returning false on truncation.
    [[nodiscard]] bool read_bitpacked_record(
        ByteSpan bytes,
        Aabb3f bounds,
        BoidState& boid
    )
    {
        auto reader = pipeline_bitpack::BitReader { .bytes = bytes };
        auto id = std::uint32_t {};
        auto px = std::uint32_t {};
        auto py = std::uint32_t {};
        auto pz = std::uint32_t {};
        auto hx = std::uint32_t {};
        auto hy = std::uint32_t {};
        auto hue = std::uint32_t {};
        if (!pipeline_bitpack::read_bits(reader, 32, id)
            || !pipeline_bitpack::read_bits(reader, 16, px)
            || !pipeline_bitpack::read_bits(reader, 16, py)
            || !pipeline_bitpack::read_bits(reader, 16, pz)
            || !pipeline_bitpack::read_bits(reader, 16, hx)
            || !pipeline_bitpack::read_bits(reader, 16, hy)
            || !pipeline_bitpack::read_bits(reader, 8, hue)) {
            return false;
        }

        boid.id = id;
        boid.position = {
            .x = pipeline_quantize::dequantize_unorm16(static_cast<std::uint16_t>(px), bounds.min.x, bounds.max.x),
            .y = pipeline_quantize::dequantize_unorm16(static_cast<std::uint16_t>(py), bounds.min.y, bounds.max.y),
            .z = pipeline_quantize::dequantize_unorm16(static_cast<std::uint16_t>(pz), bounds.min.z, bounds.max.z),
        };
        boid.heading = pipeline_quantize::decode_oct_heading(static_cast<std::uint16_t>(hx), static_cast<std::uint16_t>(hy));
        boid.hue = static_cast<std::uint8_t>(hue);
        return true;
    }

    /// Writes one entity record in the resolved layout.
    void write_record(
        std::vector<Byte>& bytes,
        RecordLayout const& layout,
        EntityNetId id,
        Vec3f position,
        Vec3f heading,
        std::uint8_t hue
    )
    {
        if (layout.bitpacked) {
            write_bitpacked_record(bytes, id, position, heading, hue, layout.bounds);
            return;
        }

        pipeline_wire::write_u32(bytes, id);
        if (layout.quantized) {
            write_quantized_vec3(bytes, position, layout.bounds);
            if (layout.oct_heading) {
                write_oct_heading(bytes, heading);
            } else {
                write_quantized_heading(bytes, heading);
            }
        } else {
            pipeline_wire::write_vec3(bytes, position);
            pipeline_wire::write_vec3(bytes, heading);
        }
        pipeline_wire::write_u8(bytes, hue);
    }

    /// Reads one entity record in the resolved layout, advancing offset. Returns false on truncation.
    [[nodiscard]] bool read_record(
        ByteSpan bytes,
        std::size_t& offset,
        RecordLayout const& layout,
        BoidState& boid
    )
    {
        if (layout.bitpacked) {
            auto const record_begin = offset;
            auto const record_end = record_begin + layout.record_bytes;
            if (record_end > bytes.size()) {
                return false;
            }
            if (!read_bitpacked_record(bytes.subspan(record_begin, layout.record_bytes), layout.bounds, boid)) {
                return false;
            }
            offset = record_end;
            return true;
        }

        if (!pipeline_wire::read_u32(bytes, offset, boid.id)) {
            return false;
        }
        if (layout.quantized) {
            if (!read_quantized_vec3(bytes, offset, layout.bounds, boid.position)) {
                return false;
            }
            if (layout.oct_heading) {
                if (!read_oct_heading(bytes, offset, boid.heading)) {
                    return false;
                }
            } else if (!read_quantized_heading(bytes, offset, boid.heading)) {
                return false;
            }
        } else if (!pipeline_wire::read_vec3(bytes, offset, boid.position)
            || !pipeline_wire::read_vec3(bytes, offset, boid.heading)) {
            return false;
        }
        return pipeline_wire::read_u8(bytes, offset, boid.hue);
    }
}
