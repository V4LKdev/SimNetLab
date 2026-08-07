module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

/// @brief Private entity selection helpers.
module simnet.pipeline:selection;

import :types;
import :messages;
import :records;
import simnet.core;
import simnet.snapshot;

namespace simnet::pipeline_selection
{
    void append_snapshot_entity(
        WorldSnapshot const& source,
        std::size_t index,
        WorldSnapshot& destination
    )
    {
        destination.ids.push_back(source.ids[index]);
        destination.classifications.push_back(source.classifications[index]);
        destination.positions.push_back(source.positions[index]);
        destination.headings.push_back(source.headings[index]);
        destination.hues.push_back(source.hues[index]);
    }

    [[nodiscard]] bool inside_area_of_interest(
        AreaOfInterestSettings const& settings,
        InterestSource const& source,
        Vec3f position
    ) noexcept
    {
        auto const offset = position - source.position;
        auto const distance_squared = length_squared(offset);
        auto const radius_squared = settings.radius * settings.radius;
        if (distance_squared > radius_squared) {
            return false;
        }
        if (settings.mode == AreaOfInterestMode::Radius || distance_squared == 0.0F) {
            return true;
        }

        auto constexpr degrees_to_radians = 0.01745329251994329577F;
        auto const half_angle = settings.fov_degrees * 0.5F * degrees_to_radians;
        auto const cosine = std::cos(half_angle);
        auto const forward_distance = dot(source.forward, offset);
        if (forward_distance < 0.0F) {
            return false;
        }
        if (settings.fov_degrees == 180.0F) {
            return true;
        }
        return forward_distance * forward_distance >= distance_squared * cosine * cosine;
    }

    /// Builds the exact sorted AOI population from sorted coarse source indices.
    void select_area_of_interest(
        PipelineScratch& scratch,
        WorldSnapshot const& snapshot,
        AreaOfInterestSettings const& settings,
        InterestSource const& source,
        std::span<std::uint32_t const> candidate_indices
    )
    {
        auto normalized_source = source;
        normalized_source.forward = normalize_or(source.forward, {.z = 1.0F});
        scratch.relevant_source_indices.clear();
        scratch.relevant_source_indices.reserve(candidate_indices.size() + 1U);
        for (auto const source_index : candidate_indices) {
            if (inside_area_of_interest(
                    settings,
                    normalized_source,
                    snapshot.positions[source_index]
                )) {
                scratch.relevant_source_indices.push_back(source_index);
            }
        }

        if (source.source_entity_id != 0U) {
            auto const found = std::ranges::lower_bound(snapshot.ids, source.source_entity_id);
            if (found != snapshot.ids.end() && *found == source.source_entity_id) {
                auto const source_index
                    = static_cast<std::uint32_t>(std::distance(snapshot.ids.begin(), found));
                auto const insertion
                    = std::ranges::lower_bound(scratch.relevant_source_indices, source_index);
                if (insertion == scratch.relevant_source_indices.end()
                    || *insertion != source_index) {
                    scratch.relevant_source_indices.insert(insertion, source_index);
                }
            }
        }

        scratch.relevant_snapshot.clear();
        scratch.relevant_snapshot.tick = snapshot.tick;
        scratch.relevant_snapshot.reserve(scratch.relevant_source_indices.size());
        for (auto const source_index : scratch.relevant_source_indices) {
            append_snapshot_entity(snapshot, source_index, scratch.relevant_snapshot);
        }
    }

    /// Selects a round-robin slice of source indices.
    void select_incremental_indices(
        PipelineScratch& scratch,
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

        // WorldSnapshot IDs are strictly ascending, so sorting indices restores ID order after
        // cursor wraparound and satisfies patch validation.
        for (std::size_t offset = 0; offset < selected_count; ++offset) {
            auto const source_index = (start + offset) % entity_count;
            scratch.selected_indices.push_back(static_cast<std::uint32_t>(source_index));
        }
        std::ranges::sort(scratch.selected_indices);
    }

