module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

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
        if (pipeline.codec != simnet::CodecKind::ByteAligned) {
            throw std::runtime_error("raw snapshot requires byte-aligned codec");
        }
        auto constexpr supported_techniques = static_cast<std::uint32_t>(
            simnet::PipelineTechniqueFlags::SendInterval | simnet::PipelineTechniqueFlags::Incremental
        );
        auto const requested_techniques = static_cast<std::uint32_t>(pipeline.techniques);
        auto const unsupported_techniques = requested_techniques & ~supported_techniques;
        if (unsupported_techniques != 0U) {
            throw std::runtime_error("raw snapshot does not support requested techniques");
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

    [[nodiscard]] bool is_incremental(simnet::PipelineDefinition const& pipeline) noexcept
    {
        return simnet::has_flag(pipeline.techniques, simnet::PipelineTechniqueFlags::Incremental);
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
        auto const payload_byte_count = selected_count
            * static_cast<std::size_t>(pipeline_wire::raw_record_bytes);
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
            pipeline_wire::write_u32(scratch.bytes, snapshot.ids[source_index]);
            pipeline_wire::write_vec3(scratch.bytes, snapshot.positions[source_index]);
            pipeline_wire::write_vec3(scratch.bytes, snapshot.headings[source_index]);
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

        auto const expected_payload = static_cast<std::uint64_t>(header.delete_count)
                * pipeline_wire::delete_record_bytes
            + static_cast<std::uint64_t>(header.upsert_count) * pipeline_wire::raw_record_bytes;
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
            if (!pipeline_wire::read_u32(packet.bytes, offset, boid.id)
                || !pipeline_wire::read_vec3(packet.bytes, offset, boid.position)
                || !pipeline_wire::read_vec3(packet.bytes, offset, boid.heading)
                || !pipeline_wire::read_u8(packet.bytes, offset, boid.hue)) {
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
