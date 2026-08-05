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

import :api;
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
        std::string error,
        simnet::Tick tick = 0,
        simnet::SequenceId sequence = 0,
        simnet::SequenceId baseline_sequence = 0,
        simnet::SnapshotKind snapshot_kind = simnet::SnapshotKind::FullReplace
    )
    {
        simnet::DecodeOutput output{};
        output.report.tick = tick;
        output.report.sequence = sequence;
        output.report.baseline_sequence = baseline_sequence;
        output.report.snapshot_kind = snapshot_kind;
        output.report.valid = false;
        output.report.error = std::move(error);
        return output;
    }

    /// Verifies that the encoded update payload size matches the header claim.
    [[nodiscard]] bool checked_payload_size(
        simnet::pipeline_wire::EncodedUpdateHeader const& header,
        std::size_t byte_count
    ) noexcept
    {
        if (byte_count < simnet::pipeline_wire::header_bytes) {
            return false;
        }
        return header.payload_bytes == byte_count - simnet::pipeline_wire::header_bytes;
    }
}

// --- Decode encoded update ---

namespace simnet
{
    DecodeOutput decode_update(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        DecodeInput const& input
    )
    {
        pipeline_validate::require_supported_pipeline_definition(pipeline);
        pipeline_validate::require_quantization_settings(pipeline);
        ByteSpan const bytes = input.bytes;

        // --- Size sanity checks ---

        if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
            return invalid_decode("encoded update byte count exceeds uint32 range");
        }
        if (bytes.size() < pipeline_wire::header_bytes) {
            return invalid_decode("encoded update is shorter than header");
        }

        // --- Read header ---

        pipeline_wire::EncodedUpdateHeader header{};
        if (!pipeline_wire::read_header(bytes, header)) {
            return invalid_decode("failed to read encoded update header");
        }

        // Helper that wraps invalid_decode with the parsed header fields.
        auto invalid_update = [&](std::string error) {
            DecodeOutput output = invalid_decode(
                std::move(error),
                header.tick,
                header.sequence,
                header.baseline_sequence,
                header.snapshot_kind
            );
            return output;
        };

        // --- Header validation ---

        if (header.magic != pipeline_wire::encoded_update_magic)
            return invalid_update("invalid encoded update magic");
        if (header.protocol != pipeline_wire::protocol_version
            || header.schema != pipeline_wire::schema_version)
            return invalid_update("unsupported encoded update version");
        if (header.decode_signature != pipeline_signature::make_pipeline_decode_signature(pipeline))
            return invalid_update("encoded update signature does not match local pipeline");
        if (header.snapshot_kind != SnapshotKind::FullReplace
            && header.snapshot_kind != SnapshotKind::Patch)
            return invalid_update("unsupported snapshot kind");
        if (header.sequence == 0U)
            return invalid_update("encoded update sequence 0 is reserved");
        if (header.sequence <= client_state.latest_remote_sequence)
            return invalid_update("stale or out-of-order encoded update sequence");
        if (!checked_payload_size(header, bytes.size()))
            return invalid_update("encoded update payload size does not match header");

        // --- Patch-kind specific checks ---

        bool const delta_enabled = pipeline_validate::is_delta(pipeline);
        bool const incremental_enabled = pipeline_validate::is_incremental(pipeline);

        if (header.snapshot_kind == SnapshotKind::FullReplace) {
            if (header.baseline_sequence != 0U)
                return invalid_update("full snapshot baseline sequence must be 0");
        } else if (delta_enabled) {
            if (header.baseline_sequence == 0U)
                return invalid_update("delta patch baseline sequence 0 is reserved");
            if (header.baseline_sequence >= header.sequence)
                return invalid_update("delta patch baseline must precede update sequence");
        } else {
            if (header.baseline_sequence != 0U)
                return invalid_update("non-delta patch baseline sequence must be 0");
            if (!incremental_enabled && header.delete_count != 0U)
                return invalid_update("non-delta patch delete count must be 0");
        }

        // --- Payload layout / size verification ---

        pipeline_records::RecordLayout const layout
            = pipeline_records::resolve_record_layout(pipeline);
        std::uint32_t const record_bytes = layout.record_bytes;

        std::uint64_t const expected_payload
            = static_cast<std::uint64_t>(header.delete_count) * pipeline_wire::delete_record_bytes
            + static_cast<std::uint64_t>(header.upsert_count) * record_bytes;
        if (expected_payload != header.payload_bytes)
            return invalid_update("encoded update payload counts do not match payload size");

        // --- Decode records ---

        std::size_t offset = pipeline_wire::header_bytes;
        SnapshotUpdate patch{};
        patch.tick = header.tick;
        patch.kind = header.snapshot_kind;
        patch.reserve(header.upsert_count, header.delete_count);

        for (std::uint32_t i = 0; i < header.delete_count; ++i) {
            EntityNetId id{};
            if (!pipeline_wire::read_u32(bytes, offset, id))
                return invalid_update("truncated delete id data");
            patch.deletes.push_back(id);
        }

        for (std::uint32_t i = 0; i < header.upsert_count; ++i) {
            EntityState boid{};
            if (!pipeline_records::read_record(bytes, offset, layout, boid))
                return invalid_update("truncated upsert record data");
            patch.upserts.push_back(boid);
        }

        if (offset != bytes.size())
            return invalid_update("encoded update has trailing bytes");

        // --- Update validity ---

        SnapshotValidationResult const validation = validate_client_snapshot_patch(patch);
        if (!validation.valid)
            return invalid_update("decoded update is invalid: " + validation.message);

        client_state.latest_remote_sequence = header.sequence;

        DecodeReport report{};
        report.tick = header.tick;
        report.sequence = header.sequence;
        report.baseline_sequence = header.baseline_sequence;
        report.snapshot_kind = header.snapshot_kind;
        report.valid = true;

        return {.update = std::move(patch), .report = std::move(report)};
    }
}
