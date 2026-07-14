module;

#include <cstddef>
#include <cstdint>
#include <limits>
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
import :selection;
import :signature;
import :validate;
import simnet.core;
import simnet.snapshot;

// --- Internal helpers - result builder, cursor ---

namespace
{
    /// Builds a skipped EncodeOutput without touching client state.
    [[nodiscard]] simnet::EncodeOutput skipped_encode(
        simnet::PipelineDefinition const& pipeline,
        simnet::WorldSnapshot const& snapshot,
        simnet::EncodeSkipReason reason)
    {
        simnet::EncodeReport report {};
        report.tick              = snapshot.tick;
        report.sequence          = 0;
        report.baseline_sequence = 0;
        report.snapshot_kind     = simnet::SnapshotKind::FullReplace;
        report.techniques        = pipeline.techniques;
        report.emitted           = false;
        report.skipped           = true;
        report.delta             = false;
        report.skip_reason       = reason;
        report.budget_exceeded   = false;
        report.input_entities    = static_cast<std::uint32_t>(snapshot.size());
        // remaining fields stay zero

        return {
            .kind        = simnet::EncodeResultKind::Skipped,
            .skip_reason = reason,
            .packet      = {},
            .report      = report,
        };
    }

    /// Wraps the incremental cursor so it never wraps past the entity count.
    [[nodiscard]] std::uint32_t next_incremental_cursor(
        std::size_t entity_count,
        std::uint32_t cursor,
        std::uint32_t selected_count) noexcept
    {
        if (entity_count == 0) {
            return 0;
        }
        auto const next = (static_cast<std::uint64_t>(cursor) + selected_count) % entity_count;
        return static_cast<std::uint32_t>(next);
    }
}

// --- Encode snapshot ---

namespace simnet
{
    PipelineDefinition make_raw_snapshot_pipeline(PacketBudget budget)
    {
        return {
            .techniques = PipelineTechniqueFlags::None,
            .budget = budget,
        };
    }

    std::uint64_t pipeline_decode_signature(PipelineDefinition const& definition) noexcept
    {
        return pipeline_signature::make_pipeline_decode_signature(definition);
    }

    EncodeOutput encode_snapshot(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        PipelineScratch& scratch,
        EncodeInput const& input)
    {
        // --- Validation ---

        pipeline_validate::require_raw_snapshot(pipeline);
        pipeline_validate::require_send_interval_settings(pipeline);
        pipeline_validate::require_incremental_settings(pipeline);
        pipeline_validate::require_quantization_settings(pipeline);
        pipeline_validate::require_snapshot(input.snapshot, "encode input snapshot");

        bool const delta_enabled = pipeline_validate::is_delta(pipeline);
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
            pipeline_validate::require_snapshot(input.baseline_snapshot, "encode baseline snapshot");
        }

        WorldSnapshot const& snapshot = *input.snapshot;
        pipeline_validate::require_u32_count(snapshot.size(), "snapshot entity count");

        // --- Send interval ---

        if (!pipeline_selection::should_emit_for_send_interval(pipeline, snapshot.tick)) {
            return skipped_encode(pipeline, snapshot, EncodeSkipReason::SendInterval);
        }

        // --- Sequence allocation ---

        SequenceId const sequence = client_state.next_sequence;
        if (sequence == 0U) {
            throw std::runtime_error("pipeline sequence 0 is reserved");
        }
        if (sequence == std::numeric_limits<SequenceId>::max()) {
            throw std::runtime_error("pipeline sequence would wrap to reserved 0");
        }
        if (input.baseline_snapshot != nullptr && input.baseline_sequence >= sequence) {
            throw std::runtime_error("delta baseline sequence must precede packet sequence");
        }

        // --- Entity selection ---

        bool const incremental_enabled = pipeline_validate::is_incremental(pipeline);
        bool const emit_delta = delta_enabled && input.baseline_snapshot != nullptr;

        if (emit_delta) {
            pipeline_validate::require_u32_count(input.baseline_snapshot->size(), "baseline snapshot entity count");
            pipeline_selection::select_delta_records(scratch, snapshot, *input.baseline_snapshot);
        } else if (incremental_enabled) {
            scratch.selected_delete_ids.clear();
            pipeline_selection::select_incremental_indices(
                scratch,
                snapshot.size(),
                client_state.incremental_cursor,
                pipeline.incremental.max_entities_per_packet);
        } else {
            scratch.selected_indices.clear();
            scratch.selected_delete_ids.clear();
        }

