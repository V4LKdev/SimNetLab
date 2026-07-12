module;

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module simnet.pipeline;

import :codec;
import :types;
import :wire;
import simnet.core;
import simnet.snapshot;

namespace
{
    void require_raw_snapshot(simnet::PipelineDefinition const& pipeline)
    {
        if (pipeline.profile != simnet::PipelineProfileKind::RawSnapshot) {
            throw std::runtime_error("unsupported pipeline profile");
        }
        if (pipeline.codec != simnet::CodecKind::ByteAligned && pipeline.codec != simnet::CodecKind::BitPacked) {
            throw std::runtime_error("unsupported raw snapshot codec");
        }
        auto constexpr supported_techniques = static_cast<std::uint32_t>(
            simnet::PipelineTechniqueFlags::SendInterval
                | simnet::PipelineTechniqueFlags::Incremental
                | simnet::PipelineTechniqueFlags::Quantization
                | simnet::PipelineTechniqueFlags::OctHeading
                | simnet::PipelineTechniqueFlags::Delta
        );
        auto const requested_techniques = static_cast<std::uint32_t>(pipeline.techniques);
        auto const unsupported_techniques = requested_techniques & ~supported_techniques;
        if (unsupported_techniques != 0U) {
            throw std::runtime_error("raw snapshot does not support requested techniques");
        }
        if (simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::Incremental)
            && simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::Quantization)) {
            throw std::runtime_error("raw snapshot quantization does not support incremental packets yet");
        }
        if (simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::Incremental)
            && simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::Delta)) {
            throw std::runtime_error("raw snapshot does not support combining incremental and delta selection");
        }
        if (simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::OctHeading)
            && !simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::Quantization)) {
            throw std::runtime_error("oct heading requires quantization");
        }
        if (pipeline.codec == simnet::CodecKind::BitPacked
            && (!simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::Quantization)
                || !simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::OctHeading))) {
            throw std::runtime_error("bit-packed raw snapshot requires quantization and oct heading");
        }
    }

    void require_snapshot(simnet::WorldSnapshot const* snapshot, char const* what)
    {
        if (snapshot == nullptr) {
            throw std::runtime_error(std::string { what } + " is null");
        }

        auto const validation = simnet::validate_world_snapshot(*snapshot);
        if (!validation.valid) {
            throw std::runtime_error(std::string { what } + " is invalid: " + validation.message);
        }
    }

    void require_u32_count(std::size_t count, char const* what)
    {
        if (count > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error(std::string { what } + " exceeds uint32 range");
        }
    }

    void require_send_interval_settings(simnet::PipelineDefinition const& pipeline)
    {
        if (simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::SendInterval)
            && pipeline.send_interval.interval_ticks == 0U) {
            throw std::runtime_error("send interval tick count must be greater than 0");
        }
    }

    void require_incremental_settings(simnet::PipelineDefinition const& pipeline)
    {
        if (simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::Incremental)
            && pipeline.incremental.max_entities_per_packet == 0U) {
            throw std::runtime_error("incremental max entities per packet must be greater than 0");
        }
    }

    void require_quantization_settings(simnet::PipelineDefinition const& pipeline)
    {
        if (!simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::Quantization)) {
            return;
        }

        auto const bounds = pipeline.quantization.position_bounds;
        if (!simnet::is_finite(bounds.min) || !simnet::is_finite(bounds.max)) {
            throw std::runtime_error("quantization bounds must be finite");
        }
        if (bounds.min.x >= bounds.max.x || bounds.min.y >= bounds.max.y || bounds.min.z >= bounds.max.z) {
            throw std::runtime_error("quantization bounds must have positive extent");
        }
    }

    [[nodiscard]] bool is_incremental(simnet::PipelineDefinition const& pipeline) noexcept
    {
        return simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::Incremental);
    }

    [[nodiscard]] bool is_quantized(simnet::PipelineDefinition const& pipeline) noexcept
    {
        return simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::Quantization);
    }

    [[nodiscard]] bool is_delta(simnet::PipelineDefinition const& pipeline) noexcept
    {
        return simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::Delta);
    }

    [[nodiscard]] bool uses_oct_heading(simnet::PipelineDefinition const& pipeline) noexcept
    {
        return simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::OctHeading);
    }

    [[nodiscard]] bool is_bitpacked(simnet::PipelineDefinition const& pipeline) noexcept
    {
        return pipeline.codec == simnet::CodecKind::BitPacked;
    }

    struct DecodeSignatureBuilder
    {
        std::uint64_t value { 14695981039346656037ULL };
    };

    void update_signature_byte(DecodeSignatureBuilder& signature, std::uint8_t value) noexcept
    {
        signature.value ^= value;
        signature.value *= 1099511628211ULL;
    }

    void update_signature_u8(DecodeSignatureBuilder& signature, std::uint8_t value) noexcept
    {
        update_signature_byte(signature, value);
    }

    void update_signature_u16(DecodeSignatureBuilder& signature, std::uint16_t value) noexcept
    {
        update_signature_byte(signature, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        update_signature_byte(signature, static_cast<std::uint8_t>(value & 0xFFU));
    }

    void update_signature_u32(DecodeSignatureBuilder& signature, std::uint32_t value) noexcept
    {
        update_signature_byte(signature, static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
        update_signature_byte(signature, static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
        update_signature_byte(signature, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        update_signature_byte(signature, static_cast<std::uint8_t>(value & 0xFFU));
    }

    void update_signature_f32(DecodeSignatureBuilder& signature, float value) noexcept
    {
        update_signature_u32(signature, std::bit_cast<std::uint32_t>(value));
    }

    [[nodiscard]] std::uint32_t decode_relevant_technique_mask(
        simnet::PipelineDefinition const& pipeline
    ) noexcept
    {
        auto constexpr mask = static_cast<std::uint32_t>(
            simnet::PipelineTechniqueFlags::Incremental
                | simnet::PipelineTechniqueFlags::Quantization
                | simnet::PipelineTechniqueFlags::OctHeading
                | simnet::PipelineTechniqueFlags::Delta
        );
        return static_cast<std::uint32_t>(pipeline.techniques) & mask;
    }

    [[nodiscard]] std::uint64_t make_pipeline_decode_signature(
        simnet::PipelineDefinition const& pipeline
    ) noexcept
    {
        auto signature = DecodeSignatureBuilder {};
        update_signature_u16(signature, simnet::pipeline_wire::protocol_version);
        update_signature_u16(signature, simnet::pipeline_wire::schema_version);
        update_signature_u8(signature, static_cast<std::uint8_t>(pipeline.profile));
        update_signature_u8(signature, static_cast<std::uint8_t>(pipeline.codec));
        update_signature_u32(signature, decode_relevant_technique_mask(pipeline));

        if (is_quantized(pipeline)) {
            auto const bounds = pipeline.quantization.position_bounds;
            update_signature_f32(signature, bounds.min.x);
            update_signature_f32(signature, bounds.min.y);
            update_signature_f32(signature, bounds.min.z);
            update_signature_f32(signature, bounds.max.x);
            update_signature_f32(signature, bounds.max.y);
            update_signature_f32(signature, bounds.max.z);
        }

        return signature.value;
    }

    [[nodiscard]] std::uint32_t encoded_record_bytes(simnet::PipelineDefinition const& pipeline) noexcept
    {
        if (is_bitpacked(pipeline)) {
            return simnet::pipeline_wire::bitpacked_quantized_oct_record_bytes;
        }
        if (uses_oct_heading(pipeline)) {
            return simnet::pipeline_wire::quantized_oct_record_bytes;
        }
        if (is_quantized(pipeline)) {
            return simnet::pipeline_wire::quantized_record_bytes;
        }
        return simnet::pipeline_wire::raw_record_bytes;
    }

    [[nodiscard]] bool should_emit_for_send_interval(
        simnet::PipelineDefinition const& pipeline,
        simnet::Tick tick
    ) noexcept
    {
        if (!simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::SendInterval)) {
            return true;
        }

        auto const interval = static_cast<simnet::Tick>(pipeline.send_interval.interval_ticks);
        auto const phase = static_cast<simnet::Tick>(pipeline.send_interval.phase_offset);
        return ((tick + phase) % interval) == 0U;
    }

    void select_incremental_indices(
        simnet::PipelineScratch& scratch,
        std::size_t entity_count,
        std::uint32_t cursor,
        std::uint32_t max_entities
    )
    {
        scratch.selected_indices.clear();
        if (entity_count == 0) {
            return;
        }

        auto const selected_count = std::min<std::size_t>(entity_count, max_entities);
        scratch.selected_indices.reserve(selected_count);
        auto const start = static_cast<std::size_t>(cursor) % entity_count;

        // First pass scheduling uses snapshot-order cursoring; selected IDs are sorted before encode.
        for (std::size_t offset = 0; offset < selected_count; ++offset) {
            auto const source_index = (start + offset) % entity_count;
            scratch.selected_indices.push_back(static_cast<std::uint32_t>(source_index));
        }
        std::sort(scratch.selected_indices.begin(), scratch.selected_indices.end());
    }

    [[nodiscard]] bool same_vec3(simnet::Vec3f left, simnet::Vec3f right) noexcept
    {
        return left.x == right.x && left.y == right.y && left.z == right.z;
    }

    [[nodiscard]] bool same_entity_state(
        simnet::WorldSnapshot const& current,
        std::size_t current_index,
        simnet::WorldSnapshot const& baseline,
        std::size_t baseline_index
    ) noexcept
    {
        return same_vec3(current.positions[current_index], baseline.positions[baseline_index])
            && same_vec3(current.headings[current_index], baseline.headings[baseline_index])
            && current.hues[current_index] == baseline.hues[baseline_index];
    }

    void select_delta_records(
        simnet::PipelineScratch& scratch,
        simnet::WorldSnapshot const& current,
        simnet::WorldSnapshot const& baseline
    )
    {
        scratch.selected_indices.clear();
        scratch.selected_delete_ids.clear();
        scratch.selected_indices.reserve(current.size());
        scratch.selected_delete_ids.reserve(baseline.size());

        auto current_index = std::size_t {};
        auto baseline_index = std::size_t {};
        while (current_index < current.size() && baseline_index < baseline.size()) {
            auto const current_id = current.ids[current_index];
            auto const baseline_id = baseline.ids[baseline_index];
            if (current_id < baseline_id) {
                scratch.selected_indices.push_back(static_cast<std::uint32_t>(current_index));
                ++current_index;
            } else if (baseline_id < current_id) {
                scratch.selected_delete_ids.push_back(baseline_id);
                ++baseline_index;
            } else {
                if (!same_entity_state(current, current_index, baseline, baseline_index)) {
                    scratch.selected_indices.push_back(static_cast<std::uint32_t>(current_index));
                }
                ++current_index;
                ++baseline_index;
            }
        }

        while (current_index < current.size()) {
            scratch.selected_indices.push_back(static_cast<std::uint32_t>(current_index));
            ++current_index;
        }
        while (baseline_index < baseline.size()) {
            scratch.selected_delete_ids.push_back(baseline.ids[baseline_index]);
            ++baseline_index;
        }
    }

    [[nodiscard]] std::uint32_t next_incremental_cursor(
        std::size_t entity_count,
        std::uint32_t cursor,
        std::uint32_t selected_count
    ) noexcept
    {
        if (entity_count == 0) {
            return 0;
        }
        auto const next = (static_cast<std::uint64_t>(cursor) + selected_count) % entity_count;
        return static_cast<std::uint32_t>(next);
    }

    [[nodiscard]] std::uint16_t quantize_unorm16(float value, float min, float max) noexcept
    {
        auto const normalized = std::clamp((value - min) / (max - min), 0.0F, 1.0F);
        return static_cast<std::uint16_t>(std::lround(normalized * 65535.0F));
    }

    [[nodiscard]] float dequantize_unorm16(std::uint16_t value, float min, float max) noexcept
    {
        auto const normalized = static_cast<float>(value) / 65535.0F;
        return min + (normalized * (max - min));
    }

    [[nodiscard]] std::uint16_t quantize_snorm16(float value) noexcept
    {
        auto const clamped = std::clamp(value, -1.0F, 1.0F);
        auto const signed_value = static_cast<std::int32_t>(std::lround(clamped * 32767.0F));
        return signed_value < 0
            ? static_cast<std::uint16_t>(65536 + signed_value)
            : static_cast<std::uint16_t>(signed_value);
    }

    [[nodiscard]] float dequantize_snorm16(std::uint16_t value) noexcept
    {
        auto const signed_value = value > 32767U
            ? static_cast<std::int32_t>(value) - 65536
            : static_cast<std::int32_t>(value);
        return static_cast<float>(signed_value) / 32767.0F;
    }

    [[nodiscard]] float sign_not_zero(float value) noexcept
    {
        return value < 0.0F ? -1.0F : 1.0F;
    }

    [[nodiscard]] std::uint16_t encode_oct_component(float value) noexcept
    {
        return quantize_snorm16(value);
    }

    [[nodiscard]] float decode_oct_component(std::uint16_t value) noexcept
    {
        return dequantize_snorm16(value);
    }

    [[nodiscard]] std::pair<std::uint16_t, std::uint16_t> encode_oct_heading(simnet::Vec3f heading) noexcept
    {
        auto value = simnet::normalize_or(heading, { .x = 1.0F, .y = 0.0F, .z = 0.0F });
        auto const inv_l1 = 1.0F / (std::abs(value.x) + std::abs(value.y) + std::abs(value.z));
        value.x *= inv_l1;
        value.y *= inv_l1;
        value.z *= inv_l1;

        if (value.z < 0.0F) {
            auto const old_x = value.x;
            auto const old_y = value.y;
            value.x = (1.0F - std::abs(old_y)) * sign_not_zero(old_x);
            value.y = (1.0F - std::abs(old_x)) * sign_not_zero(old_y);
        }

        return { encode_oct_component(value.x), encode_oct_component(value.y) };
    }

    [[nodiscard]] simnet::Vec3f decode_oct_heading(std::uint16_t encoded_x, std::uint16_t encoded_y) noexcept
    {
        auto value = simnet::Vec3f {
            .x = decode_oct_component(encoded_x),
            .y = decode_oct_component(encoded_y),
            .z = 0.0F,
        };
        value.z = 1.0F - std::abs(value.x) - std::abs(value.y);

        if (value.z < 0.0F) {
            auto const old_x = value.x;
            auto const old_y = value.y;
            value.x = (1.0F - std::abs(old_y)) * sign_not_zero(old_x);
            value.y = (1.0F - std::abs(old_x)) * sign_not_zero(old_y);
        }

        return simnet::normalize_or(value, { .x = 1.0F, .y = 0.0F, .z = 0.0F });
    }

    void write_quantized_vec3(
        std::vector<std::byte>& bytes,
        simnet::Vec3f value,
        simnet::Aabb3f bounds
    )
    {
        simnet::pipeline_wire::write_u16(bytes, quantize_unorm16(value.x, bounds.min.x, bounds.max.x));
        simnet::pipeline_wire::write_u16(bytes, quantize_unorm16(value.y, bounds.min.y, bounds.max.y));
        simnet::pipeline_wire::write_u16(bytes, quantize_unorm16(value.z, bounds.min.z, bounds.max.z));
    }

    void write_quantized_heading(std::vector<std::byte>& bytes, simnet::Vec3f value)
    {
        simnet::pipeline_wire::write_u16(bytes, quantize_snorm16(value.x));
        simnet::pipeline_wire::write_u16(bytes, quantize_snorm16(value.y));
        simnet::pipeline_wire::write_u16(bytes, quantize_snorm16(value.z));
    }

    void write_oct_heading(std::vector<std::byte>& bytes, simnet::Vec3f value)
    {
        auto const [x, y] = encode_oct_heading(value);
        simnet::pipeline_wire::write_u16(bytes, x);
        simnet::pipeline_wire::write_u16(bytes, y);
    }

    [[nodiscard]] bool read_quantized_vec3(
        std::span<const std::byte> bytes,
        std::size_t& offset,
        simnet::Aabb3f bounds,
        simnet::Vec3f& value
    )
    {
        auto x = std::uint16_t {};
        auto y = std::uint16_t {};
        auto z = std::uint16_t {};
        if (!simnet::pipeline_wire::read_u16(bytes, offset, x)
            || !simnet::pipeline_wire::read_u16(bytes, offset, y)
            || !simnet::pipeline_wire::read_u16(bytes, offset, z)) {
            return false;
        }

        value = {
            .x = dequantize_unorm16(x, bounds.min.x, bounds.max.x),
            .y = dequantize_unorm16(y, bounds.min.y, bounds.max.y),
            .z = dequantize_unorm16(z, bounds.min.z, bounds.max.z),
        };
        return true;
    }

    [[nodiscard]] bool read_quantized_heading(
        std::span<const std::byte> bytes,
        std::size_t& offset,
        simnet::Vec3f& value
    )
    {
        auto x = std::uint16_t {};
        auto y = std::uint16_t {};
        auto z = std::uint16_t {};
        if (!simnet::pipeline_wire::read_u16(bytes, offset, x)
            || !simnet::pipeline_wire::read_u16(bytes, offset, y)
            || !simnet::pipeline_wire::read_u16(bytes, offset, z)) {
            return false;
        }

        value = simnet::normalize_or(
            {
                .x = dequantize_snorm16(x),
                .y = dequantize_snorm16(y),
                .z = dequantize_snorm16(z),
            },
            { .x = 1.0F, .y = 0.0F, .z = 0.0F }
        );
        return true;
    }

    [[nodiscard]] bool read_oct_heading(
        std::span<const std::byte> bytes,
        std::size_t& offset,
        simnet::Vec3f& value
    )
    {
        auto x = std::uint16_t {};
        auto y = std::uint16_t {};
        if (!simnet::pipeline_wire::read_u16(bytes, offset, x)
            || !simnet::pipeline_wire::read_u16(bytes, offset, y)) {
            return false;
        }

        value = decode_oct_heading(x, y);
        return true;
    }

    struct BitWriter
    {
        std::vector<std::byte>& bytes;
        std::uint8_t scratch {};
        std::uint8_t used_bits {};
    };

    void write_bits(BitWriter& writer, std::uint32_t value, std::uint8_t bit_count)
    {
        for (auto bit_index = bit_count; bit_index > 0U; --bit_index) {
            auto const bit = static_cast<std::uint8_t>((value >> (bit_index - 1U)) & 1U);
            writer.scratch = static_cast<std::uint8_t>((writer.scratch << 1U) | bit);
            ++writer.used_bits;
            if (writer.used_bits == 8U) {
                writer.bytes.push_back(static_cast<std::byte>(writer.scratch));
                writer.scratch = 0;
                writer.used_bits = 0;
            }
        }
    }

    void flush_bits(BitWriter& writer)
    {
        if (writer.used_bits == 0U) {
            return;
        }
        writer.scratch = static_cast<std::uint8_t>(writer.scratch << (8U - writer.used_bits));
        writer.bytes.push_back(static_cast<std::byte>(writer.scratch));
        writer.scratch = 0;
        writer.used_bits = 0;
    }

    struct BitReader
    {
        std::span<const std::byte> bytes;
        std::size_t bit_offset {};
    };

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

    void write_bitpacked_record(
        std::vector<std::byte>& bytes,
        simnet::EntityNetId id,
        simnet::Vec3f position,
        simnet::Vec3f heading,
        std::uint8_t hue,
        simnet::Aabb3f bounds
    )
    {
        auto writer = BitWriter { .bytes = bytes };
        auto const [oct_x, oct_y] = encode_oct_heading(heading);
        write_bits(writer, id, 32);
        write_bits(writer, quantize_unorm16(position.x, bounds.min.x, bounds.max.x), 16);
        write_bits(writer, quantize_unorm16(position.y, bounds.min.y, bounds.max.y), 16);
        write_bits(writer, quantize_unorm16(position.z, bounds.min.z, bounds.max.z), 16);
        write_bits(writer, oct_x, 16);
        write_bits(writer, oct_y, 16);
        write_bits(writer, hue, 8);
        flush_bits(writer);
    }

    [[nodiscard]] bool read_bitpacked_record(
        std::span<const std::byte> bytes,
        simnet::Aabb3f bounds,
        simnet::BoidState& boid
    )
    {
        auto reader = BitReader { .bytes = bytes };
        auto id = std::uint32_t {};
        auto px = std::uint32_t {};
        auto py = std::uint32_t {};
        auto pz = std::uint32_t {};
        auto hx = std::uint32_t {};
        auto hy = std::uint32_t {};
        auto hue = std::uint32_t {};
        if (!read_bits(reader, 32, id)
            || !read_bits(reader, 16, px)
            || !read_bits(reader, 16, py)
            || !read_bits(reader, 16, pz)
            || !read_bits(reader, 16, hx)
            || !read_bits(reader, 16, hy)
            || !read_bits(reader, 8, hue)) {
            return false;
        }

        boid.id = id;
        boid.position = {
            .x = dequantize_unorm16(static_cast<std::uint16_t>(px), bounds.min.x, bounds.max.x),
            .y = dequantize_unorm16(static_cast<std::uint16_t>(py), bounds.min.y, bounds.max.y),
            .z = dequantize_unorm16(static_cast<std::uint16_t>(pz), bounds.min.z, bounds.max.z),
        };
        boid.heading = decode_oct_heading(static_cast<std::uint16_t>(hx), static_cast<std::uint16_t>(hy));
        boid.hue = static_cast<std::uint8_t>(hue);
        return true;
    }

    [[nodiscard]] simnet::EncodeOutput skipped_encode(
        simnet::PipelineDefinition const& pipeline,
        simnet::WorldSnapshot const& snapshot,
        simnet::EncodeSkipReason reason
    )
    {
        auto report = simnet::EncodeReport {
            .tick = snapshot.tick,
            .sequence = 0,
            .baseline_sequence = 0,
            .snapshot_kind = simnet::SnapshotKind::FullReplace,
            .profile = pipeline.profile,
            .codec = pipeline.codec,
            .techniques = pipeline.techniques,
            .emitted = false,
            .skipped = true,
            .delta = false,
            .skip_reason = reason,
            .budget_exceeded = false,
            .input_entities = static_cast<std::uint32_t>(snapshot.size()),
            .selected_entities = 0,
            .upsert_count = 0,
            .delete_count = 0,
            .packet_bytes = 0,
            .payload_bytes = 0,
            .uncompressed_bytes = 0,
            .final_bytes = 0,
        };

        return {
            .kind = simnet::EncodeResultKind::Skipped,
            .skip_reason = reason,
            .packet = {},
            .report = report,
        };
    }

    [[nodiscard]] simnet::DecodeOutput invalid_decode(
        std::span<std::byte const> bytes,
        std::string error,
        simnet::Tick tick = 0,
        simnet::SequenceId sequence = 0,
        simnet::SequenceId baseline_sequence = 0,
        simnet::SnapshotKind snapshot_kind = simnet::SnapshotKind::FullReplace
    )
    {
        auto output = simnet::DecodeOutput {};
        output.report.tick = tick;
        output.report.sequence = sequence;
        output.report.baseline_sequence = baseline_sequence;
        output.report.snapshot_kind = snapshot_kind;
        output.report.packet_bytes = static_cast<std::uint32_t>(
            std::min<std::size_t>(bytes.size(), std::numeric_limits<std::uint32_t>::max())
        );
        output.report.valid = false;
        output.report.error = std::move(error);
        return output;
    }

    [[nodiscard]] bool checked_payload_size(
        simnet::pipeline_wire::PacketHeader const& header,
        std::size_t byte_count
    ) noexcept
    {
        if (byte_count < simnet::pipeline_wire::header_bytes) {
            return false;
        }
        return header.payload_bytes == byte_count - simnet::pipeline_wire::header_bytes;
    }
}

namespace simnet
{
    PipelineDefinition make_raw_snapshot_pipeline(PacketBudget budget)
    {
        return {
            .profile = PipelineProfileKind::RawSnapshot,
            .codec = CodecKind::ByteAligned,
            .techniques = PipelineTechniqueFlags::None,
            .budget = budget,
        };
    }

    std::uint64_t pipeline_decode_signature(PipelineDefinition const& definition) noexcept
    {
        return make_pipeline_decode_signature(definition);
    }

    EncodeOutput encode_snapshot(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        PipelineScratch& scratch,
        EncodeInput const& input
    )
    {
        require_raw_snapshot(pipeline);
        require_send_interval_settings(pipeline);
        require_incremental_settings(pipeline);
        require_quantization_settings(pipeline);
        require_snapshot(input.snapshot, "encode input snapshot");
        auto const delta_enabled = is_delta(pipeline);
        if (input.baseline_snapshot == nullptr) {
            if (input.baseline_sequence != 0U) {
                throw std::runtime_error("baseline sequence requires a baseline snapshot");
            }
        } else {
            if (!delta_enabled) {
                throw std::runtime_error("baseline snapshot requires the delta technique");
            }
            if (input.baseline_sequence == 0U) {
                throw std::runtime_error("delta baseline sequence 0 is reserved");
            }
            require_snapshot(input.baseline_snapshot, "encode baseline snapshot");
        }
        auto const& snapshot = *input.snapshot;
        require_u32_count(snapshot.size(), "snapshot entity count");

        if (!should_emit_for_send_interval(pipeline, snapshot.tick)) {
            return skipped_encode(pipeline, snapshot, EncodeSkipReason::SendInterval);
        }

        auto const sequence = client_state.next_sequence;
        if (sequence == 0U) {
            throw std::runtime_error("pipeline sequence 0 is reserved");
        }
        if (sequence == std::numeric_limits<SequenceId>::max()) {
            throw std::runtime_error("pipeline sequence would wrap to reserved 0");
        }
        if (input.baseline_snapshot != nullptr && input.baseline_sequence >= sequence) {
            throw std::runtime_error("delta baseline sequence must precede packet sequence");
        }

        auto const incremental_enabled = is_incremental(pipeline);
        auto const emit_delta = delta_enabled && input.baseline_snapshot != nullptr;
        auto const quantized = is_quantized(pipeline);
        if (emit_delta) {
            require_u32_count(input.baseline_snapshot->size(), "baseline snapshot entity count");
            select_delta_records(scratch, snapshot, *input.baseline_snapshot);
        } else if (incremental_enabled) {
            scratch.selected_delete_ids.clear();
            select_incremental_indices(
                scratch,
                snapshot.size(),
                client_state.incremental_cursor,
                pipeline.incremental.max_entities_per_packet
            );
        } else {
            scratch.selected_indices.clear();
            scratch.selected_delete_ids.clear();
        }

        auto const selected_count = (emit_delta || incremental_enabled)
            ? scratch.selected_indices.size()
            : snapshot.size();
        auto const delete_count = emit_delta ? scratch.selected_delete_ids.size() : 0U;
        auto const snapshot_kind = (emit_delta || incremental_enabled)
            ? SnapshotKind::Patch
            : SnapshotKind::FullReplace;
        auto const baseline_sequence = emit_delta ? input.baseline_sequence : 0U;
        auto const oct_heading = uses_oct_heading(pipeline);
        auto const bitpacked = is_bitpacked(pipeline);
        auto const record_bytes = encoded_record_bytes(pipeline);
        auto const payload_byte_count =
            delete_count * static_cast<std::size_t>(pipeline_wire::delete_record_bytes)
            + selected_count * static_cast<std::size_t>(record_bytes);
        if (payload_byte_count > std::numeric_limits<std::uint32_t>::max()
            || payload_byte_count + pipeline_wire::header_bytes > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("encoded raw snapshot packet exceeds uint32 byte range");
        }
        auto const payload_bytes = static_cast<std::uint32_t>(payload_byte_count);
        auto const header = pipeline_wire::PacketHeader {
            .magic = pipeline_wire::packet_magic,
            .protocol = pipeline_wire::protocol_version,
            .schema = pipeline_wire::schema_version,
            .decode_signature = make_pipeline_decode_signature(pipeline),
            .packet_kind = PipelinePacketKind::Snapshot,
            .snapshot_kind = snapshot_kind,
            .flags = PipelinePacketFlags::None,
            .tick = snapshot.tick,
            .sequence = sequence,
            .baseline_sequence = baseline_sequence,
            .upsert_count = static_cast<std::uint32_t>(selected_count),
            .delete_count = static_cast<std::uint32_t>(delete_count),
            .payload_bytes = payload_bytes,
        };

        scratch.bytes.clear();
        scratch.bytes.reserve(pipeline_wire::header_bytes + payload_bytes);
        pipeline_wire::write_header(scratch.bytes, header);
        for (auto const id : scratch.selected_delete_ids) {
            pipeline_wire::write_u32(scratch.bytes, id);
        }

        auto write_record = [&](std::size_t source_index) {
            if (bitpacked) {
                write_bitpacked_record(
                    scratch.bytes,
                    snapshot.ids[source_index],
                    snapshot.positions[source_index],
                    snapshot.headings[source_index],
                    snapshot.hues[source_index],
                    pipeline.quantization.position_bounds
                );
                return;
            }

            pipeline_wire::write_u32(scratch.bytes, snapshot.ids[source_index]);
            if (quantized) {
                write_quantized_vec3(
                    scratch.bytes,
                    snapshot.positions[source_index],
                    pipeline.quantization.position_bounds
                );
                if (oct_heading) {
                    write_oct_heading(scratch.bytes, snapshot.headings[source_index]);
                } else {
                    write_quantized_heading(scratch.bytes, snapshot.headings[source_index]);
                }
            } else {
                pipeline_wire::write_vec3(scratch.bytes, snapshot.positions[source_index]);
                pipeline_wire::write_vec3(scratch.bytes, snapshot.headings[source_index]);
            }
            pipeline_wire::write_u8(scratch.bytes, snapshot.hues[source_index]);
        };

        if (emit_delta || incremental_enabled) {
            for (auto const source_index : scratch.selected_indices) {
                write_record(source_index);
            }
        } else {
            for (std::size_t source_index = 0; source_index < snapshot.size(); ++source_index) {
                write_record(source_index);
            }
        }

        auto packet = EncodedPacket {
            .tick = snapshot.tick,
            .sequence = sequence,
            .baseline_sequence = baseline_sequence,
            .kind = PipelinePacketKind::Snapshot,
            .flags = PipelinePacketFlags::None,
            .bytes = scratch.bytes,
        };

        auto const final_bytes = static_cast<std::uint32_t>(packet.bytes.size());
        auto report = EncodeReport {
            .tick = snapshot.tick,
            .sequence = sequence,
            .baseline_sequence = baseline_sequence,
            .snapshot_kind = snapshot_kind,
            .profile = pipeline.profile,
            .codec = pipeline.codec,
            .techniques = pipeline.techniques,
            .emitted = true,
            .skipped = false,
            .delta = emit_delta,
            .skip_reason = EncodeSkipReason::None,
            .budget_exceeded = final_bytes > pipeline.budget.max_packet_bytes,
            .input_entities = static_cast<std::uint32_t>(snapshot.size()),
            .selected_entities = static_cast<std::uint32_t>(selected_count),
            .upsert_count = static_cast<std::uint32_t>(selected_count),
            .delete_count = static_cast<std::uint32_t>(delete_count),
            .packet_bytes = final_bytes,
            .payload_bytes = payload_bytes,
            .uncompressed_bytes = final_bytes,
            .final_bytes = final_bytes,
        };

        auto output = EncodeOutput {
            .kind = EncodeResultKind::Packet,
            .skip_reason = EncodeSkipReason::None,
            .packet = std::move(packet),
            .report = report,
        };

        auto const next_cursor = incremental_enabled
            ? next_incremental_cursor(
                snapshot.size(),
                client_state.incremental_cursor,
                static_cast<std::uint32_t>(selected_count)
            )
            : client_state.incremental_cursor;
        client_state.next_sequence = sequence + 1U;
        client_state.incremental_cursor = next_cursor;
        return output;
    }

    DecodeOutput decode_packet(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        PipelineScratch& scratch,
        DecodeInput const& input
    )
    {
        require_raw_snapshot(pipeline);
        require_quantization_settings(pipeline);
        static_cast<void>(scratch);

        auto const bytes = input.bytes;
        if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            return invalid_decode(bytes, "packet byte count exceeds uint32 range");
        }
        if (bytes.size() < pipeline_wire::header_bytes) {
            return invalid_decode(bytes, "packet is shorter than header");
        }

        auto header = pipeline_wire::PacketHeader {};
        if (!pipeline_wire::read_header(bytes, header)) {
            return invalid_decode(bytes, "failed to read packet header");
        }
        auto invalid_packet = [&](std::string error) {
            auto output = invalid_decode(
                bytes,
                std::move(error),
                header.tick,
                header.sequence,
                header.baseline_sequence,
                header.snapshot_kind
            );
            output.report.delta = is_delta(pipeline)
                && header.snapshot_kind == SnapshotKind::Patch;
            return output;
        };
        if (header.magic != pipeline_wire::packet_magic) {
            return invalid_packet("invalid packet magic");
        }
        if (header.protocol != pipeline_wire::protocol_version || header.schema != pipeline_wire::schema_version) {
            return invalid_packet("unsupported packet version");
        }
        if (header.decode_signature != make_pipeline_decode_signature(pipeline)) {
            return invalid_packet("packet decode signature does not match local pipeline");
        }
        if (header.packet_kind != PipelinePacketKind::Snapshot) {
            return invalid_packet("unsupported packet kind");
        }
        if (header.snapshot_kind != SnapshotKind::FullReplace && header.snapshot_kind != SnapshotKind::Patch) {
            return invalid_packet("unsupported snapshot kind");
        }
        if (header.flags != PipelinePacketFlags::None) {
            return invalid_packet("unsupported packet flags");
        }
        if (header.sequence == 0U) {
            return invalid_packet("packet sequence 0 is reserved");
        }
        if (header.sequence <= client_state.latest_remote_sequence) {
            return invalid_packet("stale or out-of-order packet sequence");
        }
        if (!checked_payload_size(header, bytes.size())) {
            return invalid_packet("packet payload size does not match header");
        }

        auto const delta_enabled = is_delta(pipeline);
        if (header.snapshot_kind == SnapshotKind::FullReplace) {
            if (header.baseline_sequence != 0U) {
                return invalid_packet("full snapshot baseline sequence must be 0");
            }
            if (header.delete_count != 0U) {
                return invalid_packet("full snapshot delete count must be 0");
            }
        } else if (delta_enabled) {
            if (header.baseline_sequence == 0U) {
                return invalid_packet("delta patch baseline sequence 0 is reserved");
            }
            if (header.baseline_sequence >= header.sequence) {
                return invalid_packet("delta patch baseline must precede packet sequence");
            }
            if (header.baseline_sequence != input.applied_baseline_sequence) {
                return invalid_packet("delta patch baseline does not match applied baseline");
            }
        } else {
            if (header.baseline_sequence != 0U) {
                return invalid_packet("non-delta patch baseline sequence must be 0");
            }
            if (header.delete_count != 0U) {
                return invalid_packet("non-delta patch delete count must be 0");
            }
        }

        auto const quantized = is_quantized(pipeline);
        auto const oct_heading = uses_oct_heading(pipeline);
        auto const bitpacked = is_bitpacked(pipeline);
        auto const record_bytes = encoded_record_bytes(pipeline);
        auto const expected_payload = static_cast<std::uint64_t>(header.delete_count)
                * pipeline_wire::delete_record_bytes
            + static_cast<std::uint64_t>(header.upsert_count) * record_bytes;
        if (expected_payload != header.payload_bytes) {
            return invalid_packet("packet payload counts do not match payload size");
        }

        auto offset = static_cast<std::size_t>(pipeline_wire::header_bytes);
        auto patch = ClientSnapshotPatch {};
        patch.tick = header.tick;
        patch.kind = header.snapshot_kind;
        patch.reserve(header.upsert_count, header.delete_count);

        for (std::uint32_t index = 0; index < header.delete_count; ++index) {
            auto id = EntityNetId {};
            if (!pipeline_wire::read_u32(bytes, offset, id)) {
                return invalid_packet("truncated delete id data");
            }
            patch.deletes.push_back(id);
        }

        for (std::uint32_t index = 0; index < header.upsert_count; ++index) {
            auto boid = BoidState {};
            auto read_ok = true;
            if (bitpacked) {
                auto const record_begin = offset;
                auto const record_end = record_begin + record_bytes;
                if (record_end > bytes.size()) {
                    read_ok = false;
                } else {
                    read_ok = read_bitpacked_record(
                        bytes.subspan(record_begin, record_bytes),
                        pipeline.quantization.position_bounds,
                        boid
                    );
                    offset = record_end;
                }
            } else {
                read_ok = pipeline_wire::read_u32(bytes, offset, boid.id);
            }
            if (!bitpacked) {
                if (read_ok && quantized) {
                    read_ok = read_quantized_vec3(
                        bytes,
                        offset,
                        pipeline.quantization.position_bounds,
                        boid.position
                    );
                    if (read_ok && oct_heading) {
                        read_ok = read_oct_heading(bytes, offset, boid.heading);
                    } else if (read_ok) {
                        read_ok = read_quantized_heading(bytes, offset, boid.heading);
                    }
                } else if (read_ok) {
                    read_ok = pipeline_wire::read_vec3(bytes, offset, boid.position)
                        && pipeline_wire::read_vec3(bytes, offset, boid.heading);
                }
                if (read_ok) {
                    read_ok = pipeline_wire::read_u8(bytes, offset, boid.hue);
                }
            }
            if (!read_ok) {
                return invalid_packet("truncated upsert record data");
            }
            patch.upserts.push_back(boid);
        }

        if (offset != bytes.size()) {
            return invalid_packet("packet has trailing bytes");
        }

        auto const validation = validate_client_snapshot_patch(patch);
        if (!validation.valid) {
            return invalid_packet("decoded patch is invalid: " + validation.message);
        }

        client_state.latest_remote_sequence = header.sequence;

        auto report = DecodeReport {
            .tick = header.tick,
            .sequence = header.sequence,
            .baseline_sequence = header.baseline_sequence,
            .snapshot_kind = header.snapshot_kind,
            .upsert_count = header.upsert_count,
            .delete_count = header.delete_count,
            .packet_bytes = static_cast<std::uint32_t>(bytes.size()),
            .delta = delta_enabled && header.snapshot_kind == SnapshotKind::Patch,
            .valid = true,
            .error = {},
        };

        return { .patch = std::move(patch), .report = std::move(report) };
    }
}
