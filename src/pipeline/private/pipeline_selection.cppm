module;

#include <algorithm>
#include <cstddef>
#include <cstdint>

/// @brief Private entity selection helpers.
module simnet.pipeline:selection;

import :types;
import :messages;
import simnet.core;
import simnet.snapshot;

namespace simnet::pipeline_selection
{
    /// Returns true when send interval policy allows an emit on this tick.
    [[nodiscard]] bool
    should_emit_for_send_interval(PipelineDefinition const& pipeline, Tick tick) noexcept
    {
        if (!has_all_flags(pipeline.techniques, PipelineTechniqueFlags::SendInterval)) {
            return true;
        }

        auto const interval = static_cast<Tick>(pipeline.send_interval.interval_ticks);
        auto const phase = static_cast<Tick>(pipeline.send_interval.phase_offset);
        return ((tick + phase) % interval) == 0U;
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

    [[nodiscard]] bool same_vec3(Vec3f left, Vec3f right) noexcept
    {
        return left.x == right.x && left.y == right.y && left.z == right.z;
    }

    [[nodiscard]] bool same_entity_state(
        WorldSnapshot const& current,
        std::size_t current_index,
        WorldSnapshot const& baseline,
        std::size_t baseline_index
    ) noexcept
    {
        return current.classifications[current_index] == baseline.classifications[baseline_index]
            && same_vec3(current.positions[current_index], baseline.positions[baseline_index])
            && same_vec3(current.headings[current_index], baseline.headings[baseline_index])
            && current.hues[current_index] == baseline.hues[baseline_index];
    }

    /// Selects changed/new upserts and baseline-only deletes from two sorted snapshots.
    void select_delta_records(
        PipelineScratch& scratch,
        WorldSnapshot const& current,
        WorldSnapshot const& baseline
    )
    {
        scratch.selected_indices.clear();
        scratch.selected_delete_ids.clear();
        scratch.selected_indices.reserve(current.size());
        scratch.selected_delete_ids.reserve(baseline.size());

        auto current_index = std::size_t{};
        auto baseline_index = std::size_t{};

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

    /**
     * Filters scheduled upserts against a baseline and selects every baseline-only delete.
     *
     * Deleted entities have no current snapshot index and therefore cannot participate in the
     * round-robin schedule. Including every truthful delete prevents a partial update from
     * retaining entities that no longer exist.
     */
    void filter_scheduled_delta_records(
        PipelineScratch& scratch,
        WorldSnapshot const& current,
        WorldSnapshot const& baseline
    )
    {
        auto baseline_index = std::size_t{};
        auto retained_count = std::size_t{};
        for (std::uint32_t const current_index : scratch.selected_indices) {
            auto const current_id = current.ids[current_index];
            while (baseline_index < baseline.size() && baseline.ids[baseline_index] < current_id) {
                ++baseline_index;
            }

            bool const unchanged = baseline_index < baseline.size()
                && baseline.ids[baseline_index] == current_id
                && same_entity_state(current, current_index, baseline, baseline_index);
            if (!unchanged) {
                scratch.selected_indices[retained_count++] = current_index;
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
    }
}
