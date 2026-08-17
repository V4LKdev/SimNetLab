module;

#include <cstddef>
#include <algorithm>
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
import :lod;
import :signature;
import :validate;
import simnet.core;
import simnet.snapshot;

// --- Internal helpers - result builder, cursor ---

namespace
{
    /// Builds a skipped EncodeOutput without touching client state.
    [[nodiscard]] simnet::EncodeOutput skipped_encode(
        simnet::WorldSnapshot const& snapshot,
        simnet::RepresentationReport representation
    )
    {
        simnet::EncodeReport report{};
        report.tick = snapshot.tick;
        report.sequence = 0;
        report.baseline_sequence = 0;
        report.snapshot_kind = simnet::SnapshotKind::FullReplace;
        report.representation = representation;
        // remaining fields stay zero

        return {
            .kind = simnet::EncodeResultKind::Skipped,
            .update = {},
            .report = report,
            .resulting_snapshot = {},
        };
    }

    /// Wraps the incremental cursor so it never wraps past the entity count.
    [[nodiscard]] std::uint32_t next_incremental_cursor(
        std::size_t entity_count,
        std::uint32_t cursor,
        std::uint32_t selected_count
    ) noexcept
    {
        if (entity_count == 0)
        {
            return 0;
        }
        auto const next = (static_cast<std::uint64_t>(cursor) + selected_count) % entity_count;
        return static_cast<std::uint32_t>(next);
    }
}

// --- Encode snapshot ---

namespace simnet
{
    std::uint64_t pipeline_decode_signature(PipelineDefinition const& definition) noexcept
    {
        return pipeline_signature::make_pipeline_decode_signature(definition);
    }

    void validate_pipeline_definition(PipelineDefinition const& pipeline)
    {
        pipeline_validate::require_supported_pipeline_definition(pipeline);
        pipeline_validate::require_send_interval_settings(pipeline);
        pipeline_validate::require_incremental_settings(pipeline);
        pipeline_validate::require_quantization_settings(pipeline);
        pipeline_validate::require_area_of_interest_settings(pipeline);
        pipeline_validate::require_level_of_detail_settings(pipeline);
    }

    bool should_emit_snapshot(PipelineDefinition const& pipeline, Tick tick)
    {
        if (!has_all_flags(pipeline.techniques, PipelineTechniqueFlags::SendInterval))
        {
            return true;
        }

        pipeline_validate::require_send_interval_settings(pipeline);
        return (tick % pipeline.send_interval.interval_ticks) == 0U;
    }

    RepresentationReport measure_representation_quality(
        PipelineDefinition const& pipeline,
        WorldSnapshot const& source_snapshot,
        SnapshotUpdate const& represented_update
    )
    {
        validate_pipeline_definition(pipeline);
        pipeline_validate::require_snapshot(
            &source_snapshot,
            "representation quality source snapshot"
        );
        if (represented_update.tick != source_snapshot.tick)
        {
            throw std::runtime_error(
                "representation quality update tick does not match the source snapshot"
            );
        }

        auto const layout = pipeline_records::resolve_record_layout(pipeline);
        auto report = pipeline_records::make_representation_report(layout);
        auto source_index = std::size_t{};
        auto previous_id = EntityNetId{};
        for (auto const& canonical : represented_update.upserts)
        {
            if (canonical.id == 0U || canonical.id <= previous_id)
            {
                throw std::runtime_error(
                    "representation quality upserts must have nonzero ascending IDs"
                );
            }
            while (source_index < source_snapshot.size() &&
                   source_snapshot.ids[source_index] < canonical.id)
            {
                ++source_index;
            }
            if (source_index == source_snapshot.size() ||
                source_snapshot.ids[source_index] != canonical.id)
            {
                throw std::runtime_error(
                    "representation quality upsert is absent from the source snapshot"
                );
            }
            pipeline_records::observe_representation_quality(
                report,
                source_snapshot.positions[source_index],
                source_snapshot.headings[source_index],
                canonical
            );
            previous_id = canonical.id;
        }
        return report;
    }

