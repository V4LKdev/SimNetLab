#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <flecs.h>
#include <optional>
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

    [[nodiscard]] simnet::BoidSimulationSettings semantic_settings()
    {
        auto settings = test_settings();
        settings.world_half = 100.0F;
        settings.cell_size = 10.0F;
        settings.max_neighbors = 16U;
        settings.enable_separation = false;
        settings.enable_alignment = false;
        settings.enable_cohesion = false;
        settings.enable_containment = false;
        settings.enable_wander = false;
        settings.enable_hue_assimilation = false;
        settings.enable_hue_drift = false;
        settings.min_speed = 0.0F;
        settings.cruise_speed = 1.0F;
        settings.max_speed = 20.0F;
        settings.max_acceleration = 20.0F;
        settings.separation_radius = 4.0F;
        settings.alignment_radius = 50.0F;
        settings.cohesion_radius = 50.0F;
        settings.field_of_view_degrees = 360.0F;
        settings.containment_margin = 10.0F;
        settings.separation_acceleration = 12.0F;
        settings.containment_acceleration = 12.0F;
        settings.alignment_acceleration = 6.0F;
        settings.cohesion_acceleration = 6.0F;
        settings.wander_acceleration = 2.0F;
        settings.hue_assimilation_rate = 1.0F;
        settings.hue_drift_rate = 0.2F;
        return settings;
    }

    struct SemanticRun
    {
        simnet::WorldSnapshot snapshot{};
        std::optional<simnet::SelectedBoidDebug> selected{};
    };

    [[nodiscard]] SemanticRun run_semantic_boids(
        simnet::BoidSimulationSettings settings,
        std::vector<simnet::EntityState> const& boids,
        std::uint32_t ticks,
        bool spawn_player = false
    )
    {
        auto player_settings = simnet::PlayerMovementSettings{};
        player_settings.world_half = settings.world_half;
        if (spawn_player)
        {
            player_settings.slow_speed = 0.0F;
            player_settings.cruise_speed = 0.0F;
            player_settings.boost_speed = 0.0F;
        }
        auto runtime = simnet::ServerGameRuntime{settings, player_settings};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(simnet::append_authoritative_boids(world, boids).success());
        if (spawn_player)
        {
            REQUIRE(simnet::spawn_authoritative_player(world) != 0U);
        }
        for (auto tick = 0U; tick < ticks; ++tick)
        {
            REQUIRE(simnet::prepare_server_game_runtime(world, runtime));
            REQUIRE(world.progress(1.0F / 60.0F));
            REQUIRE(runtime.last_step_report().valid);
        }
        if (!boids.empty())
        {
            runtime.select_boid(boids.front().id);
        }
        auto result = SemanticRun{.selected = runtime.selected_boid_debug()};
        REQUIRE(simnet::extract_world_snapshot(world, ticks, result.snapshot).valid);
        return result;
    }

    [[nodiscard]] simnet::EntityState const
    state_at(simnet::WorldSnapshot const& snapshot, simnet::EntityNetId id)
    {
        for (auto index = std::size_t{}; index < snapshot.size(); ++index)
        {
            if (snapshot.ids[index] == id)
            {
                return {
                    .id = id,
                    .classification = snapshot.classifications[index],
                    .position = snapshot.positions[index],
                    .heading = snapshot.headings[index],
                    .hue = snapshot.hues[index],
                };
            }
        }
        FAIL("snapshot does not contain requested entity");
        return {};
    }

    [[nodiscard]] float length(simnet::Vec3f value)
    {
        return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    }

    [[nodiscard]] float distance(simnet::Vec3f left, simnet::Vec3f right)
    {
        return length({left.x - right.x, left.y - right.y, left.z - right.z});
    }

    [[nodiscard]] float yaw(simnet::Vec3f heading)
    {
        return std::atan2(heading.x, heading.z);
    }

    struct PlayerRun
    {
        simnet::EntityState state{};
        bool input_accepted{};
    };

    [[nodiscard]] PlayerRun run_player(
        simnet::PlayerControlState input,
        std::uint32_t ticks,
        simnet::PlayerMovementSettings player_settings = {}
    )
    {
        auto settings = semantic_settings();
        player_settings.world_half = settings.world_half;
        auto runtime = simnet::ServerGameRuntime{settings, player_settings};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        auto const player_id = simnet::spawn_authoritative_player(world);
        REQUIRE(player_id != 0U);
        auto const accepted = simnet::set_authoritative_player_input(world, player_id, input);
        for (auto tick = 0U; tick < ticks; ++tick)
        {
            REQUIRE(simnet::prepare_server_game_runtime(world, runtime));
            REQUIRE(world.progress(1.0F / 60.0F));
            REQUIRE(runtime.last_step_report().valid);
        }
        auto snapshot = simnet::WorldSnapshot{};
        REQUIRE(simnet::extract_world_snapshot(world, ticks, snapshot).valid);
        return {.state = state_at(snapshot, player_id), .input_accepted = accepted};
    }
}

