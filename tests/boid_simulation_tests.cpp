#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <flecs.h>
#include <vector>

import simnet.core;
import simnet.game_server;
import simnet.game_shared;
import simnet.snapshot;

namespace
{
    [[nodiscard]] simnet::BoidSimulationSettings test_settings()
    {
        auto settings = simnet::BoidSimulationSettings{};
        settings.world_half = 30.0F;
        settings.cell_size = 6.0F;
        settings.min_speed = 0.0F;
        settings.cruise_speed = 1.0F;
        settings.max_speed = 10.0F;
        settings.max_acceleration = 20.0F;
        settings.separation_radius = 2.5F;
        settings.alignment_radius = 6.0F;
        settings.cohesion_radius = 6.0F;
        settings.field_of_view_degrees = 240.0F;
        settings.containment_prediction_seconds = 0.75F;
        settings.containment_margin = 6.0F;
        settings.separation_acceleration = 10.0F;
        settings.containment_acceleration = 9.0F;
        settings.alignment_acceleration = 3.0F;
        settings.cohesion_acceleration = 2.0F;
        settings.enable_wander = true;
        settings.enable_hue_assimilation = true;
        settings.enable_hue_drift = true;
        return settings;
    }

    [[nodiscard]] simnet::EntityState
    boid(simnet::EntityNetId id, simnet::Vec3f position, simnet::Vec3f heading)
    {
        return {
            .id = id,
            .classification = simnet::boid_entity_classification,
            .position = position,
            .heading = heading,
            .hue = static_cast<std::uint8_t>(id),
        };
    }

    void hash_byte(std::uint64_t& hash, std::uint8_t value)
    {
        hash ^= value;
        hash *= 1099511628211ULL;
    }

    void hash_u32(std::uint64_t& hash, std::uint32_t value)
    {
        for (auto shift = 0U; shift < 32U; shift += 8U)
        {
            hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
        }
    }

    [[nodiscard]] std::uint64_t canonical_hash(simnet::WorldSnapshot const& snapshot)
    {
        auto hash = std::uint64_t{14695981039346656037ULL};
        for (auto index = std::size_t{}; index < snapshot.size(); ++index)
        {
            hash_u32(hash, snapshot.ids[index]);
            hash_byte(hash, snapshot.classifications[index].value());
            hash_u32(hash, std::bit_cast<std::uint32_t>(snapshot.positions[index].x));
            hash_u32(hash, std::bit_cast<std::uint32_t>(snapshot.positions[index].y));
            hash_u32(hash, std::bit_cast<std::uint32_t>(snapshot.positions[index].z));
            hash_u32(hash, std::bit_cast<std::uint32_t>(snapshot.headings[index].x));
            hash_u32(hash, std::bit_cast<std::uint32_t>(snapshot.headings[index].y));
            hash_u32(hash, std::bit_cast<std::uint32_t>(snapshot.headings[index].z));
            hash_byte(hash, snapshot.hues[index]);
        }
        return hash;
    }

    [[nodiscard]] std::uint64_t run_workload(std::uint32_t worker_count)
    {
        auto runtime = simnet::ServerGameRuntime{test_settings()};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);

        auto boids = std::vector<simnet::EntityState>{};
        boids.reserve(256U);
        auto constexpr headings = std::array{
            simnet::Vec3f{1.0F, 0.0F, 0.0F},
            simnet::Vec3f{0.0F, 1.0F, 0.0F},
            simnet::Vec3f{0.0F, 0.0F, 1.0F},
            simnet::Vec3f{-1.0F, 0.0F, 0.0F},
        };

        for (auto index = std::uint32_t{}; index < 256U; ++index)
        {
            auto const x = static_cast<float>(index % 8U) * 3.0F - 10.5F;
            auto const y = static_cast<float>((index / 8U) % 8U) * 3.0F - 10.5F;
            auto const z = static_cast<float>(index / 64U) * 3.0F - 4.5F;
            boids.push_back(boid(index + 1U, {x, y, z}, headings[index % headings.size()]));
        }
        REQUIRE(simnet::append_authoritative_boids(world, boids).success());

        if (worker_count > 1U)
        {
            world.set_threads(static_cast<std::int32_t>(worker_count));
        }

        for (auto tick = 0U; tick < 120U; ++tick)
        {
            REQUIRE(simnet::prepare_server_game_runtime(world, runtime));
            REQUIRE(world.progress(1.0F / 60.0F));
            REQUIRE(runtime.last_step_report().valid);
        }

        auto snapshot = simnet::WorldSnapshot{};
        REQUIRE(simnet::extract_world_snapshot(world, 120U, snapshot).valid);
        REQUIRE(snapshot.size() == 256U);
        return canonical_hash(snapshot);
    }
}

TEST_CASE("authoritative boid workload is deterministic across worker counts", "[boids][determinism]")
{
    auto const serial = run_workload(1U);
    CHECK(run_workload(1U) == serial);
    CHECK(run_workload(4U) == serial);
    CHECK(run_workload(8U) == serial);
}