    EncodeOutput encode_snapshot_unchecked(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        PipelineScratch& scratch,
        EncodeInput const& input
    )
    {
        validate_pipeline_definition(pipeline);
        pipeline_validate::require_snapshot_pointer(input.snapshot, "encode input snapshot");

        bool const delta_enabled = pipeline_validate::is_delta(pipeline);
        bool const delta_field_mask_enabled = pipeline_wire::field_mask_enabled(pipeline);
        bool const level_of_detail_enabled = pipeline_validate::is_level_of_detail(pipeline);
        if (input.baseline_snapshot == nullptr)
        {
            if (input.baseline_sequence != 0U)
            {
                throw std::runtime_error("baseline sequence requires a baseline snapshot");
            }
        }
        else
        {
            if (!delta_enabled && !level_of_detail_enabled)
            {
                throw std::runtime_error("baseline snapshot requires Delta or level of detail");
            }
            if (input.baseline_sequence == 0U)
            {
                throw std::runtime_error("delta baseline sequence 0 is reserved");
            }
        }
        if (input.replica_snapshot == nullptr)
        {
            if (input.replica_sequence != 0U)
            {
                throw std::runtime_error("replica sequence requires a replica snapshot");
            }
        }
        else if (input.replica_sequence == 0U)
        {
            throw std::runtime_error("replica baseline sequence 0 is reserved");
        }
        if (level_of_detail_enabled && input.replica_snapshot != nullptr)
        {
            throw std::runtime_error("level of detail uses the explicit baseline snapshot");
        }
        auto previous_recovery_id = EntityNetId{};
        for (auto const id : input.recovery_upsert_ids)
        {
            if (id == 0U || id <= previous_recovery_id)
            {
                throw std::runtime_error("recovery upsert IDs must be nonzero and ascending");
            }
            previous_recovery_id = id;
        }
        if (input.force_full_replace &&
            (input.baseline_snapshot != nullptr || input.replica_snapshot != nullptr ||
             !input.recovery_upsert_ids.empty()))
        {
            throw std::runtime_error("forced FullReplace accepts no baseline or recovery upserts");
        }

        WorldSnapshot const& snapshot = *input.snapshot;
        pipeline_validate::require_u32_count(snapshot.size(), "snapshot entity count");
        pipeline_records::RecordLayout const layout =
            pipeline_records::resolve_record_layout(pipeline);
        auto representation = pipeline_records::make_representation_report(layout);

        // --- Send interval ---

        if (!should_emit_snapshot(pipeline, snapshot.tick))
        {
            return skipped_encode(snapshot, representation);
        }

        bool const area_of_interest_enabled =
            pipeline.area_of_interest.mode != AreaOfInterestMode::None;
        if (area_of_interest_enabled && input.interest_source == nullptr)
        {
            throw std::runtime_error("area of interest requires an interest source");
        }
        if (area_of_interest_enabled)
        {
            pipeline_validate::require_interest_source(*input.interest_source);
            pipeline_validate::require_candidate_indices(input.candidate_indices, snapshot.size());
        }

        // --- Sequence allocation ---

        SequenceId const sequence = client_state.next_sequence;
        if (sequence == 0U)
        {
            throw std::runtime_error("pipeline sequence 0 is reserved");
        }
        if (sequence == std::numeric_limits<SequenceId>::max())
        {
            throw std::runtime_error("pipeline sequence would wrap to reserved 0");
        }
        if (input.baseline_snapshot != nullptr && input.baseline_sequence >= sequence)
        {
            throw std::runtime_error("delta baseline sequence must precede update sequence");
        }
        if (input.replica_snapshot != nullptr && input.replica_sequence >= sequence)
        {
            throw std::runtime_error("replica baseline sequence must precede update sequence");
        }

        // --- Relevancy selection ---

        auto area_of_interest = AreaOfInterestReport{
            .source_available = true,
            .source_entity_count = static_cast<std::uint32_t>(snapshot.size()),
        };
        WorldSnapshot const* selected_snapshot = &snapshot;
        if (area_of_interest_enabled)
        {
            if (input.interest_source->source_entity_id != 0U &&
                !std::ranges::binary_search(snapshot.ids, input.interest_source->source_entity_id))
            {
                throw std::runtime_error("AOI Player source entity is absent from the snapshot");
            }
            pipeline_selection::select_area_of_interest(
                scratch,
                snapshot,
                pipeline.area_of_interest,
                *input.interest_source,
                input.candidate_indices
            );
            selected_snapshot = &scratch.relevant_snapshot;
            pipeline_validate::require_u32_count(
                input.candidate_indices.size(),
                "AOI candidate count"
            );
            area_of_interest.candidate_count =
                static_cast<std::uint32_t>(input.candidate_indices.size());
            area_of_interest.retained_count = static_cast<std::uint32_t>(selected_snapshot->size());
        }
        else
        {
            area_of_interest.candidate_count = static_cast<std::uint32_t>(snapshot.size());
            area_of_interest.retained_count = static_cast<std::uint32_t>(snapshot.size());
        }
        area_of_interest.culled_count =
            area_of_interest.source_entity_count - area_of_interest.retained_count;

        // --- Update scheduling ---

        bool const incremental_enabled = pipeline_validate::is_incremental(pipeline);
        bool const schedule_level_of_detail = level_of_detail_enabled &&
                                              client_state.level_of_detail_seeded &&
                                              !input.force_full_replace;
        if (level_of_detail_enabled && !client_state.level_of_detail_seeded &&
            input.baseline_snapshot != nullptr)
        {
            throw std::runtime_error("initial level-of-detail synchronization must be FullReplace");
        }
        if (schedule_level_of_detail && input.baseline_snapshot == nullptr)
        {
            throw std::runtime_error("level-of-detail Patch requires an explicit baseline");
        }
        bool const emit_delta =
            !input.force_full_replace && delta_enabled && input.baseline_snapshot != nullptr;
        bool const seed_incremental = !level_of_detail_enabled && incremental_enabled &&
                                      !delta_enabled && !client_state.incremental_seeded &&
                                      !input.force_full_replace;
        bool const schedule_incremental =
            incremental_enabled &&
            (schedule_level_of_detail || (!level_of_detail_enabled && !input.force_full_replace &&
                                          !seed_incremental && (!delta_enabled || emit_delta)));
        bool const emit_patch = schedule_level_of_detail || emit_delta || schedule_incremental;
        auto incremental_selection_count = std::size_t{};
        auto level_of_detail_cursor = client_state.incremental_cursor;
        if (input.replica_snapshot != nullptr &&
            (!incremental_enabled || delta_enabled || level_of_detail_enabled))
        {
            throw std::runtime_error(
                "replica snapshot is only valid for non-delta incremental encoding"
            );
        }
        if (schedule_incremental && !level_of_detail_enabled && !delta_enabled &&
            input.replica_snapshot == nullptr)
        {
            throw std::runtime_error("incremental patch requires the latest replica snapshot");
        }
        if (!input.recovery_upsert_ids.empty() && !emit_patch)
        {
            throw std::runtime_error("recovery upserts require a partial Patch");
        }

        auto recovery_forced_addition_count = std::uint32_t{};
        auto level_of_detail = LevelOfDetailReport{};
        if (level_of_detail_enabled)
        {
            level_of_detail = pipeline_lod::reconcile_schedule(
                client_state,
                scratch,
                *selected_snapshot,
                pipeline.level_of_detail,
                *input.interest_source
            );
            if (schedule_level_of_detail)
            {
                level_of_detail_cursor = pipeline_lod::select_pending_indices(
                    scratch.level_of_detail_schedule,
                    *selected_snapshot,
                    input.interest_source->source_entity_id,
                    incremental_enabled,
                    client_state.incremental_cursor,
                    pipeline.incremental.max_entities_per_update,
                    scratch.selected_indices
                );
                pipeline_lod::service_selected_indices(
                    scratch.level_of_detail_schedule,
                    *selected_snapshot,
                    pipeline.level_of_detail,
                    scratch.selected_indices,
                    level_of_detail
                );
                recovery_forced_addition_count =
                    pipeline_selection::merge_recovery_upsert_indices(
                        scratch,
                        *selected_snapshot,
                        input.recovery_upsert_ids
                    );
                level_of_detail.recovery_forced_count = recovery_forced_addition_count;
                if (!delta_enabled)
                {
                    pipeline_selection::select_replica_deletes(
                        scratch,
                        *selected_snapshot,
                        *input.baseline_snapshot
                    );
                }
            }
            else
            {
                pipeline_lod::service_full_replace(
                    scratch.level_of_detail_schedule,
                    scratch,
                    *selected_snapshot,
                    pipeline.level_of_detail,
                    level_of_detail
                );
            }
        }
        else if (schedule_incremental)
        {
            scratch.selected_delete_ids.clear();
            pipeline_selection::select_incremental_indices(
                scratch,
                selected_snapshot->size(),
                client_state.incremental_cursor,
                pipeline.incremental.max_entities_per_update
            );
            incremental_selection_count = scratch.selected_indices.size();
            recovery_forced_addition_count = pipeline_selection::merge_recovery_upsert_indices(
                scratch,
                *selected_snapshot,
                input.recovery_upsert_ids
            );
            if (!delta_enabled)
            {
                pipeline_selection::select_replica_deletes(
                    scratch,
                    *selected_snapshot,
                    *input.replica_snapshot
                );
            }
        }

        // --- Delta selection ---

        auto delta = DeltaReport{};
        if (emit_delta)
        {
            pipeline_validate::require_u32_count(
                input.baseline_snapshot->size(),
                "baseline snapshot entity count"
            );
            if (schedule_level_of_detail || schedule_incremental)
            {
                delta = pipeline_selection::filter_scheduled_delta_records(
                    scratch,
                    *selected_snapshot,
                    *input.baseline_snapshot,
                    layout,
                    delta_field_mask_enabled
                );
            }
            else
            {
                delta = pipeline_selection::select_delta_records(
                    scratch,
                    *selected_snapshot,
                    *input.baseline_snapshot,
                    layout,
                    delta_field_mask_enabled
                );
            }
        }
        else
        {
            scratch.prepared_record_bytes.clear();
            if (!emit_patch)
            {
                scratch.selected_delete_ids.clear();
            }
            if (!emit_patch)
            {
                scratch.selected_indices.clear();
            }
        }

        if (level_of_detail_enabled)
        {
            pipeline_lod::count_represented(
                scratch.level_of_detail_schedule,
                scratch.selected_indices,
                level_of_detail
            );
        }

        std::size_t const selected_count =
            emit_patch ? scratch.selected_indices.size() : selected_snapshot->size();
        std::size_t const delete_count = emit_patch ? scratch.selected_delete_ids.size() : 0U;
        SnapshotKind const snapshot_kind =
            emit_patch ? SnapshotKind::Patch : SnapshotKind::FullReplace;
        auto baseline_sequence = SequenceId{};
        if (schedule_level_of_detail || emit_delta)
        {
            baseline_sequence = input.baseline_sequence;
        }
        else if (schedule_incremental)
        {
            baseline_sequence = input.replica_sequence;
        }

        // --- Representation encoding and layout ---

        std::uint32_t const record_bytes = layout.record_bytes;

        auto const upsert_byte_count =
            emit_delta ? scratch.prepared_record_bytes.size()
                       : selected_count * static_cast<std::size_t>(record_bytes);
        std::size_t const payload_byte_count =
            delete_count * static_cast<std::size_t>(pipeline_wire::delete_record_bytes) +
            upsert_byte_count;

        if (payload_byte_count > std::numeric_limits<std::uint32_t>::max() ||
            payload_byte_count + pipeline_wire::header_bytes >
                std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error("encoded update exceeds uint32 byte range");
        }
        std::uint32_t const payload_bytes = static_cast<std::uint32_t>(payload_byte_count);

        pipeline_wire::EncodedUpdateHeader const header{
            .magic = pipeline_wire::encoded_update_magic,
            .protocol = pipeline_wire::protocol_version,
            .schema = pipeline_wire::encoded_update_schema(pipeline),
            .decode_signature = pipeline_signature::make_pipeline_decode_signature(pipeline),
            .snapshot_kind = snapshot_kind,
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

        for (EntityNetId const id : scratch.selected_delete_ids)
        {
            simnet::append_big_endian(scratch.bytes, id);
        }

        if (!emit_delta)
        {
            scratch.logical_update.clear();
        }
        else
        {
            scratch.logical_update.deletes.clear();
        }
        scratch.logical_update.tick = selected_snapshot->tick;
        scratch.logical_update.kind = snapshot_kind;
        scratch.logical_update.reserve(selected_count, delete_count);
        scratch.logical_update.deletes.assign(
            scratch.selected_delete_ids.begin(),
            scratch.selected_delete_ids.end()
        );

        auto prepare_and_write_record = [&](std::size_t source_index)
        {
            auto const prepared = pipeline_records::prepare_record(
                layout,
                selected_snapshot->ids[source_index],
                selected_snapshot->classifications[source_index],
                selected_snapshot->positions[source_index],
                selected_snapshot->headings[source_index],
                selected_snapshot->hues[source_index]
            );
            pipeline_records::write_prepared_record(scratch.bytes, layout, prepared);
            scratch.logical_update.upserts.push_back(prepared.canonical);
        };

        if (emit_delta)
        {
            scratch.bytes.insert(
                scratch.bytes.end(),
                scratch.prepared_record_bytes.begin(),
                scratch.prepared_record_bytes.end()
            );
        }
        else if (emit_patch)
        {
            for (std::uint32_t const source_index : scratch.selected_indices)
            {
                prepare_and_write_record(source_index);
            }
        }
        else
        {
            for (std::size_t source_index = 0; source_index < selected_snapshot->size();
                 ++source_index)
            {
                prepare_and_write_record(source_index);
            }
        }

        auto resulting_snapshot = WorldSnapshot{};
        WorldSnapshot const* reconstruction_baseline = nullptr;
        if (snapshot_kind == SnapshotKind::Patch)
        {
            reconstruction_baseline = schedule_level_of_detail || emit_delta
                                          ? input.baseline_snapshot
                                          : input.replica_snapshot;
        }
        auto const reconstruction = reconstruct_world_snapshot_unchecked(
            reconstruction_baseline,
            scratch.logical_update,
            resulting_snapshot
        );
        if (!reconstruction.valid)
        {
            throw std::runtime_error(
                "failed to reconstruct encoded Client result: " + reconstruction.message
            );
        }

        // --- Encoded update / report ---

        EncodedUpdate update{
            .sequence = sequence,
            .bytes = scratch.bytes,
        };

        EncodeReport report{};
        report.tick = snapshot.tick;
        report.sequence = sequence;
        report.baseline_sequence = baseline_sequence;
        report.snapshot_kind = snapshot_kind;
        report.upsert_count = static_cast<std::uint32_t>(selected_count);
        report.delete_count = static_cast<std::uint32_t>(delete_count);
        report.recovery_forced_addition_count = recovery_forced_addition_count;
        report.representation = representation;
        report.delta = delta;
        report.area_of_interest = area_of_interest;
        if (level_of_detail_enabled)
        {
            level_of_detail.deletions_bypassing_count = report.delete_count;
            level_of_detail.encoded_bytes = static_cast<std::uint32_t>(update.bytes.size());
            report.level_of_detail = level_of_detail;
        }

        EncodeOutput output;
        output.kind = EncodeResultKind::Update;
        output.update = std::move(update);
        output.report = report;
        output.resulting_snapshot = std::move(resulting_snapshot);

        // --- Update client state ---

        if (schedule_incremental && !level_of_detail_enabled)
        {
            client_state.incremental_cursor = next_incremental_cursor(
                selected_snapshot->size(),
                client_state.incremental_cursor,
                static_cast<std::uint32_t>(incremental_selection_count)
            );
        }
        if (incremental_enabled && snapshot_kind == SnapshotKind::FullReplace)
        {
            client_state.incremental_seeded = true;
        }
        if (level_of_detail_enabled && snapshot_kind == SnapshotKind::FullReplace)
        {
            client_state.level_of_detail_seeded = true;
        }
        if (level_of_detail_enabled)
        {
            client_state.incremental_cursor = level_of_detail_cursor;
            client_state.level_of_detail_schedule.swap(scratch.level_of_detail_schedule);
        }
        client_state.next_sequence = sequence + 1U;
        return output;
    }

    EncodeOutput encode_snapshot(
        PipelineDefinition const& pipeline,
        ClientReplicationState& client_state,
        PipelineScratch& scratch,
        EncodeInput const& input
    )
    {
        pipeline_validate::require_snapshot(input.snapshot, "encode input snapshot");
        if (input.baseline_snapshot != nullptr)
        {
            pipeline_validate::require_snapshot(
                input.baseline_snapshot,
                "encode baseline snapshot"
            );
        }
        if (input.replica_snapshot != nullptr)
        {
            pipeline_validate::require_snapshot(input.replica_snapshot, "encode replica snapshot");
        }

        return encode_snapshot_unchecked(pipeline, client_state, scratch, input);
    }
}
