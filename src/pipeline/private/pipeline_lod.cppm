module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

/// @brief Private deterministic temporal distance-LOD scheduling helpers.
module simnet.pipeline:lod;

import :types;
import simnet.core;
import simnet.snapshot;

namespace simnet::pipeline_lod
{
    void increment(LevelOfDetailBandCounts& counts, LevelOfDetailBand band) noexcept
    {
        switch (band) {
            case LevelOfDetailBand::Near:
                ++counts.near;
                return;
            case LevelOfDetailBand::Medium:
                ++counts.medium;
                return;
            case LevelOfDetailBand::Far:
                ++counts.far;
                return;
        }
    }

    [[nodiscard]] std::uint32_t total(LevelOfDetailBandCounts const& counts) noexcept
    {
        return counts.near + counts.medium + counts.far;
    }

    [[nodiscard]] std::uint32_t mixed_entity_id(EntityNetId id) noexcept
    {
        auto value = static_cast<std::uint32_t>(id);
        value ^= value >> 16U;
        value *= 0x7FEB352DU;
        value ^= value >> 15U;
        value *= 0x846CA68BU;
        value ^= value >> 16U;
        return value;
    }

    [[nodiscard]] std::uint32_t
    interval_ticks(LevelOfDetailSettings const& settings, LevelOfDetailBand band) noexcept
    {
        switch (band) {
            case LevelOfDetailBand::Near:
                return 1U;
            case LevelOfDetailBand::Medium:
                return settings.medium_interval_ticks;
            case LevelOfDetailBand::Far:
                return settings.far_interval_ticks;
        }
        return 1U;
    }

    [[nodiscard]] Tick next_due_tick(Tick serviced_tick, EntityNetId id, std::uint32_t interval)
    {
        auto const phase = static_cast<Tick>(mixed_entity_id(id) % interval);
        auto const remainder = serviced_tick % interval;
        auto delta = (phase + interval - remainder) % interval;
        if (delta == 0U) {
            delta = interval;
        }
        if (serviced_tick > std::numeric_limits<Tick>::max() - delta) {
            throw std::runtime_error("level-of-detail due tick would overflow");
        }
        return serviced_tick + delta;
    }

    [[nodiscard]] LevelOfDetailBand classify(
        LevelOfDetailSettings const& settings,
        InterestSource const& source,
        EntityNetId id,
        Vec3f position
    ) noexcept
    {
        if (source.source_entity_id != 0U && id == source.source_entity_id) {
            return LevelOfDetailBand::Near;
        }
        auto const offset = position - source.position;
        auto const distance_squared = length_squared(offset);
        auto const near_squared = settings.near_distance * settings.near_distance;
        if (distance_squared <= near_squared) {
            return LevelOfDetailBand::Near;
        }
        auto const medium_squared = settings.medium_distance * settings.medium_distance;
        return distance_squared <= medium_squared ? LevelOfDetailBand::Medium
                                                  : LevelOfDetailBand::Far;
    }

    LevelOfDetailReport reconcile_schedule(
        ClientReplicationState const& state,
        PipelineScratch& scratch,
        WorldSnapshot const& current,
        LevelOfDetailSettings const& settings,
        InterestSource const& source
    )
    {
        auto report = LevelOfDetailReport{.mode = settings.mode};
        scratch.level_of_detail_schedule.clear();
        scratch.level_of_detail_schedule.reserve(current.size());

        auto old_index = std::size_t{};
        for (auto current_index = std::size_t{}; current_index < current.size(); ++current_index) {
            auto const id = current.ids[current_index];
            while (old_index < state.level_of_detail_schedule.size()
                   && state.level_of_detail_schedule[old_index].id < id) {
                ++old_index;
            }

            auto entry = LevelOfDetailScheduleEntry{.id = id};
            auto const existed = old_index < state.level_of_detail_schedule.size()
                && state.level_of_detail_schedule[old_index].id == id;
            if (existed) {
                entry = state.level_of_detail_schedule[old_index++];
            } else {
                entry.next_due_tick = current.tick;
                entry.pending_due = true;
                ++report.forced_immediate_count;
            }

            auto const band = classify(settings, source, id, current.positions[current_index]);
            if (existed && entry.band != band) {
                ++report.transition_count;
                if (band < entry.band) {
                    if (!entry.pending_due) {
                        ++report.forced_immediate_count;
                    }
                    entry.pending_due = true;
                }
            }
            entry.band = band;
            if (!entry.pending_due && current.tick >= entry.next_due_tick) {
                entry.pending_due = true;
            }
            increment(report.population, entry.band);
            if (entry.pending_due) {
                increment(report.eligible, entry.band);
            }
            scratch.level_of_detail_schedule.push_back(entry);
        }

        return report;
    }

