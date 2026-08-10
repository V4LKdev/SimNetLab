module;

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

module simnet.synthetic;

import :snapshot;
import :types;
import simnet.core;
import simnet.snapshot;

namespace
{
    constexpr simnet::EntityClassification synthetic_entity_classification{1U};

    struct SplitMix64
    {
        std::uint64_t state{};

        [[nodiscard]] std::uint64_t next() noexcept
        {
            auto value = (state += 0x9E3779B97F4A7C15ULL);
            value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
            value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
            return value ^ (value >> 31U);
        }

        [[nodiscard]] float unit_float() noexcept
        {
            auto constexpr scale = 1.0 / static_cast<double>(std::uint64_t{1} << 53U);
            auto const value = next() >> 11U;
            return static_cast<float>(static_cast<double>(value) * scale);
        }
    };

    [[nodiscard]] bool valid_bounds(simnet::Aabb3f bounds) noexcept
    {
        return simnet::is_finite(bounds.min) && simnet::is_finite(bounds.max) &&
               bounds.max.x > bounds.min.x && bounds.max.y > bounds.min.y &&
               bounds.max.z > bounds.min.z;
    }

    void validate_settings(simnet::SyntheticSnapshotSettings const& settings)
    {
        if (!valid_bounds(settings.bounds))
        {
            throw std::runtime_error("invalid synthetic snapshot bounds");
        }
        if (settings.entity_count > std::numeric_limits<simnet::EntityNetId>::max())
        {
            throw std::runtime_error("synthetic snapshot entity count exceeds EntityNetId range");
        }
    }

    void validate_change_settings(simnet::SyntheticChangeSettings const& settings)
    {
        if (!std::isfinite(settings.entity_change_fraction) ||
            settings.entity_change_fraction < 0.0 || settings.entity_change_fraction > 1.0)
        {
            throw std::runtime_error("synthetic entity change fraction must be finite [0, 1]");
        }
    }

    [[nodiscard]] bool same_binary32(float left, float right) noexcept
    {
        return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
    }

    [[nodiscard]] bool same_snapshot_settings(
        simnet::SyntheticSnapshotSettings const& left,
        simnet::SyntheticSnapshotSettings const& right
    ) noexcept
    {
        return left.seed == right.seed && left.entity_count == right.entity_count &&
               left.pattern == right.pattern &&
               same_binary32(left.bounds.min.x, right.bounds.min.x) &&
               same_binary32(left.bounds.min.y, right.bounds.min.y) &&
               same_binary32(left.bounds.min.z, right.bounds.min.z) &&
               same_binary32(left.bounds.max.x, right.bounds.max.x) &&
               same_binary32(left.bounds.max.y, right.bounds.max.y) &&
               same_binary32(left.bounds.max.z, right.bounds.max.z);
    }

    [[nodiscard]] bool same_change_settings(
        simnet::SyntheticChangeSettings const& left,
        simnet::SyntheticChangeSettings const& right
    ) noexcept
    {
        return std::bit_cast<std::uint64_t>(left.entity_change_fraction) ==
                   std::bit_cast<std::uint64_t>(right.entity_change_fraction) &&
               left.field_change_mode == right.field_change_mode;
    }

    [[nodiscard]] simnet::Vec3f lerp(simnet::Vec3f min, simnet::Vec3f max, simnet::Vec3f t) noexcept
    {
        return {
            .x = min.x + (max.x - min.x) * t.x,
            .y = min.y + (max.y - min.y) * t.y,
            .z = min.z + (max.z - min.z) * t.z,
        };
    }

    [[nodiscard]] simnet::Vec3f
    deterministic_heading(std::uint32_t index, simnet::Tick tick) noexcept
    {
        auto const wide_index = static_cast<std::uint64_t>(index);
        auto const angle_seed = (wide_index * 37ULL + tick * 17ULL) % 360ULL;
        auto const z_seed = (wide_index * 13ULL + tick * 7ULL) % 200ULL;
        auto const angle = static_cast<float>(angle_seed) * 0.017453292519943295F;
        auto const z_wave = static_cast<float>(z_seed) / 100.0F - 1.0F;
        return simnet::normalize_or(
            {std::cos(angle), std::sin(angle), z_wave * 0.25F},
            {1.0F, 0.0F, 0.0F}
        );
    }

    [[nodiscard]] std::uint8_t deterministic_hue(std::uint32_t index, simnet::Tick tick) noexcept
    {
        auto const wide_index = static_cast<std::uint64_t>(index);
        return static_cast<std::uint8_t>((wide_index * 29ULL + tick * 11ULL) & 0xFFULL);
    }

    [[nodiscard]] std::uint32_t grid_axis_count(std::uint32_t entity_count) noexcept
    {
        auto axis = std::uint32_t{1};
        while (static_cast<std::uint64_t>(axis) * axis * axis < entity_count)
        {
            ++axis;
        }
        return axis;
    }

    [[nodiscard]] simnet::Vec3f
    grid_position(simnet::Aabb3f bounds, std::uint32_t index, std::uint32_t axis_count) noexcept
    {
        auto const x_index = index % axis_count;
        auto const y_index = (index / axis_count) % axis_count;
        auto const z_index =
            index / static_cast<std::uint32_t>(static_cast<std::uint64_t>(axis_count) * axis_count);
        auto const divisor = static_cast<float>(std::max(axis_count, 1U));

        return lerp(
            bounds.min,
            bounds.max,
            {
                .x = (static_cast<float>(x_index) + 0.5F) / divisor,
                .y = (static_cast<float>(y_index) + 0.5F) / divisor,
                .z = (static_cast<float>(z_index) + 0.5F) / divisor,
            }
        );
    }

