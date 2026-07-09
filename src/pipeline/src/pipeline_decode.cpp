module;

#include <algorithm>
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
import :messages;
import :wire;
import :records;
import :signature;
import :validate;
import simnet.core;
import simnet.snapshot;

// --- Internal helpers - error result builder, payload check ---

namespace
{
    /// Builds an invalid DecodeOutput, filling the error message.
    [[nodiscard]] simnet::DecodeOutput invalid_decode(
        simnet::ByteSpan bytes,
        std::string error,
        simnet::Tick tick = 0,
        simnet::SequenceId sequence = 0,
        simnet::SequenceId baseline_sequence = 0,
        simnet::SnapshotKind snapshot_kind = simnet::SnapshotKind::FullReplace)
    {
        simnet::DecodeOutput output {};
        output.report.tick              = tick;
        output.report.sequence          = sequence;
        output.report.baseline_sequence = baseline_sequence;
        output.report.snapshot_kind     = snapshot_kind;
        output.report.packet_bytes      = static_cast<std::uint32_t>(
            std::min<std::size_t>(bytes.size(), std::numeric_limits<std::uint32_t>::max()));
        output.report.valid             = false;
        output.report.error             = std::move(error);
        return output;
    }

    /// Verifies that the packet payload size matches the header claim.
    [[nodiscard]] bool checked_payload_size(
        simnet::pipeline_wire::PacketHeader const& header,
        std::size_t byte_count) noexcept
    {
        if (byte_count < simnet::pipeline_wire::header_bytes) {
            return false;
        }
        return header.payload_bytes == byte_count - simnet::pipeline_wire::header_bytes;
    }
}

// --- Decode packet ---

namespace simnet
{
    DecodeOutput decode_packet(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        PipelineScratch& scratch,
        DecodeInput const& input)
    {
        pipeline_validate::require_raw_snapshot(pipeline);
        pipeline_validate::require_quantization_settings(pipeline);
        static_cast<void>(scratch);   // reserved for future use

        ByteSpan const bytes = input.bytes;

        // --- Size sanity checks ---

        if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            return invalid_decode(bytes, "packet byte count exceeds uint32 range");
        }
        if (bytes.size() < pipeline_wire::header_bytes) {
            return invalid_decode(bytes, "packet is shorter than header");
        }

        // --- Read header ---

        pipeline_wire::PacketHeader header {};
        if (!pipeline_wire::read_header(bytes, header)) {
            return invalid_decode(bytes, "failed to read packet header");
        }

        // Helper that wraps invalid_decode with the parsed header fields.
        auto invalid_packet = [&](std::string error) {
            DecodeOutput output = invalid_decode(
                bytes, std::move(error),
                header.tick, header.sequence,
                header.baseline_sequence, header.snapshot_kind);
            output.report.delta = pipeline_validate::is_delta(pipeline)
                && header.snapshot_kind == SnapshotKind::Patch;
            return output;
        };

        // --- Header validation ---

        if (header.magic != pipeline_wire::packet_magic)
            return invalid_packet("invalid packet magic");
        if (header.protocol != pipeline_wire::protocol_version
            || header.schema != pipeline_wire::schema_version)
            return invalid_packet("unsupported packet version");
        if (header.decode_signature != pipeline_signature::make_pipeline_decode_signature(pipeline))
            return invalid_packet("packet decode signature does not match local pipeline");
        if (header.packet_kind != PipelinePacketKind::Snapshot)
            return invalid_packet("unsupported packet kind");
        if (header.snapshot_kind != SnapshotKind::FullReplace
            && header.snapshot_kind != SnapshotKind::Patch)
            return invalid_packet("unsupported snapshot kind");
        if (header.flags != PipelinePacketFlags::None)
            return invalid_packet("unsupported packet flags");
        if (header.sequence == 0U)
            return invalid_packet("packet sequence 0 is reserved");
        if (header.sequence <= client_state.latest_remote_sequence)
            return invalid_packet("stale or out-of-order packet sequence");
        if (!checked_payload_size(header, bytes.size()))
            return invalid_packet("packet payload size does not match header");

        // --- Patch-kind specific checks ---

        bool const delta_enabled = pipeline_validate::is_delta(pipeline);

        if (header.snapshot_kind == SnapshotKind::FullReplace) {
            if (header.baseline_sequence != 0U)
                return invalid_packet("full snapshot baseline sequence must be 0");
            if (header.delete_count != 0U)
                return invalid_packet("full snapshot delete count must be 0");
        } else if (delta_enabled) {
            if (header.baseline_sequence == 0U)
                return invalid_packet("delta patch baseline sequence 0 is reserved");
            if (header.baseline_sequence >= header.sequence)
                return invalid_packet("delta patch baseline must precede packet sequence");
            if (header.baseline_sequence != input.applied_baseline_sequence)
                return invalid_packet("delta patch baseline does not match applied baseline");
        } else {
            if (header.baseline_sequence != 0U)
                return invalid_packet("non-delta patch baseline sequence must be 0");
            if (header.delete_count != 0U)
                return invalid_packet("non-delta patch delete count must be 0");
        }

        // --- Payload layout / size verification ---

        pipeline_records::RecordLayout const layout = pipeline_records::resolve_record_layout(pipeline);
        std::uint32_t const record_bytes = layout.record_bytes;

        std::uint64_t const expected_payload =
            static_cast<std::uint64_t>(header.delete_count) * pipeline_wire::delete_record_bytes
            + static_cast<std::uint64_t>(header.upsert_count) * record_bytes;
        if (expected_payload != header.payload_bytes)
            return invalid_packet("packet payload counts do not match payload size");

        // --- Decode records ---

        std::size_t offset = pipeline_wire::header_bytes;
        ClientSnapshotPatch patch {};
        patch.tick = header.tick;
        patch.kind = header.snapshot_kind;
        patch.reserve(header.upsert_count, header.delete_count);

        for (std::uint32_t i = 0; i < header.delete_count; ++i) {
            EntityNetId id {};
            if (!pipeline_wire::read_u32(bytes, offset, id))
                return invalid_packet("truncated delete id data");
            patch.deletes.push_back(id);
        }

        for (std::uint32_t i = 0; i < header.upsert_count; ++i) {
            BoidState boid {};
            if (!pipeline_records::read_record(bytes, offset, layout, boid))
                return invalid_packet("truncated upsert record data");
            patch.upserts.push_back(boid);
        }

        if (offset != bytes.size())
            return invalid_packet("packet has trailing bytes");

        // --- Patch validity ---

        SnapshotValidationResult const validation = validate_client_snapshot_patch(patch);
        if (!validation.valid)
            return invalid_packet("decoded patch is invalid: " + validation.message);

        // --- Finalise ---

        client_state.latest_remote_sequence = header.sequence;

        DecodeReport report {};
        report.tick              = header.tick;
        report.sequence          = header.sequence;
        report.baseline_sequence = header.baseline_sequence;
        report.snapshot_kind     = header.snapshot_kind;
        report.upsert_count      = header.upsert_count;
        report.delete_count      = header.delete_count;
        report.packet_bytes      = static_cast<std::uint32_t>(bytes.size());
        report.delta             = delta_enabled && header.snapshot_kind == SnapshotKind::Patch;
        report.valid             = true;

        return { .patch = std::move(patch), .report = std::move(report) };
    }
}