    [[nodiscard]] std::uint32_t select_pending_indices(
        std::span<LevelOfDetailScheduleEntry const> schedule,
        WorldSnapshot const& current,
        EntityNetId forced_self_id,
        bool incremental_enabled,
        std::uint32_t cursor,
        std::uint32_t maximum_ordinary,
        std::vector<std::uint32_t>& selected
    )
    {
        selected.clear();
        auto const count = schedule.size();
        if (count == 0U) {
            return 0U;
        }

        auto const start = static_cast<std::size_t>(cursor) % count;
        auto last_selected = start;
        auto selected_ordinary = std::uint32_t{};
        for (auto offset = std::size_t{}; offset < count; ++offset) {
            auto const index = (start + offset) % count;
            auto const& entry = schedule[index];
            if (!entry.pending_due || entry.id == forced_self_id) {
                continue;
            }
            selected.push_back(static_cast<std::uint32_t>(index));
            last_selected = index;
            ++selected_ordinary;
            if (incremental_enabled && selected_ordinary == maximum_ordinary) {
                break;
            }
        }

        if (forced_self_id != 0U) {
            auto const self = std::ranges::lower_bound(current.ids, forced_self_id);
            if (self != current.ids.end() && *self == forced_self_id) {
                selected.push_back(
                    static_cast<std::uint32_t>(std::distance(current.ids.begin(), self))
                );
            }
        }
        std::ranges::sort(selected);
        if (!incremental_enabled || selected_ordinary == 0U) {
            return cursor;
        }
        return static_cast<std::uint32_t>((last_selected + 1U) % count);
    }

    void service_selected_indices(
        std::vector<LevelOfDetailScheduleEntry>& schedule,
        WorldSnapshot const& current,
        LevelOfDetailSettings const& settings,
        std::span<std::uint32_t const> selected,
        LevelOfDetailReport& report
    )
    {
        for (auto const index : selected) {
            auto& entry = schedule[index];
            if (entry.pending_due) {
                increment(report.serviced, entry.band);
            }
            entry.pending_due = false;
            entry.next_due_tick
                = next_due_tick(current.tick, entry.id, interval_ticks(settings, entry.band));
        }

        for (auto const& entry : schedule) {
            if (entry.pending_due) {
                increment(report.deferred, entry.band);
            }
        }
        report.pending_due_count = total(report.deferred);
    }

    void service_full_replace(
        std::vector<LevelOfDetailScheduleEntry>& schedule,
        PipelineScratch& scratch,
        WorldSnapshot const& current,
        LevelOfDetailSettings const& settings,
        LevelOfDetailReport& report
    )
    {
        report.eligible = report.population;
        scratch.selected_indices.clear();
        scratch.selected_indices.reserve(current.size());
        for (auto index = std::size_t{}; index < current.size(); ++index) {
            schedule[index].pending_due = true;
            scratch.selected_indices.push_back(static_cast<std::uint32_t>(index));
        }
        service_selected_indices(schedule, current, settings, scratch.selected_indices, report);
        report.full_replace_override_count = 1U;
    }

    void count_represented(
        std::span<LevelOfDetailScheduleEntry const> schedule,
        std::span<std::uint32_t const> represented,
        LevelOfDetailReport& report
    ) noexcept
    {
        for (auto const index : represented) {
            increment(report.represented, schedule[index].band);
        }
    }
}
