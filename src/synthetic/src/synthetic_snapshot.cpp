module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

module simnet.synthetic;

import :snapshot;
import :types;
import simnet.core;
import simnet.snapshot;

namespace
{
    struct SplitMix64
    {
        std::uint64_t state {};

        [[nodiscard]] std::uint64_t next() noexcept
        {
            auto value = (state += 0x9E3779B97F4A7C15ULL);
            value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
            value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
            return value ^ (value >> 31U);
        }

        [[nodiscard]] float unit_float() noexcept
        {
            auto constexpr scale = 1.0 / static_cast<double>(std::uint64_t { 1 } << 53U);
            auto const value = next() >> 11U;
            return static_cast<float>(static_cast<double>(value) * scale);
        }
    };

    [[nodiscard]] bool valid_bounds(simnet::Aabb3f bounds) noexcept
    {
        return simnet::is_finite(bounds.min)
            && simnet::is_finite(bounds.max)
            && bounds.max.x > bounds.min.x
            && bounds.max.y > bounds.min.y
            && bounds.max.z > bounds.min.z;
    }

    void validate_settings(simnet::SyntheticSnapshotSettings const& settings)
    {
        if (!valid_bounds(settings.bounds)) {
            throw std::runtime_error("invalid synthetic snapshot bounds");
        }
        if (settings.entity_count > std::numeric_limits<simnet::EntityNetId>::max()) {
            throw std::runtime_error("synthetic snapshot entity count exceeds EntityNetId range");
        }
    }

    [[nodiscard]] simnet::Vec3f lerp(simnet::Vec3f min, simnet::Vec3f max, simnet::Vec3f t) noexcept
    {
        return {
            .x = min.x + (max.x - min.x) * t.x,
            .y = min.y + (max.y - min.y) * t.y,
            .z = min.z + (max.z - min.z) * t.z,
        };
    }

    [[nodiscard]] simnet::Vec3f deterministic_heading(std::uint32_t index, simnet::Tick tick) noexcept
    {
        auto const angle = static_cast<float>((index * 37U + tick * 17U) % 360U) * 0.017453292519943295F;
        auto const z_wave = static_cast<float>((index * 13U + tick * 7U) % 200U) / 100.0F - 1.0F;
        return simnet::normalize_or(
            { std::cos(angle), std::sin(angle), z_wave * 0.25F },
            { 1.0F, 0.0F, 0.0F }
        );
    }

    [[nodiscard]] std::uint8_t deterministic_hue(std::uint32_t index, simnet::Tick tick) noexcept
    {
        return static_cast<std::uint8_t>((index * 29U + tick * 11U) & 0xFFU);
    }

    [[nodiscard]] std::uint32_t grid_axis_count(std::uint32_t entity_count) noexcept
    {
        auto axis = std::uint32_t { 1 };
        while (static_cast<std::uint64_t>(axis) * axis * axis < entity_count) {
            ++axis;
        }
        return axis;
    }

    [[nodiscard]] simnet::Vec3f grid_position(
        simnet::Aabb3f bounds,
        std::uint32_t index,
        std::uint32_t axis_count
    ) noexcept
    {
        auto const x_index = index % axis_count;
        auto const y_index = (index / axis_count) % axis_count;
        auto const z_index = index / static_cast<std::uint32_t>(static_cast<std::uint64_t>(axis_count) * axis_count);
        auto const divisor = static_cast<float>(std::max(axis_count, 1U));

        return lerp(bounds.min, bounds.max, {
            .x = (static_cast<float>(x_index) + 0.5F) / divisor,
            .y = (static_cast<float>(y_index) + 0.5F) / divisor,
            .z = (static_cast<float>(z_index) + 0.5F) / divisor,
        });
    }

    [[nodiscard]] simnet::Vec3f random_position(simnet::Aabb3f bounds, SplitMix64& rng) noexcept
    {
        return lerp(bounds.min, bounds.max, {
            .x = rng.unit_float(),
            .y = rng.unit_float(),
            .z = rng.unit_float(),
        });
    }

    void append_common_fields(simnet::WorldSnapshot& snapshot, std::uint32_t index, simnet::Tick tick)
    {
        snapshot.ids.push_back(index);
        snapshot.headings.push_back(deterministic_heading(index, tick));
        snapshot.hues.push_back(deterministic_hue(index, tick));
    }
}

namespace simnet
{
    WorldSnapshot make_synthetic_world_snapshot(
        SyntheticSnapshotSettings const& settings,
        Tick tick
    )
    {
        validate_settings(settings);

        auto snapshot = WorldSnapshot {};
        snapshot.tick = tick;
        snapshot.reserve(settings.entity_count);

        auto rng = SplitMix64 { .state = settings.seed ^ (tick * 0xD1B54A32D192ED03ULL) };
        auto const axis_count = grid_axis_count(settings.entity_count);

        for (std::uint32_t index = 0; index < settings.entity_count; ++index) {
            append_common_fields(snapshot, index, tick);

            switch (settings.pattern) {
            case SyntheticPattern::Grid:
                snapshot.positions.push_back(grid_position(settings.bounds, index, axis_count));
                break;
            case SyntheticPattern::RandomUniform:
                snapshot.positions.push_back(random_position(settings.bounds, rng));
                break;
            }
        }

        auto const validation = validate_world_snapshot(snapshot);
        if (!validation.valid) {
            throw std::runtime_error("generated invalid synthetic snapshot: " + validation.message);
        }

        return snapshot;
    }
}