    /// Adds current indices for sorted recovery IDs while preserving strict ID order.
    [[nodiscard]] std::uint32_t merge_recovery_upsert_indices(
        PipelineScratch& scratch,
        WorldSnapshot const& current,
        std::span<EntityNetId const> recovery_ids
    )
    {
        auto const previous_size = scratch.selected_indices.size();
        for (auto const id : recovery_ids) {
            auto const found = std::ranges::lower_bound(current.ids, id);
            if (found != current.ids.end() && *found == id) {
                scratch.selected_indices.push_back(
                    static_cast<std::uint32_t>(std::distance(current.ids.begin(), found))
                );
            }
        }
        std::ranges::sort(scratch.selected_indices);
        auto const unique_end = std::ranges::unique(scratch.selected_indices).begin();
        scratch.selected_indices.erase(unique_end, scratch.selected_indices.end());
        return static_cast<std::uint32_t>(scratch.selected_indices.size() - previous_size);
    }

    [[nodiscard]] pipeline_records::PreparedRecord prepare_snapshot_record(
        pipeline_records::RecordLayout const& layout,
        WorldSnapshot const& snapshot,
        std::size_t index
    ) noexcept
    {
        return pipeline_records::prepare_record(
            layout,
            snapshot.ids[index],
            snapshot.classifications[index],
            snapshot.positions[index],
            snapshot.headings[index],
            snapshot.hues[index]
        );
    }

    void begin_delta_preparation(PipelineScratch& scratch, std::size_t maximum_upserts)
    {
        scratch.prepared_record_bytes.clear();
        scratch.logical_update.upserts.clear();
        scratch.logical_update.upserts.reserve(maximum_upserts);
    }

    void retain_prepared_record(
        PipelineScratch& scratch,
        pipeline_records::RecordLayout const& layout,
        pipeline_records::PreparedRecord const& prepared
    )
    {
        pipeline_records::write_prepared_record(scratch.prepared_record_bytes, layout, prepared);
        scratch.logical_update.upserts.push_back(prepared.canonical);
    }

    /// Selects changed/new upserts and baseline-only deletes from two sorted snapshots.
    [[nodiscard]] DeltaReport select_delta_records(
        PipelineScratch& scratch,
        WorldSnapshot const& current,
        WorldSnapshot const& baseline,
        pipeline_records::RecordLayout const& layout,
        bool collect_representation_quality,
        RepresentationReport& representation
    )
    {
        scratch.selected_indices.clear();
        scratch.selected_delete_ids.clear();
        scratch.selected_indices.reserve(current.size());
        scratch.selected_delete_ids.reserve(baseline.size());
        begin_delta_preparation(scratch, current.size());

        auto report = DeltaReport{};
        auto retain_spawn = [&](std::size_t current_index) {
            auto const prepared = prepare_snapshot_record(layout, current, current_index);
            scratch.selected_indices.push_back(static_cast<std::uint32_t>(current_index));
            if (collect_representation_quality) {
                pipeline_records::observe_representation_quality(
                    representation,
                    current.positions[current_index],
                    current.headings[current_index],
                    prepared
                );
            }
            retain_prepared_record(scratch, layout, prepared);
            ++report.candidate_count;
            ++report.spawned_count;
            ++report.produced_upsert_count;
        };

        auto current_index = std::size_t{};
        auto baseline_index = std::size_t{};

        while (current_index < current.size() && baseline_index < baseline.size()) {
            auto const current_id = current.ids[current_index];
            auto const baseline_id = baseline.ids[baseline_index];

            if (current_id < baseline_id) {
                retain_spawn(current_index);
                ++current_index;
            } else if (baseline_id < current_id) {
                scratch.selected_delete_ids.push_back(baseline_id);
                ++baseline_index;
            } else {
                auto const prepared = prepare_snapshot_record(layout, current, current_index);
                ++report.candidate_count;
                if (!pipeline_records::same_canonical_state(
                        prepared.canonical,
                        baseline,
                        baseline_index
                    )) {
                    scratch.selected_indices.push_back(static_cast<std::uint32_t>(current_index));
                    if (collect_representation_quality) {
                        pipeline_records::observe_representation_quality(
                            representation,
                            current.positions[current_index],
                            current.headings[current_index],
                            prepared
                        );
                    }
                    retain_prepared_record(scratch, layout, prepared);
                    ++report.changed_existing_count;
                    ++report.produced_upsert_count;
                } else {
                    ++report.unchanged_count;
                }
                ++current_index;
                ++baseline_index;
            }
        }

        while (current_index < current.size()) {
            retain_spawn(current_index);
            ++current_index;
        }
        while (baseline_index < baseline.size()) {
            scratch.selected_delete_ids.push_back(baseline.ids[baseline_index]);
            ++baseline_index;
        }
        return report;
    }

