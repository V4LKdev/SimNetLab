module;

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
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
import :messages;
import :types;
import simnet.core;
import simnet.snapshot;

namespace simnet::pipeline_records
{
    /// Record format resolved once per encode/decode call, out of the entity loop.
    struct RecordLayout
    {
        EntityRecordLayout name{EntityRecordLayout::Raw};
        bool bitpacked{};
        bool quantized{};
        bool oct_heading{};
        std::uint32_t record_bytes{};
        Aabb3f bounds{};
    };

    /// Exact wire tokens and complete logical value prepared from one source entity.
    struct PreparedRecord
    {
        EntityState canonical{};
        std::array<std::uint32_t, 3> position_tokens{};
        std::array<std::uint32_t, 3> heading_tokens{};
    };

    /// Resolves the record layout for a given pipeline definition.
    [[nodiscard]] RecordLayout resolve_record_layout(PipelineDefinition const& pipeline) noexcept
    {
        auto const bitpacked
            = has_all_flags(pipeline.techniques, PipelineTechniqueFlags::BitPacking);
        auto const quantized
            = has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Quantization);
        auto const oct_heading
            = has_all_flags(pipeline.techniques, PipelineTechniqueFlags::OctHeading);

        auto record_bytes = pipeline_wire::raw_record_bytes;
        auto name = EntityRecordLayout::Raw;
        if (bitpacked) {
            record_bytes = pipeline_wire::bitpacked_quantized_oct_record_bytes;
            name = EntityRecordLayout::BitPackedQuantizedOctHeading;
        } else if (oct_heading) {
            record_bytes = pipeline_wire::quantized_oct_record_bytes;
            name = EntityRecordLayout::QuantizedOctHeading;
        } else if (quantized) {
            record_bytes = pipeline_wire::quantized_record_bytes;
            name = EntityRecordLayout::Quantized;
        }