TEST_CASE("authoritative boid workload is deterministic across worker counts", "[boids][determinism]")
{
    auto const serial = run_workload(1U);
    CHECK(run_workload(1U) == serial);
    CHECK(run_workload(4U) == serial);
    CHECK(run_workload(8U) == serial);
}

TEST_CASE("individual boid rules have their intended directional semantics", "[boids][semantics]")
{
    auto const parallel_pair = std::vector{
        boid(1U, {-0.5F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}),
        boid(2U, {0.5F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}),
    };
    auto control_settings = semantic_settings();
    auto separation_settings = control_settings;
    separation_settings.enable_separation = true;
    auto const separation_control = run_semantic_boids(control_settings, parallel_pair, 30U);
    auto const separation = run_semantic_boids(separation_settings, parallel_pair, 30U);
    auto const separation_control_distance = distance(
        state_at(separation_control.snapshot, 1U).position,
        state_at(separation_control.snapshot, 2U).position
    );
    auto const separation_distance = distance(
        state_at(separation.snapshot, 1U).position,
        state_at(separation.snapshot, 2U).position
    );
    CHECK(separation_distance > separation_control_distance + 0.25F);
    REQUIRE(separation.selected.has_value());
    CHECK(separation.selected->separation.x < 0.0F);

    auto const opposed_headings = std::vector{
        boid(1U, {-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
        boid(2U, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}),
    };
    auto alignment_settings = control_settings;
    alignment_settings.enable_alignment = true;
    auto const alignment_control = run_semantic_boids(control_settings, opposed_headings, 30U);
    auto const alignment = run_semantic_boids(alignment_settings, opposed_headings, 30U);
    auto const control_agreement = simnet::dot(
        state_at(alignment_control.snapshot, 1U).heading,
        state_at(alignment_control.snapshot, 2U).heading
    );
    auto const aligned_agreement = simnet::dot(
        state_at(alignment.snapshot, 1U).heading,
        state_at(alignment.snapshot, 2U).heading
    );
    CHECK(aligned_agreement > control_agreement + 0.25F);

    auto const separated_pair = std::vector{
        boid(1U, {-8.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}),
        boid(2U, {8.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}),
    };
    auto cohesion_settings = control_settings;
    cohesion_settings.enable_cohesion = true;
    auto const cohesion_control = run_semantic_boids(control_settings, separated_pair, 60U);
    auto const cohesion = run_semantic_boids(cohesion_settings, separated_pair, 60U);
    auto const cohesion_control_distance = distance(
        state_at(cohesion_control.snapshot, 1U).position,
        state_at(cohesion_control.snapshot, 2U).position
    );
    auto const cohesion_distance = distance(
        state_at(cohesion.snapshot, 1U).position,
        state_at(cohesion.snapshot, 2U).position
    );
    CHECK(cohesion_distance < cohesion_control_distance - 0.5F);

    auto containment_settings = control_settings;
    containment_settings.enable_containment = true;
    auto const boundary_boid = std::vector{
        boid(1U, {95.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
    };
    auto const containment_control = run_semantic_boids(control_settings, boundary_boid, 60U);
    auto const containment = run_semantic_boids(containment_settings, boundary_boid, 60U);
    CHECK(
        state_at(containment.snapshot, 1U).position.x <
        state_at(containment_control.snapshot, 1U).position.x - 0.25F
    );
    CHECK(std::abs(state_at(containment.snapshot, 1U).position.x) <= 100.0F);

    auto wander_settings = control_settings;
    wander_settings.enable_wander = true;
    auto const solitary = std::vector{
        boid(1U, {}, {0.0F, 0.0F, 1.0F}),
    };
    auto const wander_control = run_semantic_boids(control_settings, solitary, 120U);
    auto const wander = run_semantic_boids(wander_settings, solitary, 120U);
    CHECK(
        simnet::dot(
            state_at(wander.snapshot, 1U).heading,
            state_at(wander_control.snapshot, 1U).heading
        ) < 0.999F
    );
}

TEST_CASE("boid hue rules have assimilation and drift semantics", "[boids][semantics][hue]")
{
    auto control_settings = semantic_settings();
    auto assimilation_settings = control_settings;
    assimilation_settings.enable_hue_assimilation = true;
    auto pair = std::vector{
        boid(1U, {-1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}),
        boid(2U, {1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}),
    };
    pair[0].hue = 16U;
    pair[1].hue = 96U;
    auto const control = run_semantic_boids(control_settings, pair, 120U);
    auto const assimilated = run_semantic_boids(assimilation_settings, pair, 120U);
    auto const control_difference = std::abs(
        static_cast<int>(state_at(control.snapshot, 1U).hue) -
        static_cast<int>(state_at(control.snapshot, 2U).hue)
    );
    auto const assimilated_difference = std::abs(
        static_cast<int>(state_at(assimilated.snapshot, 1U).hue) -
        static_cast<int>(state_at(assimilated.snapshot, 2U).hue)
    );
    CHECK(assimilated_difference < control_difference);

    auto drift_settings = control_settings;
    drift_settings.enable_hue_drift = true;
    auto solitary = std::vector{boid(1U, {}, {0.0F, 0.0F, 1.0F})};
    solitary[0].hue = 32U;
    auto const drift_control = run_semantic_boids(control_settings, solitary, 120U);
    auto const drifted = run_semantic_boids(drift_settings, solitary, 120U);
    CHECK(state_at(drift_control.snapshot, 1U).hue == 32U);
    CHECK(state_at(drifted.snapshot, 1U).hue != 32U);
}

TEST_CASE("Player lure attracts and predator repels a matched boid", "[boids][player][semantics]")
{
    auto control_settings = semantic_settings();
    control_settings.cruise_speed = 0.0F;
    auto lure_settings = control_settings;
    lure_settings.player_lure = {
        .enabled = true,
        .radius = 30.0F,
        .max_acceleration = 8.0F,
    };
    auto predator_settings = control_settings;
    predator_settings.player_predator = {
        .enabled = true,
        .radius = 30.0F,
        .max_acceleration = 8.0F,
    };
    auto const initial = std::vector{
        boid(1U, {10.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}),
    };
    auto const control = run_semantic_boids(control_settings, initial, 60U, true);
    auto const lured = run_semantic_boids(lure_settings, initial, 60U, true);
    auto const repelled = run_semantic_boids(predator_settings, initial, 60U, true);
    auto const control_x = state_at(control.snapshot, 1U).position.x;
    auto const lure_x = state_at(lured.snapshot, 1U).position.x;
    auto const predator_x = state_at(repelled.snapshot, 1U).position.x;
    CHECK(lure_x < control_x - 0.5F);
    CHECK(predator_x > control_x + 0.5F);
    REQUIRE(lured.selected.has_value());
    REQUIRE(repelled.selected.has_value());
    CHECK(lured.selected->lure_source_count == 1U);
    CHECK(lured.selected->lure.x < 0.0F);
    CHECK(repelled.selected->predator_source_count == 1U);
    CHECK(repelled.selected->predator.x > 0.0F);
}

TEST_CASE("Player controls move and rotate in the documented directions", "[player][controls][semantics]")
{
    auto const neutral = run_player({}, 60U);
    auto const accelerated = run_player({.accelerate = true}, 60U);
    auto const decelerated = run_player({.decelerate = true}, 60U);
    auto const yaw_left = run_player({.yaw_left = true}, 30U);
    auto const yaw_right = run_player({.yaw_right = true}, 30U);
    auto const pitch_up = run_player({.pitch_up = true}, 30U);
    auto const pitch_down = run_player({.pitch_down = true}, 30U);
    auto const opposed = run_player(
        {.pitch_up = true,
         .yaw_left = true,
         .pitch_down = true,
         .yaw_right = true,
         .accelerate = true,
         .decelerate = true,
         .left_mouse = true,
         .right_mouse = true},
        60U
    );

    CHECK(neutral.input_accepted);
    CHECK(neutral.state.position.z > 7.9F);
    CHECK(accelerated.state.position.z > neutral.state.position.z + 2.0F);
    CHECK(decelerated.state.position.z < neutral.state.position.z - 2.0F);
    CHECK(yaw_left.state.position.x > 0.25F);
    CHECK(yaw_left.state.heading.x > 0.0F);
    CHECK(yaw_right.state.position.x < -0.25F);
    CHECK(yaw_right.state.heading.x < 0.0F);
    CHECK(pitch_up.state.position.y > 0.25F);
    CHECK(pitch_up.state.heading.y > 0.0F);
    CHECK(pitch_down.state.position.y < -0.25F);
    CHECK(pitch_down.state.heading.y < 0.0F);
    CHECK(distance(opposed.state.position, neutral.state.position) < 1e-4F);
    CHECK(simnet::dot(opposed.state.heading, neutral.state.heading) > 0.9999F);
}

TEST_CASE("Player angular velocity damps after input release", "[player][controls][damping]")
{
    auto settings = semantic_settings();
    auto player_settings = simnet::PlayerMovementSettings{};
    player_settings.world_half = settings.world_half;
    auto runtime = simnet::ServerGameRuntime{settings, player_settings};
    auto world = flecs::world{};
    simnet::register_server_game(world, runtime);
    auto const player_id = simnet::spawn_authoritative_player(world);
    REQUIRE(player_id != 0U);
    REQUIRE(simnet::set_authoritative_player_input(world, player_id, {.yaw_left = true}));
    for (auto tick = 0U; tick < 20U; ++tick)
    {
        REQUIRE(simnet::prepare_server_game_runtime(world, runtime));
        REQUIRE(world.progress(1.0F / 60.0F));
    }
    auto snapshot = simnet::WorldSnapshot{};
    REQUIRE(simnet::extract_world_snapshot(world, 20U, snapshot).valid);
    auto const released_yaw = yaw(state_at(snapshot, player_id).heading);
    REQUIRE(simnet::set_authoritative_player_input(world, player_id, {}));
    for (auto tick = 0U; tick < 10U; ++tick)
    {
        REQUIRE(simnet::prepare_server_game_runtime(world, runtime));
        REQUIRE(world.progress(1.0F / 60.0F));
    }
    REQUIRE(simnet::extract_world_snapshot(world, 30U, snapshot).valid);
    auto const first_yaw = yaw(state_at(snapshot, player_id).heading);
    for (auto tick = 0U; tick < 10U; ++tick)
    {
        REQUIRE(simnet::prepare_server_game_runtime(world, runtime));
        REQUIRE(world.progress(1.0F / 60.0F));
    }
    REQUIRE(simnet::extract_world_snapshot(world, 40U, snapshot).valid);
    auto const second_yaw = yaw(state_at(snapshot, player_id).heading);
    CHECK(first_yaw - released_yaw > second_yaw - first_yaw);
    CHECK(second_yaw > first_yaw);
}