    /**
     * Filters scheduled upserts against a baseline and selects every baseline-only delete.
     *
     * Deleted entities have no current snapshot index and therefore cannot participate in the
     * round-robin schedule. Including every truthful delete prevents a partial update from
     * retaining entities that no longer exist.
     */
    [[nodiscard]] DeltaReport filter_scheduled_delta_records(
        PipelineScratch& scratch,
        WorldSnapshot const& current,
        WorldSnapshot const& baseline,
        pipeline_records::RecordLayout const& layout,
        bool collect_representation_quality,
        RepresentationReport& representation
    )
    {
        begin_delta_preparation(scratch, scratch.selected_indices.size());
        auto report = DeltaReport{};
        auto baseline_index = std::size_t{};
        auto retained_count = std::size_t{};
        for (std::uint32_t const current_index : scratch.selected_indices) {
            auto const prepared = prepare_snapshot_record(layout, current, current_index);
            ++report.candidate_count;
            auto const current_id = current.ids[current_index];
            while (baseline_index < baseline.size() && baseline.ids[baseline_index] < current_id) {
                ++baseline_index;
            }

            bool const existed
                = baseline_index < baseline.size() && baseline.ids[baseline_index] == current_id;
            auto unchanged = false;
            if (existed) {
                unchanged = pipeline_records::same_canonical_state(
                    prepared.canonical,
                    baseline,
                    baseline_index
                );
            }
            if (!unchanged) {
                scratch.selected_indices[retained_count++] = current_index;
                if (collect_representation_quality) {
                    pipeline_records::observe_representation_quality(
                        representation,
                        current.positions[current_index],
                        current.headings[current_index],
                        prepared
                    );
                }
                retain_prepared_record(scratch, layout, prepared);
                if (existed) {
                    ++report.changed_existing_count;
                } else {
                    ++report.spawned_count;
                }
                ++report.produced_upsert_count;
            } else {
                ++report.unchanged_count;
            }
        }
        scratch.selected_indices.resize(retained_count);

        scratch.selected_delete_ids.clear();
        scratch.selected_delete_ids.reserve(baseline.size());
        auto current_index = std::size_t{};
        baseline_index = 0;
        while (current_index < current.size() && baseline_index < baseline.size()) {
            auto const current_id = current.ids[current_index];
            auto const baseline_id = baseline.ids[baseline_index];
            if (current_id < baseline_id) {
                ++current_index;
            } else if (baseline_id < current_id) {
                scratch.selected_delete_ids.push_back(baseline_id);
                ++baseline_index;
            } else {
                ++current_index;
                ++baseline_index;
            }
        }
        while (baseline_index < baseline.size()) {
            scratch.selected_delete_ids.push_back(baseline.ids[baseline_index]);
            ++baseline_index;
        }
        return report;
    }

    /// Selects every entity that disappeared from the latest exact non-Delta replica.
    void select_replica_deletes(
        PipelineScratch& scratch,
        WorldSnapshot const& current,
        WorldSnapshot const& replica
    )
    {
        scratch.selected_delete_ids.clear();
        scratch.selected_delete_ids.reserve(replica.size());
        auto current_index = std::size_t{};
        auto replica_index = std::size_t{};
        while (current_index < current.size() && replica_index < replica.size()) {
            if (current.ids[current_index] < replica.ids[replica_index]) {
                ++current_index;
            } else if (replica.ids[replica_index] < current.ids[current_index]) {
                scratch.selected_delete_ids.push_back(replica.ids[replica_index++]);
            } else {
                ++current_index;
                ++replica_index;
            }
        }
        while (replica_index < replica.size()) {
            scratch.selected_delete_ids.push_back(replica.ids[replica_index++]);
        }
    }
}
