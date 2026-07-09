module;

#include <algorithm>
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

    void require_snapshot(simnet::WorldSnapshot const* snapshot)
    {
        if (snapshot == nullptr) {
            throw std::runtime_error("encode input snapshot is null");
        }

        auto const validation = simnet::validate_world_snapshot(*snapshot);
        if (!validation.valid) {
            throw std::runtime_error("encode input snapshot is invalid: " + validation.message);
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

    [[nodiscard]] bool uses_oct_heading(simnet::PipelineDefinition const& pipeline) noexcept
    {
        return simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::OctHeading);
    }

    [[nodiscard]] bool is_bitpacked(simnet::PipelineDefinition const& pipeline) noexcept
    {
        return pipeline.codec == simnet::CodecKind::BitPacked;
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
            .profile = pipeline.profile,
            .codec = pipeline.codec,
            .techniques = pipeline.techniques,
            .emitted = false,
            .skipped = true,
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
        simnet::EncodedPacket const* packet,
        std::string error
    )
    {
        auto output = simnet::DecodeOutput {};
        output.report.tick = packet == nullptr ? 0U : packet->tick;
        output.report.sequence = packet == nullptr ? 0U : packet->sequence;
        output.report.packet_bytes = packet == nullptr
            ? 0U
            : static_cast<std::uint32_t>(
                std::min<std::size_t>(packet->bytes.size(), std::numeric_limits<std::uint32_t>::max())
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
        require_snapshot(input.snapshot);
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

        auto const incremental_enabled = is_incremental(pipeline);
        auto const quantized = is_quantized(pipeline);
        if (incremental_enabled) {
            select_incremental_indices(
                scratch,
                snapshot.size(),
                client_state.incremental_cursor,
                pipeline.incremental.max_entities_per_packet
            );
        }

        auto const selected_count = incremental_enabled
            ? scratch.selected_indices.size()
            : snapshot.size();
        auto const snapshot_kind = incremental_enabled ? SnapshotKind::Patch : SnapshotKind::FullReplace;
        auto const oct_heading = uses_oct_heading(pipeline);
        auto const bitpacked = is_bitpacked(pipeline);
        auto const record_bytes = encoded_record_bytes(pipeline);
        auto const payload_byte_count = selected_count
            * static_cast<std::size_t>(record_bytes);
        if (payload_byte_count > std::numeric_limits<std::uint32_t>::max()
            || payload_byte_count + pipeline_wire::header_bytes > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("encoded raw snapshot packet exceeds uint32 byte range");
        }
        auto const payload_bytes = static_cast<std::uint32_t>(payload_byte_count);
        auto const header = pipeline_wire::PacketHeader {
            .magic = pipeline_wire::packet_magic,
            .protocol = pipeline_wire::protocol_version,
            .schema = pipeline_wire::schema_version,
            .packet_kind = PipelinePacketKind::Snapshot,
            .snapshot_kind = snapshot_kind,
            .flags = PipelinePacketFlags::None,
            .tick = snapshot.tick,
            .sequence = sequence,
            .baseline_sequence = 0,
            .upsert_count = static_cast<std::uint32_t>(selected_count),
            .delete_count = 0,
            .payload_bytes = payload_bytes,
        };

        scratch.bytes.clear();
        scratch.bytes.reserve(pipeline_wire::header_bytes + payload_bytes);
        pipeline_wire::write_header(scratch.bytes, header);

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

        if (incremental_enabled) {
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
            .baseline_sequence = 0,
            .kind = PipelinePacketKind::Snapshot,
            .flags = PipelinePacketFlags::None,
            .bytes = scratch.bytes,
        };

        auto const final_bytes = static_cast<std::uint32_t>(packet.bytes.size());
        auto report = EncodeReport {
            .tick = snapshot.tick,
            .sequence = sequence,
            .profile = pipeline.profile,
            .codec = pipeline.codec,
            .techniques = pipeline.techniques,
            .emitted = true,
            .skipped = false,
            .skip_reason = EncodeSkipReason::None,
            .budget_exceeded = final_bytes > pipeline.budget.max_packet_bytes,
            .input_entities = static_cast<std::uint32_t>(snapshot.size()),
            .selected_entities = static_cast<std::uint32_t>(selected_count),
            .upsert_count = static_cast<std::uint32_t>(selected_count),
            .delete_count = 0,
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

        if (input.packet == nullptr) {
            return invalid_decode(nullptr, "decode input packet is null");
        }

        auto const& packet = *input.packet;
        if (packet.bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            return invalid_decode(&packet, "packet byte count exceeds uint32 range");
        }
        if (packet.bytes.size() < pipeline_wire::header_bytes) {
            return invalid_decode(&packet, "packet is shorter than header");
        }

        auto header = pipeline_wire::PacketHeader {};
        if (!pipeline_wire::read_header(packet.bytes, header)) {
            return invalid_decode(&packet, "failed to read packet header");
        }
        if (header.magic != pipeline_wire::packet_magic) {
            return invalid_decode(&packet, "invalid packet magic");
        }
        if (header.protocol != pipeline_wire::protocol_version || header.schema != pipeline_wire::schema_version) {
            return invalid_decode(&packet, "unsupported packet version");
        }
        if (header.packet_kind != PipelinePacketKind::Snapshot) {
            return invalid_decode(&packet, "unsupported packet kind");
        }
        if (header.snapshot_kind != SnapshotKind::FullReplace && header.snapshot_kind != SnapshotKind::Patch) {
            return invalid_decode(&packet, "unsupported snapshot kind");
        }
        if (header.baseline_sequence != 0U) {
            return invalid_decode(&packet, "raw snapshot baseline sequence must be 0");
        }
        if (header.delete_count != 0U) {
            return invalid_decode(&packet, "raw snapshot delete count must be 0");
        }
        if (header.flags != PipelinePacketFlags::None) {
            return invalid_decode(&packet, "unsupported packet flags");
        }
        if (packet.tick != header.tick
            || packet.sequence != header.sequence
            || packet.baseline_sequence != header.baseline_sequence
            || packet.kind != header.packet_kind
            || packet.flags != header.flags) {
            return invalid_decode(&packet, "outer packet metadata does not match header");
        }
        if (header.sequence == 0U) {
            return invalid_decode(&packet, "packet sequence 0 is reserved");
        }
        if (header.sequence <= client_state.latest_remote_sequence) {
            return invalid_decode(&packet, "stale or out-of-order packet sequence");
        }
        if (!checked_payload_size(header, packet.bytes.size())) {
            return invalid_decode(&packet, "packet payload size does not match header");
        }

        auto const quantized = is_quantized(pipeline);
        auto const oct_heading = uses_oct_heading(pipeline);
        auto const bitpacked = is_bitpacked(pipeline);
        auto const record_bytes = encoded_record_bytes(pipeline);
        auto const expected_payload = static_cast<std::uint64_t>(header.delete_count)
                * pipeline_wire::delete_record_bytes
            + static_cast<std::uint64_t>(header.upsert_count) * record_bytes;
        if (expected_payload != header.payload_bytes) {
            return invalid_decode(&packet, "packet payload counts do not match payload size");
        }

        auto offset = static_cast<std::size_t>(pipeline_wire::header_bytes);
        auto patch = ClientSnapshotPatch {};
        patch.tick = header.tick;
        patch.kind = header.snapshot_kind;
        patch.reserve(header.upsert_count, header.delete_count);

        for (std::uint32_t index = 0; index < header.delete_count; ++index) {
            auto id = EntityNetId {};
            if (!pipeline_wire::read_u32(packet.bytes, offset, id)) {
                return invalid_decode(&packet, "truncated delete id data");
            }
            patch.deletes.push_back(id);
        }

        for (std::uint32_t index = 0; index < header.upsert_count; ++index) {
            auto boid = BoidState {};
            auto read_ok = true;
            if (bitpacked) {
                auto const record_begin = offset;
                auto const record_end = record_begin + record_bytes;
                if (record_end > packet.bytes.size()) {
                    read_ok = false;
                } else {
                    read_ok = read_bitpacked_record(
                        std::span<const std::byte> { packet.bytes }.subspan(record_begin, record_bytes),
                        pipeline.quantization.position_bounds,
                        boid
                    );
                    offset = record_end;
                }
            } else {
                read_ok = pipeline_wire::read_u32(packet.bytes, offset, boid.id);
            }
            if (!bitpacked) {
                if (read_ok && quantized) {
                    read_ok = read_quantized_vec3(
                        packet.bytes,
                        offset,
                        pipeline.quantization.position_bounds,
                        boid.position
                    );
                    if (read_ok && oct_heading) {
                        read_ok = read_oct_heading(packet.bytes, offset, boid.heading);
                    } else if (read_ok) {
                        read_ok = read_quantized_heading(packet.bytes, offset, boid.heading);
                    }
                } else if (read_ok) {
                    read_ok = pipeline_wire::read_vec3(packet.bytes, offset, boid.position)
                        && pipeline_wire::read_vec3(packet.bytes, offset, boid.heading);
                }
                if (read_ok) {
                    read_ok = pipeline_wire::read_u8(packet.bytes, offset, boid.hue);
                }
            }
            if (!read_ok) {
                return invalid_decode(&packet, "truncated upsert record data");
            }
            patch.upserts.push_back(boid);
        }

        if (offset != packet.bytes.size()) {
            return invalid_decode(&packet, "packet has trailing bytes");
        }

        auto const validation = validate_client_snapshot_patch(patch);
        if (!validation.valid) {
            return invalid_decode(&packet, "decoded patch is invalid: " + validation.message);
        }

        client_state.latest_remote_sequence = header.sequence;

        auto report = DecodeReport {
            .tick = header.tick,
            .sequence = header.sequence,
            .upsert_count = header.upsert_count,
            .delete_count = header.delete_count,
            .packet_bytes = static_cast<std::uint32_t>(packet.bytes.size()),
            .valid = true,
            .error = {},
        };

        return { .patch = std::move(patch), .report = std::move(report) };
    }
}