        return {
            .name = name,
            .bitpacked = bitpacked,
            .quantized = quantized,
            .oct_heading = oct_heading,
            .record_bytes = record_bytes,
            .bounds = pipeline.quantization.position_bounds,
        };
    }

    [[nodiscard]] RepresentationReport
    make_representation_report(RecordLayout const& layout) noexcept
    {
        return {
            .layout = layout.name,
            .record_bytes = layout.record_bytes,
        };
    }

    /// Prepares the only representation used by comparison, writing, and exact reconstruction.
    [[nodiscard]] PreparedRecord prepare_record(
        RecordLayout const& layout,
        EntityNetId id,
        EntityClassification classification,
        Vec3f position,
        Vec3f heading,
        std::uint8_t hue
    ) noexcept
    {
        auto prepared = PreparedRecord{};
        prepared.canonical.id = id;
        prepared.canonical.classification = classification;
        prepared.canonical.hue = hue;

        if (!layout.quantized) {
            prepared.position_tokens = {
                std::bit_cast<std::uint32_t>(position.x),
                std::bit_cast<std::uint32_t>(position.y),
                std::bit_cast<std::uint32_t>(position.z),
            };
            prepared.heading_tokens = {
                std::bit_cast<std::uint32_t>(heading.x),
                std::bit_cast<std::uint32_t>(heading.y),
                std::bit_cast<std::uint32_t>(heading.z),
            };
            prepared.canonical.position = {
                .x = std::bit_cast<float>(prepared.position_tokens[0]),
                .y = std::bit_cast<float>(prepared.position_tokens[1]),
                .z = std::bit_cast<float>(prepared.position_tokens[2]),
            };
            prepared.canonical.heading = {
                .x = std::bit_cast<float>(prepared.heading_tokens[0]),
                .y = std::bit_cast<float>(prepared.heading_tokens[1]),
                .z = std::bit_cast<float>(prepared.heading_tokens[2]),
            };
            return prepared;
        }

        prepared.position_tokens = {
            pipeline_quantize::quantize_unorm16(
                position.x,
                layout.bounds.min.x,
                layout.bounds.max.x
            ),
            pipeline_quantize::quantize_unorm16(
                position.y,
                layout.bounds.min.y,
                layout.bounds.max.y
            ),
            pipeline_quantize::quantize_unorm16(
                position.z,
                layout.bounds.min.z,
                layout.bounds.max.z
            ),
        };
        prepared.canonical.position = {
            .x = pipeline_quantize::dequantize_unorm16(
                static_cast<std::uint16_t>(prepared.position_tokens[0]),
                layout.bounds.min.x,
                layout.bounds.max.x
            ),
            .y = pipeline_quantize::dequantize_unorm16(
                static_cast<std::uint16_t>(prepared.position_tokens[1]),
                layout.bounds.min.y,
                layout.bounds.max.y
            ),
            .z = pipeline_quantize::dequantize_unorm16(
                static_cast<std::uint16_t>(prepared.position_tokens[2]),
                layout.bounds.min.z,
                layout.bounds.max.z
            ),
        };

        if (layout.oct_heading) {
            auto const [x, y] = pipeline_quantize::encode_oct_heading(heading);
            prepared.heading_tokens = {x, y, 0U};
            prepared.canonical.heading = pipeline_quantize::decode_oct_heading(x, y);
        } else {
            prepared.heading_tokens = {
                pipeline_quantize::quantize_snorm16(heading.x),
                pipeline_quantize::quantize_snorm16(heading.y),
                pipeline_quantize::quantize_snorm16(heading.z),
            };
            prepared.canonical.heading = normalize_or(
                {
                    .x = pipeline_quantize::dequantize_snorm16(
                        static_cast<std::uint16_t>(prepared.heading_tokens[0])
                    ),
                    .y = pipeline_quantize::dequantize_snorm16(
                        static_cast<std::uint16_t>(prepared.heading_tokens[1])
                    ),
                    .z = pipeline_quantize::dequantize_snorm16(
                        static_cast<std::uint16_t>(prepared.heading_tokens[2])
                    ),
                },
                {.x = 1.0F}
            );
        }
        return prepared;
    }

    /// Adds one produced record's source-to-canonical error without re-preparing its values.
    void observe_representation_quality(
        RepresentationReport& report,
        Vec3f source_position,
        Vec3f source_heading,
        PreparedRecord const& prepared
    ) noexcept
    {
        ++report.quality_sample_count;
        if (report.layout == EntityRecordLayout::Raw) {
            return;
        }

        auto const position_x
            = static_cast<double>(source_position.x) - prepared.canonical.position.x;
        auto const position_y
            = static_cast<double>(source_position.y) - prepared.canonical.position.y;
        auto const position_z
            = static_cast<double>(source_position.z) - prepared.canonical.position.z;
        auto const position_error = std::sqrt(
            position_x * position_x + position_y * position_y + position_z * position_z
        );
        report.position_error_sum += position_error;
        report.position_error_maximum = std::max(report.position_error_maximum, position_error);

        auto const normalized_source = normalize_or(source_heading, {.x = 1.0F});
        auto const normalized_canonical = normalize_or(prepared.canonical.heading, {.x = 1.0F});
        auto const cosine = std::clamp(
            static_cast<double>(dot(normalized_source, normalized_canonical)),
            -1.0,
            1.0
        );
        auto constexpr radians_to_degrees = 57.295779513082320876;
        auto const heading_error = std::acos(cosine) * radians_to_degrees;
        report.heading_angular_error_degrees_sum += heading_error;
        report.heading_angular_error_degrees_maximum
            = std::max(report.heading_angular_error_degrees_maximum, heading_error);
    }

    [[nodiscard]] bool same_binary32(float left, float right) noexcept
    {
        return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
    }

    /// Compares a prepared logical value with an already canonical retained baseline value.
    [[nodiscard]] bool same_canonical_state(
        EntityState const& current,
        WorldSnapshot const& baseline,
        std::size_t baseline_index
    ) noexcept
    {
        return current.id == baseline.ids[baseline_index]
            && current.classification == baseline.classifications[baseline_index]
            && same_binary32(current.position.x, baseline.positions[baseline_index].x)
            && same_binary32(current.position.y, baseline.positions[baseline_index].y)
            && same_binary32(current.position.z, baseline.positions[baseline_index].z)
            && same_binary32(current.heading.x, baseline.headings[baseline_index].x)
            && same_binary32(current.heading.y, baseline.headings[baseline_index].y)
            && same_binary32(current.heading.z, baseline.headings[baseline_index].z)
            && current.hue == baseline.hues[baseline_index];
    }

    /// Returns the semantic fields that differ from one exact retained canonical entity.
    [[nodiscard]] std::uint8_t canonical_field_mask(
        EntityState const& current,
        WorldSnapshot const& baseline,
        std::size_t baseline_index
    ) noexcept
    {
        auto mask = std::uint8_t{};
        if (current.classification != baseline.classifications[baseline_index]) {
            mask |= pipeline_wire::classification_field_mask;
        }
        if (!same_binary32(current.position.x, baseline.positions[baseline_index].x)
            || !same_binary32(current.position.y, baseline.positions[baseline_index].y)
            || !same_binary32(current.position.z, baseline.positions[baseline_index].z)) {
            mask |= pipeline_wire::position_field_mask;
        }
        if (!same_binary32(current.heading.x, baseline.headings[baseline_index].x)
            || !same_binary32(current.heading.y, baseline.headings[baseline_index].y)
            || !same_binary32(current.heading.z, baseline.headings[baseline_index].z)) {
            mask |= pipeline_wire::heading_field_mask;
        }
        if (current.hue != baseline.hues[baseline_index]) {
            mask |= pipeline_wire::hue_field_mask;
        }
        return mask;
    }

    [[nodiscard]] std::uint32_t
    selected_field_bytes(RecordLayout const& layout, std::uint8_t mask) noexcept
    {
        auto bytes = std::uint32_t{};
        if ((mask & pipeline_wire::classification_field_mask) != 0U) {
            bytes += pipeline_wire::u8_bytes;
        }
        if ((mask & pipeline_wire::position_field_mask) != 0U) {
            bytes += layout.quantized ? 3U * pipeline_wire::u16_bytes : pipeline_wire::vec3_bytes;
        }
        if ((mask & pipeline_wire::heading_field_mask) != 0U) {
            bytes += layout.quantized ? (layout.oct_heading ? 2U : 3U) * pipeline_wire::u16_bytes
                                      : pipeline_wire::vec3_bytes;
        }
        if ((mask & pipeline_wire::hue_field_mask) != 0U) {
            bytes += pipeline_wire::u8_bytes;
        }
        return bytes;
    }

    void observe_field_mask(DeltaReport& report, std::uint8_t mask) noexcept
    {
        if ((mask & pipeline_wire::classification_field_mask) != 0U) {
            ++report.classification_inclusion_count;
        }
        if ((mask & pipeline_wire::position_field_mask) != 0U) {
            ++report.position_inclusion_count;
        }
        if ((mask & pipeline_wire::heading_field_mask) != 0U) {
            ++report.heading_inclusion_count;
        }
        if ((mask & pipeline_wire::hue_field_mask) != 0U) {
            ++report.hue_inclusion_count;
        }
    }

    /// Reads a 3D vector from three quantized 16-bit unsigned integers, given the specified bounds.
    /// Returns false on truncation.
    [[nodiscard]] bool
    read_quantized_vec3(ByteSpan bytes, std::size_t& offset, Aabb3f bounds, Vec3f& value)
    {
        auto x = std::uint16_t{};
        auto y = std::uint16_t{};
        auto z = std::uint16_t{};
        if (!pipeline_wire::read_u16(bytes, offset, x) || !pipeline_wire::read_u16(bytes, offset, y)
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
    [[nodiscard]] bool read_quantized_heading(ByteSpan bytes, std::size_t& offset, Vec3f& value)
    {
        auto x = std::uint16_t{};
        auto y = std::uint16_t{};
        auto z = std::uint16_t{};
        if (!pipeline_wire::read_u16(bytes, offset, x) || !pipeline_wire::read_u16(bytes, offset, y)
            || !pipeline_wire::read_u16(bytes, offset, z)) {
            return false;
        }

        value = normalize_or(
            {
                .x = pipeline_quantize::dequantize_snorm16(x),
                .y = pipeline_quantize::dequantize_snorm16(y),
                .z = pipeline_quantize::dequantize_snorm16(z),
            },
            {.x = 1.0F, .y = 0.0F, .z = 0.0F}
        );
        return true;
    }

    /// Reads a 3D heading vector from two octant-encoded 16-bit unsigned integers.
    /// Returns false on truncation.
    [[nodiscard]] bool read_oct_heading(ByteSpan bytes, std::size_t& offset, Vec3f& value)
    {
        auto x = std::uint16_t{};
        auto y = std::uint16_t{};
        if (!pipeline_wire::read_u16(bytes, offset, x)
            || !pipeline_wire::read_u16(bytes, offset, y)) {
            return false;
        }

        value = pipeline_quantize::decode_oct_heading(x, y);
        return true;
    }

    /// Writes one prepared entity record in the bit-packed layout.
    void write_bitpacked_record(std::vector<Byte>& bytes, PreparedRecord const& prepared)
    {
        auto writer = pipeline_bitpack::BitWriter{.bytes = bytes};
        pipeline_bitpack::write_bits(writer, prepared.canonical.id, 32);
        pipeline_bitpack::write_bits(writer, prepared.canonical.classification.value(), 8);
        pipeline_bitpack::write_bits(writer, prepared.position_tokens[0], 16);
        pipeline_bitpack::write_bits(writer, prepared.position_tokens[1], 16);
        pipeline_bitpack::write_bits(writer, prepared.position_tokens[2], 16);
        pipeline_bitpack::write_bits(writer, prepared.heading_tokens[0], 16);
        pipeline_bitpack::write_bits(writer, prepared.heading_tokens[1], 16);
        pipeline_bitpack::write_bits(writer, prepared.canonical.hue, 8);
        pipeline_bitpack::flush_bits(writer);
    }

    /// Reads one entity record in the bit-packed layout, returning false on truncation.
    [[nodiscard]] bool read_bitpacked_record(ByteSpan bytes, Aabb3f bounds, EntityState& boid)
    {
        auto reader = pipeline_bitpack::BitReader{.bytes = bytes};
        auto id = std::uint32_t{};
        auto classification = std::uint32_t{};
        auto px = std::uint32_t{};
        auto py = std::uint32_t{};
        auto pz = std::uint32_t{};
        auto hx = std::uint32_t{};
        auto hy = std::uint32_t{};
        auto hue = std::uint32_t{};
        if (!pipeline_bitpack::read_bits(reader, 32, id)
            || !pipeline_bitpack::read_bits(reader, 8, classification)
            || !pipeline_bitpack::read_bits(reader, 16, px)
            || !pipeline_bitpack::read_bits(reader, 16, py)
            || !pipeline_bitpack::read_bits(reader, 16, pz)
            || !pipeline_bitpack::read_bits(reader, 16, hx)
            || !pipeline_bitpack::read_bits(reader, 16, hy)
            || !pipeline_bitpack::read_bits(reader, 8, hue)) {
            return false;
        }

        boid.id = id;
        boid.classification = EntityClassification{static_cast<std::uint8_t>(classification)};
        boid.position = {
            .x = pipeline_quantize::dequantize_unorm16(
                static_cast<std::uint16_t>(px),
                bounds.min.x,
                bounds.max.x
            ),
            .y = pipeline_quantize::dequantize_unorm16(
                static_cast<std::uint16_t>(py),
                bounds.min.y,
                bounds.max.y
            ),
            .z = pipeline_quantize::dequantize_unorm16(
                static_cast<std::uint16_t>(pz),
                bounds.min.z,
                bounds.max.z
            ),
        };
        boid.heading = pipeline_quantize::decode_oct_heading(
            static_cast<std::uint16_t>(hx),
            static_cast<std::uint16_t>(hy)
        );
        boid.hue = static_cast<std::uint8_t>(hue);
        return true;
    }

    /// Writes the exact tokens from one prepared record.
    void write_prepared_record(
        std::vector<Byte>& bytes,
        RecordLayout const& layout,
        PreparedRecord const& prepared
    )
    {
        if (layout.bitpacked) {
            write_bitpacked_record(bytes, prepared);
            return;
        }

        pipeline_wire::write_u32(bytes, prepared.canonical.id);
        pipeline_wire::write_u8(bytes, prepared.canonical.classification.value());
        if (layout.quantized) {
            pipeline_wire::write_u16(
                bytes,
                static_cast<std::uint16_t>(prepared.position_tokens[0])
            );
            pipeline_wire::write_u16(
                bytes,
                static_cast<std::uint16_t>(prepared.position_tokens[1])
            );
            pipeline_wire::write_u16(
                bytes,
                static_cast<std::uint16_t>(prepared.position_tokens[2])
            );
            if (layout.oct_heading) {
                pipeline_wire::write_u16(
                    bytes,
                    static_cast<std::uint16_t>(prepared.heading_tokens[0])
                );
                pipeline_wire::write_u16(
                    bytes,
                    static_cast<std::uint16_t>(prepared.heading_tokens[1])
                );
            } else {
                pipeline_wire::write_u16(
                    bytes,
                    static_cast<std::uint16_t>(prepared.heading_tokens[0])
                );
                pipeline_wire::write_u16(
                    bytes,
                    static_cast<std::uint16_t>(prepared.heading_tokens[1])
                );
                pipeline_wire::write_u16(
                    bytes,
                    static_cast<std::uint16_t>(prepared.heading_tokens[2])
                );
            }
        } else {
            pipeline_wire::write_u32(bytes, prepared.position_tokens[0]);
            pipeline_wire::write_u32(bytes, prepared.position_tokens[1]);
            pipeline_wire::write_u32(bytes, prepared.position_tokens[2]);
            pipeline_wire::write_u32(bytes, prepared.heading_tokens[0]);
            pipeline_wire::write_u32(bytes, prepared.heading_tokens[1]);
            pipeline_wire::write_u32(bytes, prepared.heading_tokens[2]);
        }
        pipeline_wire::write_u8(bytes, prepared.canonical.hue);
    }

    /// Writes selected prepared canonical fields without an entity ID.
    void write_prepared_fields(
        std::vector<Byte>& bytes,
        RecordLayout const& layout,
        PreparedRecord const& prepared,
        std::uint8_t mask
    )
    {
        if ((mask & pipeline_wire::classification_field_mask) != 0U) {
            pipeline_wire::write_u8(bytes, prepared.canonical.classification.value());
        }
        if ((mask & pipeline_wire::position_field_mask) != 0U) {
            if (layout.quantized) {
                for (auto const token : prepared.position_tokens) {
                    pipeline_wire::write_u16(bytes, static_cast<std::uint16_t>(token));
                }
            } else {
                for (auto const token : prepared.position_tokens) {
                    pipeline_wire::write_u32(bytes, token);
                }
            }
        }
        if ((mask & pipeline_wire::heading_field_mask) != 0U) {
            if (layout.quantized) {
                auto const token_count = layout.oct_heading ? 2U : 3U;
                for (auto index = std::size_t{}; index < token_count; ++index) {
                    pipeline_wire::write_u16(
                        bytes,
                        static_cast<std::uint16_t>(prepared.heading_tokens[index])
                    );
                }
            } else {
                for (auto const token : prepared.heading_tokens) {
                    pipeline_wire::write_u32(bytes, token);
                }
            }
        }
        if ((mask & pipeline_wire::hue_field_mask) != 0U) {
            pipeline_wire::write_u8(bytes, prepared.canonical.hue);
        }
    }

    void write_masked_record(
        std::vector<Byte>& bytes,
        RecordLayout const& layout,
        PreparedRecord const& prepared,
        std::uint8_t selector
    )
    {
        pipeline_wire::write_u32(bytes, prepared.canonical.id);
        pipeline_wire::write_u8(bytes, selector);
        auto const fields = selector == pipeline_wire::spawn_record_selector
            ? pipeline_wire::existing_field_mask
            : selector;
        write_prepared_fields(bytes, layout, prepared, fields);
    }

    /// Reads selected canonical fields into a complete caller-initialized entity.
    [[nodiscard]] bool read_selected_fields(
        ByteSpan bytes,
        std::size_t& offset,
        RecordLayout const& layout,
        std::uint8_t mask,
        EntityState& entity
    )
    {
        if ((mask & pipeline_wire::classification_field_mask) != 0U) {
            auto classification = std::uint8_t{};
            if (!pipeline_wire::read_u8(bytes, offset, classification)) {
                return false;
            }
            entity.classification = EntityClassification{classification};
        }
        if ((mask & pipeline_wire::position_field_mask) != 0U) {
            if (layout.quantized) {
                if (!read_quantized_vec3(bytes, offset, layout.bounds, entity.position)) {
                    return false;
                }
            } else if (!pipeline_wire::read_vec3(bytes, offset, entity.position)) {
                return false;
            }
        }
        if ((mask & pipeline_wire::heading_field_mask) != 0U) {
            if (layout.quantized) {
                if (layout.oct_heading) {
                    if (!read_oct_heading(bytes, offset, entity.heading)) {
                        return false;
                    }
                } else if (!read_quantized_heading(bytes, offset, entity.heading)) {
                    return false;
                }
            } else if (!pipeline_wire::read_vec3(bytes, offset, entity.heading)) {
                return false;
            }
        }
        if ((mask & pipeline_wire::hue_field_mask) != 0U) {
            return pipeline_wire::read_u8(bytes, offset, entity.hue);
        }
        return true;
    }

    /// Reads one entity record in the resolved layout, advancing offset. Returns false on truncation.
    [[nodiscard]] bool
    read_record(ByteSpan bytes, std::size_t& offset, RecordLayout const& layout, EntityState& boid)
    {
        if (layout.bitpacked) {
            auto const record_begin = offset;
            auto const record_end = record_begin + layout.record_bytes;
            if (record_end > bytes.size()) {
                return false;
            }
            if (!read_bitpacked_record(
                    bytes.subspan(record_begin, layout.record_bytes),
                    layout.bounds,
                    boid
                )) {
                return false;
            }
            offset = record_end;
            return true;
        }

        if (!pipeline_wire::read_u32(bytes, offset, boid.id)) {
            return false;
        }
        auto classification = std::uint8_t{};
        if (!pipeline_wire::read_u8(bytes, offset, classification)) {
            return false;
        }
        boid.classification = EntityClassification{classification};
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
        } else if (
            !pipeline_wire::read_vec3(bytes, offset, boid.position)
            || !pipeline_wire::read_vec3(bytes, offset, boid.heading)
        ) {
            return false;
        }
        return pipeline_wire::read_u8(bytes, offset, boid.hue);
    }
}