    [[nodiscard]] simnet::Vec3f random_position(simnet::Aabb3f bounds, SplitMix64& rng) noexcept
    {
        return lerp(
            bounds.min,
            bounds.max,
            {
                .x = rng.unit_float(),
                .y = rng.unit_float(),
                .z = rng.unit_float(),
            }
        );
    }

    void
    append_common_fields(simnet::WorldSnapshot& snapshot, std::uint32_t index, simnet::Tick tick)
    {
        snapshot.ids.push_back(static_cast<simnet::EntityNetId>(index + 1U));
        snapshot.classifications.push_back(synthetic_entity_classification);
        snapshot.headings.push_back(deterministic_heading(index, tick));
        snapshot.hues.push_back(deterministic_hue(index, tick));
    }

    void fill_synthetic_world_snapshot(
        simnet::SyntheticSnapshotSettings const& settings,
        simnet::Tick tick,
        simnet::WorldSnapshot& snapshot
    )
    {
        validate_settings(settings);

        snapshot.clear();
        snapshot.tick = tick;
        snapshot.reserve(settings.entity_count);

        auto rng = SplitMix64{.state = settings.seed ^ (tick * 0xD1B54A32D192ED03ULL)};
        auto const axis_count = grid_axis_count(settings.entity_count);

        for (std::uint32_t index = 0; index < settings.entity_count; ++index)
        {
            append_common_fields(snapshot, index, tick);

            switch (settings.pattern)
            {
                case simnet::SyntheticPattern::Grid:
                    snapshot.positions.push_back(grid_position(settings.bounds, index, axis_count));
                    break;
                case simnet::SyntheticPattern::RandomUniform:
                    snapshot.positions.push_back(random_position(settings.bounds, rng));
                    break;
            }
        }

        auto const validation = simnet::validate_world_snapshot(snapshot);
        if (!validation.valid)
        {
            throw std::runtime_error("generated invalid synthetic snapshot: " + validation.message);
        }
    }

    [[nodiscard]] std::uint32_t
    cohort_size(std::uint32_t entity_count, double entity_change_fraction) noexcept
    {
        if (entity_count == 0U || entity_change_fraction == 0.0)
        {
            return 0U;
        }
        auto const scaled = std::floor(entity_change_fraction * static_cast<double>(entity_count));
        return std::clamp(static_cast<std::uint32_t>(scaled), 1U, entity_count);
    }

    void copy_selected_fields(
        simnet::WorldSnapshot const& candidate,
        std::size_t index,
        simnet::SyntheticFieldChangeMode mode,
        simnet::WorldSnapshot& current
    ) noexcept
    {
        switch (mode)
        {
            case simnet::SyntheticFieldChangeMode::All:
                current.positions[index] = candidate.positions[index];
                current.headings[index] = candidate.headings[index];
                current.hues[index] = candidate.hues[index];
                break;
            case simnet::SyntheticFieldChangeMode::Transform:
                current.positions[index] = candidate.positions[index];
                current.headings[index] = candidate.headings[index];
                break;
            case simnet::SyntheticFieldChangeMode::PositionOnly:
                current.positions[index] = candidate.positions[index];
                break;
            case simnet::SyntheticFieldChangeMode::HeadingOnly:
                current.headings[index] = candidate.headings[index];
                break;
        }
    }
}

namespace simnet
{
    WorldSnapshot
    make_synthetic_world_snapshot(SyntheticSnapshotSettings const& settings, Tick tick)
    {
        auto snapshot = WorldSnapshot{};
        fill_synthetic_world_snapshot(settings, tick, snapshot);
        return snapshot;
    }

    WorldSnapshot const& update_synthetic_world_snapshot(
        SyntheticSnapshotSettings const& snapshot_settings,
        SyntheticChangeSettings const& change_settings,
        Tick tick,
        SyntheticSnapshotState& state
    )
    {
        validate_settings(snapshot_settings);
        validate_change_settings(change_settings);

        if (!state.initialized)
        {
            auto initial = make_synthetic_world_snapshot(snapshot_settings, tick);
            state.current = std::move(initial);
            state.accepted_snapshot_settings = snapshot_settings;
            state.accepted_change_settings = change_settings;
            state.next_entity_index = 0U;
            state.last_accepted_tick = tick;
            state.initialized = true;
            return state.current;
        }

        if (!same_snapshot_settings(snapshot_settings, state.accepted_snapshot_settings) ||
            !same_change_settings(change_settings, state.accepted_change_settings))
        {
            throw std::runtime_error("synthetic snapshot state cannot be reused with new settings");
        }
        if (state.last_accepted_tick == std::numeric_limits<Tick>::max() ||
            tick != state.last_accepted_tick + 1U)
        {
            throw std::runtime_error("synthetic snapshot ticks must advance by exactly one");
        }

        fill_synthetic_world_snapshot(snapshot_settings, tick, state.candidate);
        auto const count =
            cohort_size(snapshot_settings.entity_count, change_settings.entity_change_fraction);
        auto const population = snapshot_settings.entity_count;
        for (std::uint32_t offset = 0U; offset < count; ++offset)
        {
            auto const index = static_cast<std::size_t>(
                (static_cast<std::uint64_t>(state.next_entity_index) + offset) % population
            );
            copy_selected_fields(
                state.candidate,
                index,
                change_settings.field_change_mode,
                state.current
            );
        }
        state.current.tick = tick;
        if (population != 0U)
        {
            state.next_entity_index = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(state.next_entity_index) + count) % population
            );
        }
        state.last_accepted_tick = tick;
        return state.current;
    }
}