        std::size_t const selected_count = (emit_delta || incremental_enabled)
            ? scratch.selected_indices.size()
            : snapshot.size();
        std::size_t const delete_count   = emit_delta ? scratch.selected_delete_ids.size() : 0U;
        SnapshotKind const snapshot_kind = (emit_delta || incremental_enabled)
            ? SnapshotKind::Patch
            : SnapshotKind::FullReplace;
        SequenceId const baseline_sequence = emit_delta ? input.baseline_sequence : 0U;

        // --- Payload layout / sizing ---

        pipeline_records::RecordLayout const layout = pipeline_records::resolve_record_layout(pipeline);
        std::uint32_t const record_bytes = layout.record_bytes;

        std::size_t const payload_byte_count =
            delete_count * static_cast<std::size_t>(pipeline_wire::delete_record_bytes)
            + selected_count * static_cast<std::size_t>(record_bytes);

        if (payload_byte_count > std::numeric_limits<std::uint32_t>::max()
            || payload_byte_count + pipeline_wire::header_bytes > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("encoded raw snapshot packet exceeds uint32 byte range");
        }
        std::uint32_t const payload_bytes = static_cast<std::uint32_t>(payload_byte_count);

        pipeline_wire::PacketHeader const header {
            .magic             = pipeline_wire::packet_magic,
            .protocol          = pipeline_wire::protocol_version,
            .schema            = pipeline_wire::schema_version,
            .decode_signature  = pipeline_signature::make_pipeline_decode_signature(pipeline),
            .snapshot_kind     = snapshot_kind,
            .tick              = snapshot.tick,
            .sequence          = sequence,
            .baseline_sequence = baseline_sequence,
            .upsert_count      = static_cast<std::uint32_t>(selected_count),
            .delete_count      = static_cast<std::uint32_t>(delete_count),
            .payload_bytes     = payload_bytes,
        };

        // --- Serialise ---

        scratch.bytes.clear();
        scratch.bytes.reserve(pipeline_wire::header_bytes + payload_bytes);
        pipeline_wire::write_header(scratch.bytes, header);

        for (EntityNetId const id : scratch.selected_delete_ids) {
            pipeline_wire::write_u32(scratch.bytes, id);
        }

        auto write_record = [&](std::size_t source_index) {
            pipeline_records::write_record(
                scratch.bytes, layout,
                snapshot.ids[source_index],
                snapshot.positions[source_index],
                snapshot.headings[source_index],
                snapshot.hues[source_index]);
        };

        if (emit_delta || incremental_enabled) {
            for (std::uint32_t const idx : scratch.selected_indices) {
                write_record(idx);
            }
        } else {
            for (std::size_t idx = 0; idx < snapshot.size(); ++idx) {
                write_record(idx);
            }
        }

        // --- Packet / report ---

        EncodedPacket packet {
            .tick              = snapshot.tick,
            .sequence          = sequence,
            .baseline_sequence = baseline_sequence,
            .bytes             = scratch.bytes,
        };

        std::uint32_t const final_bytes = static_cast<std::uint32_t>(packet.bytes.size());

        EncodeReport report {};
        report.tick              = snapshot.tick;
        report.sequence          = sequence;
        report.baseline_sequence = baseline_sequence;
        report.snapshot_kind     = snapshot_kind;
        report.techniques        = pipeline.techniques;
        report.emitted           = true;
        report.skipped           = false;
        report.delta             = emit_delta;
        report.skip_reason       = EncodeSkipReason::None;
        report.budget_exceeded   = final_bytes > pipeline.budget.max_packet_bytes;
        report.input_entities    = static_cast<std::uint32_t>(snapshot.size());
        report.selected_entities = static_cast<std::uint32_t>(selected_count);
        report.upsert_count      = static_cast<std::uint32_t>(selected_count);
        report.delete_count      = static_cast<std::uint32_t>(delete_count);
        report.packet_bytes      = final_bytes;
        report.payload_bytes     = payload_bytes;
        report.uncompressed_bytes = final_bytes;
        report.final_bytes       = final_bytes;

        EncodeOutput output;
        output.kind        = EncodeResultKind::Packet;
        output.skip_reason = EncodeSkipReason::None;
        output.packet      = std::move(packet);
        output.report      = report;

        // --- Update client state ---

        if (incremental_enabled) {
            client_state.incremental_cursor = next_incremental_cursor(
                snapshot.size(),
                client_state.incremental_cursor,
                static_cast<std::uint32_t>(selected_count));
        }
        client_state.next_sequence = sequence + 1U;
        return output;
    }
}
