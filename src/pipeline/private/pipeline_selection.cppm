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
    [[nodiscard]] bool should_emit_for_send_interval(PipelineDefinition const& pipeline, Tick tick) noexcept
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

        // Snapshot-order cursoring is sorted afterward to satisfy patch validation.
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
        return same_vec3(current.positions[current_index], baseline.positions[baseline_index])
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
}
