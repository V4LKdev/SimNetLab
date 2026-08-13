#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <flecs.h>
#include <initializer_list>
#include <limits>
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
        settings.world_half = 100.0F;
        settings.cell_size = 10.0F;
        settings.min_speed = 0.0F;
        settings.cruise_speed = 1.0F;
        settings.max_speed = 10.0F;
        settings.max_acceleration = 20.0F;
        settings.enable_wander = false;
        settings.enable_hue_assimilation = false;
        settings.enable_hue_drift = false;
        settings.separation_radius = 3.0F;
        settings.alignment_radius = 20.0F;
        settings.cohesion_radius = 20.0F;
        settings.field_of_view_degrees = 240.0F;
        settings.containment_prediction_seconds = 0.75F;
        settings.containment_margin = 5.0F;
        settings.separation_acceleration = 10.0F;
        settings.containment_acceleration = 9.0F;
        settings.alignment_acceleration = 3.0F;
        settings.cohesion_acceleration = 2.0F;
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

    [[nodiscard]] simnet::AuthoritativeSpawnReport
    append_boids(flecs::world& world, std::initializer_list<simnet::EntityState> values)
    {
        auto storage = std::vector<simnet::EntityState>{values};
        return simnet::append_authoritative_boids(world, storage);
    }

    [[nodiscard]] simnet::EntityState hued_boid(
        simnet::EntityNetId id,
        simnet::Vec3f position,
        simnet::Vec3f heading,
        std::uint8_t hue
    )
    {
        auto result = boid(id, position, heading);
        result.hue = hue;
        return result;
    }

    void
    step(flecs::world& world, simnet::ServerGameRuntime& runtime, float delta_time = 1.0F / 60.0F)
    {
        REQUIRE(simnet::prepare_server_game_runtime(world, runtime));
        REQUIRE(world.progress(delta_time));
        REQUIRE(runtime.last_step_report().valid);
    }

    [[nodiscard]] simnet::PlayerMovementSettings stationary_player_settings()
    {
        return {
            .world_half = 100.0F,
            .cruise_speed = 0.0F,
            .boost_speed = 1.0F,
            .slow_speed = 0.0F,
            .speed_change_rate = 1.0F,
            .yaw_acceleration_degrees = 360.0F,
            .pitch_acceleration_degrees = 300.0F,
            .yaw_damping = 8.0F,
            .pitch_damping = 8.0F,
            .max_yaw_rate_degrees = 120.0F,
            .max_pitch_rate_degrees = 90.0F,
            .pitch_limit_degrees = 80.0F,
        };
    }

    [[nodiscard]] simnet::BoidSimulationSettings isolated_influence_settings()
    {
        auto settings = test_settings();
        settings.enable_separation = false;
        settings.enable_alignment = false;
        settings.enable_cohesion = false;
        settings.enable_containment = false;
        settings.enable_wander = false;
        settings.enable_hue_assimilation = false;
        settings.enable_hue_drift = false;
        settings.min_speed = 0.0F;
        settings.cruise_speed = 0.0F;
        settings.max_speed = 20.0F;
        settings.max_acceleration = 12.0F;
        return settings;
    }

    void set_player_position(flecs::world& world, simnet::EntityNetId id, simnet::Vec3f position)
    {
        auto found = false;
        auto query = world
                         .query_builder<
                             const simnet::EntityKindComponent,
                             const simnet::NetIdentity,
                             simnet::Position>()
                         .build();
        query.each(
            [&](simnet::EntityKindComponent const& kind,
                simnet::NetIdentity const& identity,
                simnet::Position& current)
            {
                if (kind.value == simnet::EntityKind::Player && identity.id == id)
                {
                    current.value = position;
                    found = true;
                }
            }
        );
        REQUIRE(found);
    }

    [[nodiscard]] simnet::WorldSnapshot snapshot(flecs::world const& world, simnet::Tick tick)
    {
        auto result = simnet::WorldSnapshot{};
        REQUIRE(simnet::extract_world_snapshot(world, tick, result).valid);
        return result;
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

    [[nodiscard]] std::uint64_t canonical_hash(simnet::WorldSnapshot const& value)
    {
        auto hash = std::uint64_t{14695981039346656037ULL};
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            hash_u32(hash, value.ids[index]);
            hash_byte(hash, value.classifications[index].value());
            hash_u32(hash, std::bit_cast<std::uint32_t>(value.positions[index].x));
            hash_u32(hash, std::bit_cast<std::uint32_t>(value.positions[index].y));
            hash_u32(hash, std::bit_cast<std::uint32_t>(value.positions[index].z));
            hash_u32(hash, std::bit_cast<std::uint32_t>(value.headings[index].x));
            hash_u32(hash, std::bit_cast<std::uint32_t>(value.headings[index].y));
            hash_u32(hash, std::bit_cast<std::uint32_t>(value.headings[index].z));
            hash_byte(hash, value.hues[index]);
        }
        return hash;
    }

    [[nodiscard]] std::uint64_t canonical_boid_hash(simnet::WorldSnapshot const& value)
    {
        auto copy = simnet::WorldSnapshot{};
        copy.tick = value.tick;
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            if (value.classifications[index] != simnet::boid_entity_classification)
            {
                continue;
            }
            copy.ids.push_back(value.ids[index]);
            copy.classifications.push_back(value.classifications[index]);
            copy.positions.push_back(value.positions[index]);
            copy.headings.push_back(value.headings[index]);
            copy.hues.push_back(value.hues[index]);
        }
        return canonical_hash(copy);
    }

    [[nodiscard]] std::uint64_t run_determinism_case(
        std::uint32_t thread_count,
        bool lure_enabled = false,
        bool predator_enabled = false
    )
    {
        auto settings = test_settings();
        settings.world_half = 30.0F;
        settings.cell_size = 6.0F;
        settings.separation_radius = 2.5F;
        settings.alignment_radius = 6.0F;
        settings.cohesion_radius = 6.0F;
        settings.containment_margin = 6.0F;
        settings.enable_wander = true;
        settings.enable_hue_assimilation = true;
        settings.enable_hue_drift = true;
        settings.player_lure = {
            .enabled = lure_enabled,
            .radius = lure_enabled ? 20.0F : 0.0F,
            .max_acceleration = lure_enabled ? 4.0F : 0.0F,
        };
        settings.player_predator = {
            .enabled = predator_enabled,
            .radius = predator_enabled ? 12.0F : 0.0F,
            .max_acceleration = predator_enabled ? 8.0F : 0.0F,
        };
        auto runtime = simnet::ServerGameRuntime{settings, stationary_player_settings()};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);

        auto boids = std::vector<simnet::EntityState>{};
        boids.reserve(256);
        auto constexpr headings = std::array{
            simnet::Vec3f{1.0F, 0.0F, 0.0F},
            simnet::Vec3f{0.0F, 1.0F, 0.0F},
            simnet::Vec3f{0.0F, 0.0F, 1.0F},
            simnet::Vec3f{-1.0F, 0.0F, 0.0F},
        };
        for (std::uint32_t index = 0; index < 256U; ++index)
        {
            auto const x = static_cast<float>(index % 8U) * 3.0F - 10.5F;
            auto const y_index = (index / 8U) % 8U;
            auto const y = static_cast<float>(y_index) * 3.0F - 10.5F;
            auto const z_index = index / 64U;
            auto const z = static_cast<float>(z_index) * 3.0F - 4.5F;
            boids.push_back(boid(index + 1U, {x, y, z}, headings[index % headings.size()]));
        }
        REQUIRE(simnet::append_authoritative_boids(world, boids).success());
        if (lure_enabled || predator_enabled)
        {
            auto const first_player = simnet::spawn_authoritative_player(world);
            auto const second_player = simnet::spawn_authoritative_player(world);
            REQUIRE(first_player != 0U);
            REQUIRE(second_player > first_player);
            set_player_position(world, first_player, {-8.0F, 3.0F, 4.0F});
            set_player_position(world, second_player, {9.0F, -2.0F, -5.0F});
        }
        if (thread_count > 1U)
        {
            world.set_threads(static_cast<std::int32_t>(thread_count));
        }
        for (auto tick = 0U; tick < 120U; ++tick)
        {
            step(world, runtime);
        }
        return canonical_hash(snapshot(world, 120U));
    }
}

TEST_CASE("boid rules produce deterministic local steering", "[boids]")
{
    SECTION("separation pushes nearby boids apart")
    {
        auto settings = test_settings();
        settings.alignment_acceleration = 0.0F;
        settings.cohesion_acceleration = 0.0F;
        auto runtime = simnet::ServerGameRuntime{settings};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(append_boids(
                    world,
                    {
                        boid(1U, {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}),
                        boid(2U, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}),
                    }
        )
                    .success());
        runtime.select_boid(1U);
        step(world, runtime);
        auto const debug = runtime.selected_boid_debug();
        REQUIRE(debug.has_value());
        CHECK(debug->separation_neighbor_count == 1U);
        CHECK(debug->separation.x < 0.0F);
    }

    SECTION("alignment follows average velocity and cohesion follows centroid")
    {
        auto settings = test_settings();
        settings.separation_radius = 0.1F;
        settings.field_of_view_degrees = 360.0F;
        auto runtime = simnet::ServerGameRuntime{settings};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(append_boids(
                    world,
                    {
                        boid(1U, {-5.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}),
                        boid(2U, {5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
                    }
        )
                    .success());
        runtime.select_boid(1U);
        step(world, runtime);
        auto const debug = runtime.selected_boid_debug();
        REQUIRE(debug.has_value());
        CHECK(debug->alignment.x > 0.0F);
        CHECK(debug->cohesion.x > 0.0F);
    }

    SECTION("FOV excludes social rules but never separation")
    {
        auto settings = test_settings();
        settings.field_of_view_degrees = 90.0F;
        auto runtime = simnet::ServerGameRuntime{settings};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(append_boids(
                    world,
                    {
                        boid(1U, {}, {1.0F, 0.0F, 0.0F}),
                        boid(2U, {-2.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
                    }
        )
                    .success());
        runtime.select_boid(1U);
        step(world, runtime);
        auto const debug = runtime.selected_boid_debug();
        REQUIRE(debug.has_value());
        CHECK(debug->separation_neighbor_count == 1U);
        CHECK(debug->alignment_neighbor_count == 0U);
        CHECK(debug->cohesion_neighbor_count == 0U);
    }

    SECTION("containment predicts an inward correction")
    {
        auto settings = test_settings();
        settings.world_half = 10.0F;
        settings.containment_margin = 3.0F;
        auto runtime = simnet::ServerGameRuntime{settings};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(append_boids(
                    world,
                    {
                        boid(1U, {9.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
                    }
        )
                    .success());
        runtime.select_boid(1U);
        step(world, runtime);
        auto const debug = runtime.selected_boid_debug();
        REQUIRE(debug.has_value());
        CHECK(debug->containment.x < 0.0F);
    }

    SECTION("alignment and cohesion use independent radii")
    {
        auto settings = test_settings();
        settings.separation_radius = 0.1F;
        settings.alignment_radius = 2.0F;
        settings.cohesion_radius = 10.0F;
        settings.field_of_view_degrees = 360.0F;
        auto runtime = simnet::ServerGameRuntime{settings};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(append_boids(
                    world,
                    {
                        boid(1U, {}, {0.0F, 1.0F, 0.0F}),
                        boid(2U, {5.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}),
                    }
        )
                    .success());
        runtime.select_boid(1U);
        step(world, runtime);
        auto const debug = runtime.selected_boid_debug();
        REQUIRE(debug.has_value());
        CHECK(debug->alignment_neighbor_count == 0U);
        CHECK(debug->cohesion_neighbor_count == 1U);
        CHECK(simnet::length_squared(debug->alignment) == 0.0F);
        CHECK(debug->cohesion.x > 0.0F);
    }
}

TEST_CASE(
    "authoritative player is replicated but excluded from flock simulation",
    "[boids][player]"
)
{
    auto settings = test_settings();
    settings.enable_separation = false;
    settings.enable_alignment = false;
    settings.enable_cohesion = false;
    settings.enable_containment = false;
    auto player_settings = simnet::PlayerMovementSettings{
        .world_half = 10.0F,
        .cruise_speed = 2.0F,
        .boost_speed = 4.0F,
        .slow_speed = 1.0F,
        .speed_change_rate = 10.0F,
        .yaw_acceleration_degrees = 360.0F,
        .pitch_acceleration_degrees = 300.0F,
        .yaw_damping = 8.0F,
        .pitch_damping = 8.0F,
        .max_yaw_rate_degrees = 120.0F,
        .max_pitch_rate_degrees = 90.0F,
        .pitch_limit_degrees = 75.0F,
    };
    auto runtime = simnet::ServerGameRuntime{settings, player_settings};
    auto world = flecs::world{};
    simnet::register_server_game(world, runtime);
    REQUIRE(append_boids(
                world,
                {
                    boid(1U, {-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
                    boid(2U, {5.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}),
                }
    )
                .success());

    auto const player_id = simnet::spawn_authoritative_player(world);
    REQUIRE(player_id == 3U);
    REQUIRE(
        simnet::set_authoritative_player_input(
            world,
            player_id,
            {
                .pitch_up = true,
                .yaw_right = true,
                .accelerate = true,
            }
        )
    );

    step(world, runtime, 0.25F);
    CHECK(runtime.last_step_report().entity_count == 2U);
    auto const after_move = snapshot(world, 1U);
    REQUIRE(after_move.size() == 3U);
    auto const found = std::ranges::find(after_move.ids, player_id);
    REQUIRE(found != after_move.ids.end());
    auto const offset = static_cast<std::size_t>(std::distance(after_move.ids.begin(), found));
    CHECK(after_move.positions[offset].z > 0.0F);
    CHECK(after_move.headings[offset].x < 0.0F);
    CHECK(after_move.headings[offset].y > 0.0F);
    CHECK(simnet::is_normalized_heading(after_move.headings[offset]));

    auto const second_player_id = simnet::spawn_authoritative_player(world);
    REQUIRE(second_player_id == 4U);
    CHECK(snapshot(world, 2U).size() == 4U);
    REQUIRE(simnet::set_authoritative_player_input(world, player_id, {}));
    REQUIRE(
        simnet::set_authoritative_player_input(
            world,
            second_player_id,
            {
                .yaw_left = true,
                .decelerate = true,
            }
        )
    );
    step(world, runtime, 0.25F);
    auto const independently_moved = snapshot(world, 3U);
    auto const first_position = std::ranges::find(independently_moved.ids, player_id);
    auto const second_position = std::ranges::find(independently_moved.ids, second_player_id);
    REQUIRE(first_position != independently_moved.ids.end());
    REQUIRE(second_position != independently_moved.ids.end());
    auto const first_offset =
        static_cast<std::size_t>(std::distance(independently_moved.ids.begin(), first_position));
    auto const second_offset =
        static_cast<std::size_t>(std::distance(independently_moved.ids.begin(), second_position));
    CHECK(independently_moved.headings[first_offset].x < 0.0F);
    CHECK(independently_moved.headings[second_offset].x > 0.0F);
    REQUIRE(simnet::delete_authoritative_player(world, player_id));
    auto const after_first_disconnect = snapshot(world, 4U);
    CHECK(
        std::ranges::find(after_first_disconnect.ids, player_id) == after_first_disconnect.ids.end()
    );
    CHECK(
        std::ranges::find(after_first_disconnect.ids, second_player_id) !=
        after_first_disconnect.ids.end()
    );
    REQUIRE(simnet::delete_authoritative_player(world, second_player_id));
    CHECK_FALSE(simnet::set_authoritative_player_input(world, player_id, {}));
    CHECK(snapshot(world, 5U).size() == 2U);
}

TEST_CASE("disabled Player influence preserves exact Boid results", "[boids][player]")
{
    auto settings = test_settings();
    auto control_runtime = simnet::ServerGameRuntime{settings, stationary_player_settings()};
    auto player_runtime = simnet::ServerGameRuntime{settings, stationary_player_settings()};
    auto control_world = flecs::world{};
    auto player_world = flecs::world{};
    simnet::register_server_game(control_world, control_runtime);
    simnet::register_server_game(player_world, player_runtime);
    auto const initial = std::vector{
        boid(1U, {-2.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}),
        boid(2U, {2.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}),
    };
    REQUIRE(simnet::append_authoritative_boids(control_world, initial).success());
    REQUIRE(simnet::append_authoritative_boids(player_world, initial).success());
    REQUIRE(simnet::spawn_authoritative_player(player_world) == 3U);

    for (auto tick = 0U; tick < 30U; ++tick)
    {
        step(control_world, control_runtime);
        step(player_world, player_runtime);
    }
    CHECK(
        canonical_boid_hash(snapshot(player_world, 30U)) ==
        canonical_boid_hash(snapshot(control_world, 30U))
    );
}

TEST_CASE(
    "duplicate Player heartbeats are idempotent and a neutral heartbeat recovers loss",
    "[boids][player][delivery][peer]"
)
{
    auto recovered_runtime =
        simnet::ServerGameRuntime{test_settings(), stationary_player_settings()};
    auto stuck_runtime = simnet::ServerGameRuntime{test_settings(), stationary_player_settings()};
    auto recovered_world = flecs::world{};
    auto stuck_world = flecs::world{};
    simnet::register_server_game(recovered_world, recovered_runtime);
    simnet::register_server_game(stuck_world, stuck_runtime);

    auto const recovered_player = simnet::spawn_authoritative_player(recovered_world);
    auto const recovered_other = simnet::spawn_authoritative_player(recovered_world);
    auto const stuck_player = simnet::spawn_authoritative_player(stuck_world);
    auto const stuck_other = simnet::spawn_authoritative_player(stuck_world);
    REQUIRE(recovered_player == stuck_player);
    REQUIRE(recovered_other == stuck_other);

    auto const active = simnet::PlayerControlState{
        .yaw_right = true,
        .accelerate = true,
    };
    auto const other = simnet::PlayerControlState{
        .pitch_up = true,
    };
    REQUIRE(simnet::set_authoritative_player_input(recovered_world, recovered_player, active));
    REQUIRE(simnet::set_authoritative_player_input(stuck_world, stuck_player, active));
    REQUIRE(simnet::set_authoritative_player_input(stuck_world, stuck_player, active));
    REQUIRE(simnet::set_authoritative_player_input(recovered_world, recovered_other, other));
    REQUIRE(simnet::set_authoritative_player_input(stuck_world, stuck_other, other));
    step(recovered_world, recovered_runtime, 0.1F);
    step(stuck_world, stuck_runtime, 0.1F);
    CHECK(
        canonical_hash(snapshot(recovered_world, 1U)) == canonical_hash(snapshot(stuck_world, 1U))
    );

    step(recovered_world, recovered_runtime, 0.1F);
    step(stuck_world, stuck_runtime, 0.1F);
    CHECK(
        canonical_hash(snapshot(recovered_world, 2U)) == canonical_hash(snapshot(stuck_world, 2U))
    );

    REQUIRE(simnet::set_authoritative_player_input(recovered_world, recovered_player, {}));
    step(recovered_world, recovered_runtime, 0.1F);
    step(stuck_world, stuck_runtime, 0.1F);
    auto const recovered = snapshot(recovered_world, 3U);
    auto const stuck = snapshot(stuck_world, 3U);
    CHECK(canonical_hash(recovered) != canonical_hash(stuck));

    auto const recovered_found = std::ranges::find(recovered.ids, recovered_other);
    auto const stuck_found = std::ranges::find(stuck.ids, stuck_other);
    REQUIRE(recovered_found != recovered.ids.end());
    REQUIRE(stuck_found != stuck.ids.end());
    auto const recovered_index =
        static_cast<std::size_t>(std::distance(recovered.ids.begin(), recovered_found));
    auto const stuck_index =
        static_cast<std::size_t>(std::distance(stuck.ids.begin(), stuck_found));
    CHECK(recovered.positions[recovered_index].x == stuck.positions[stuck_index].x);
    CHECK(recovered.positions[recovered_index].y == stuck.positions[stuck_index].y);
    CHECK(recovered.positions[recovered_index].z == stuck.positions[stuck_index].z);
    CHECK(recovered.headings[recovered_index].x == stuck.headings[stuck_index].x);
    CHECK(recovered.headings[recovered_index].y == stuck.headings[stuck_index].y);
    CHECK(recovered.headings[recovered_index].z == stuck.headings[stuck_index].z);
}

TEST_CASE("Player lure and predator steer in their authoritative directions", "[boids][player]")
{
    SECTION("lure points toward the Player")
    {
        auto settings = isolated_influence_settings();
        settings.player_lure = {.enabled = true, .radius = 10.0F, .max_acceleration = 5.0F};
        auto runtime = simnet::ServerGameRuntime{settings, stationary_player_settings()};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(append_boids(world, {boid(1U, {}, {0.0F, 1.0F, 0.0F})}).success());
        auto const player = simnet::spawn_authoritative_player(world);
        REQUIRE(player == 2U);
        set_player_position(world, player, {5.0F, 0.0F, 0.0F});
        runtime.select_boid(1U);
        step(world, runtime, 0.1F);
        auto const debug = runtime.selected_boid_debug();
        REQUIRE(debug.has_value());
        CHECK(debug->lure_source_count == 1U);
        CHECK(debug->predator_source_count == 0U);
        CHECK(debug->lure.x > 0.0F);
        CHECK(debug->acceleration.x > 0.0F);
        CHECK(simnet::length(debug->lure) <= 5.0F);
    }

    SECTION("predator points away from the Player")
    {
        auto settings = isolated_influence_settings();
        settings.player_predator = {
            .enabled = true,
            .radius = 10.0F,
            .max_acceleration = 8.0F,
        };
        auto runtime = simnet::ServerGameRuntime{settings, stationary_player_settings()};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(append_boids(world, {boid(1U, {}, {0.0F, 1.0F, 0.0F})}).success());
        auto const player = simnet::spawn_authoritative_player(world);
        REQUIRE(player == 2U);
        set_player_position(world, player, {5.0F, 0.0F, 0.0F});
        runtime.select_boid(1U);
        step(world, runtime, 0.1F);
        auto const debug = runtime.selected_boid_debug();
        REQUIRE(debug.has_value());
        CHECK(debug->predator_source_count == 1U);
        CHECK(debug->predator.x < 0.0F);
        CHECK(debug->acceleration.x < 0.0F);
        CHECK(simnet::length(debug->predator) <= 8.0F);
    }
}

TEST_CASE("Player influence has smooth compact support and finite overlap", "[boids][player]")
{
    auto lure_at = [](simnet::Vec3f player_position)
    {
        auto settings = isolated_influence_settings();
        settings.player_lure = {.enabled = true, .radius = 10.0F, .max_acceleration = 5.0F};
        auto runtime = simnet::ServerGameRuntime{settings, stationary_player_settings()};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(append_boids(world, {boid(1U, {}, {0.0F, 1.0F, 0.0F})}).success());
        auto const player = simnet::spawn_authoritative_player(world);
        REQUIRE(player == 2U);
        set_player_position(world, player, player_position);
        runtime.select_boid(1U);
        step(world, runtime, 0.1F);
        auto const debug = runtime.selected_boid_debug();
        REQUIRE(debug.has_value());
        return *debug;
    };

    auto const overlap = lure_at({});
    CHECK(overlap.lure_source_count == 1U);
    CHECK(simnet::is_finite(overlap.lure));
    CHECK(simnet::length_squared(overlap.lure) == 0.0F);
    auto const near = lure_at({2.0F, 0.0F, 0.0F});
    auto const far = lure_at({8.0F, 0.0F, 0.0F});
    CHECK(simnet::length(near.lure) > simnet::length(far.lure));
    auto const boundary = lure_at({10.0F, 0.0F, 0.0F});
    CHECK(boundary.lure_source_count == 1U);
    CHECK(simnet::length_squared(boundary.lure) == 0.0F);
    auto const outside = lure_at({10.01F, 0.0F, 0.0F});
    CHECK(outside.lure_source_count == 0U);
    CHECK(simnet::length_squared(outside.lure) == 0.0F);

    auto predator_at = [](simnet::Vec3f player_position)
    {
        auto settings = isolated_influence_settings();
        settings.player_predator = {
            .enabled = true,
            .radius = 10.0F,
            .max_acceleration = 8.0F,
        };
        auto runtime = simnet::ServerGameRuntime{settings, stationary_player_settings()};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(append_boids(world, {boid(1U, {}, {0.0F, 1.0F, 0.0F})}).success());
        auto const player = simnet::spawn_authoritative_player(world);
        REQUIRE(player == 2U);
        set_player_position(world, player, player_position);
        runtime.select_boid(1U);
        step(world, runtime, 0.1F);
        auto const debug = runtime.selected_boid_debug();
        REQUIRE(debug.has_value());
        return *debug;
    };
    auto const first = predator_at({});
    auto const second = predator_at({});
    CHECK(first.predator_source_count == 1U);
    CHECK(simnet::is_finite(first.predator));
    CHECK(simnet::length(first.predator) == Catch::Approx(8.0F));
    CHECK(first.predator.x == second.predator.x);
    CHECK(first.predator.y == second.predator.y);
    CHECK(first.predator.z == second.predator.z);
    auto const predator_boundary = predator_at({10.0F, 0.0F, 0.0F});
    CHECK(predator_boundary.predator_source_count == 1U);
    CHECK(simnet::length_squared(predator_boundary.predator) == 0.0F);
    auto const predator_outside = predator_at({10.01F, 0.0F, 0.0F});
    CHECK(predator_outside.predator_source_count == 0U);
}

TEST_CASE(
    "multiple Players accumulate by stable identity and deletion removes influence",
    "[boids][player]"
)
{
    auto settings = isolated_influence_settings();
    settings.player_lure = {.enabled = true, .radius = 10.0F, .max_acceleration = 5.0F};
    auto runtime = simnet::ServerGameRuntime{settings, stationary_player_settings()};
    auto world = flecs::world{};
    simnet::register_server_game(world, runtime);
    REQUIRE(append_boids(world, {boid(1U, {}, {0.0F, 0.0F, 1.0F})}).success());
    auto const first = simnet::spawn_authoritative_player(world);
    auto const second = simnet::spawn_authoritative_player(world);
    auto const third = simnet::spawn_authoritative_player(world);
    REQUIRE(first == 2U);
    REQUIRE(second == 3U);
    REQUIRE(third == 4U);
    set_player_position(world, third, {-4.0F, 0.0F, 0.0F});
    set_player_position(world, first, {2.0F, 0.0F, 0.0F});
    set_player_position(world, second, {0.0F, 3.0F, 0.0F});
    runtime.select_boid(1U);
    step(world, runtime, 0.1F);
    auto debug = runtime.selected_boid_debug();
    REQUIRE(debug.has_value());
    CHECK(debug->lure_source_count == 3U);
    auto const expected_x = (0.96F - 0.84F) * 5.0F;
    auto const expected_y = 0.91F * 5.0F;
    CHECK(debug->lure.x == Catch::Approx(expected_x));
    CHECK(debug->lure.y == Catch::Approx(expected_y));

    REQUIRE(simnet::delete_authoritative_player(world, first));
    REQUIRE(simnet::delete_authoritative_player(world, second));
    REQUIRE(simnet::delete_authoritative_player(world, third));
    step(world, runtime, 0.1F);
    debug = runtime.selected_boid_debug();
    REQUIRE(debug.has_value());
    CHECK(debug->lure_source_count == 0U);
    CHECK(simnet::length_squared(debug->lure) == 0.0F);
}

TEST_CASE("predator safety has priority over lure social acceleration", "[boids][player]")
{
    auto settings = isolated_influence_settings();
    settings.max_acceleration = 12.0F;
    settings.player_lure = {.enabled = true, .radius = 10.0F, .max_acceleration = 5.0F};
    settings.player_predator = {
        .enabled = true,
        .radius = 1.0F,
        .max_acceleration = 12.0F,
    };
    auto runtime = simnet::ServerGameRuntime{settings, stationary_player_settings()};
    auto world = flecs::world{};
    simnet::register_server_game(world, runtime);
    REQUIRE(append_boids(world, {boid(1U, {}, {0.0F, 0.0F, 1.0F})}).success());
    auto const overlapping = simnet::spawn_authoritative_player(world);
    auto const attracting = simnet::spawn_authoritative_player(world);
    REQUIRE(overlapping == 2U);
    REQUIRE(attracting == 3U);
    set_player_position(world, attracting, {5.0F, 0.0F, 0.0F});
    runtime.select_boid(1U);
    step(world, runtime, 0.1F);
    auto const debug = runtime.selected_boid_debug();
    REQUIRE(debug.has_value());
    CHECK(debug->lure_source_count == 2U);
    CHECK(debug->predator_source_count == 1U);
    CHECK(debug->lure.x > 0.0F);
    CHECK(simnet::length(debug->predator) == Catch::Approx(12.0F));
    CHECK(debug->acceleration.x == Catch::Approx(debug->predator.x));
    CHECK(debug->acceleration.y == Catch::Approx(debug->predator.y));
    CHECK(debug->acceleration.z == Catch::Approx(debug->predator.z));
    CHECK(simnet::length(debug->acceleration) <= 12.0001F);
    CHECK(debug->speed <= settings.max_speed);
}

TEST_CASE("player yaw follows the right-handed chase convention", "[player]")
{
    auto settings = test_settings();
    auto player_settings = simnet::PlayerMovementSettings{
        .world_half = 10.0F,
        .cruise_speed = 2.0F,
        .boost_speed = 4.0F,
        .slow_speed = 1.0F,
        .speed_change_rate = 10.0F,
        .yaw_acceleration_degrees = 360.0F,
        .pitch_acceleration_degrees = 300.0F,
        .yaw_damping = 8.0F,
        .pitch_damping = 8.0F,
        .max_yaw_rate_degrees = 120.0F,
        .max_pitch_rate_degrees = 90.0F,
        .pitch_limit_degrees = 75.0F,
    };
    auto runtime = simnet::ServerGameRuntime{settings, player_settings};
    auto world = flecs::world{};
    simnet::register_server_game(world, runtime);

    auto player_id = simnet::spawn_authoritative_player(world);
    REQUIRE(player_id != 0U);
    REQUIRE(
        simnet::set_authoritative_player_input(
            world,
            player_id,
            {
                .yaw_right = true,
            }
        )
    );
    step(world, runtime, 1.0F / 60.0F);
    auto state = snapshot(world, 1U);
    REQUIRE(state.size() == 1U);
    CHECK(state.headings.front().x < 0.0F);

    auto const former_player_id = player_id;
    REQUIRE(simnet::delete_authoritative_player(world, former_player_id));
    player_id = simnet::spawn_authoritative_player(world);
    REQUIRE(player_id > former_player_id);
    REQUIRE(
        simnet::set_authoritative_player_input(
            world,
            player_id,
            {
                .yaw_left = true,
            }
        )
    );
    step(world, runtime, 1.0F / 60.0F);
    state = snapshot(world, 2U);
    REQUIRE(state.size() == 1U);
    CHECK(state.headings.front().x > 0.0F);
}

TEST_CASE("player steering accelerates, damps, and respects angular limits", "[player]")
{
    auto settings = test_settings();
    auto player_settings = simnet::PlayerMovementSettings{
        .world_half = 100.0F,
        .cruise_speed = 2.0F,
        .boost_speed = 4.0F,
        .slow_speed = 1.0F,
        .speed_change_rate = 10.0F,
        .yaw_acceleration_degrees = 360.0F,
        .pitch_acceleration_degrees = 300.0F,
        .yaw_damping = 8.0F,
        .pitch_damping = 8.0F,
        .max_yaw_rate_degrees = 120.0F,
        .max_pitch_rate_degrees = 90.0F,
        .pitch_limit_degrees = 80.0F,
    };
    auto runtime = simnet::ServerGameRuntime{settings, player_settings};
    auto world = flecs::world{};
    simnet::register_server_game(world, runtime);
    auto const player_id = simnet::spawn_authoritative_player(world);
    REQUIRE(player_id != 0U);
    REQUIRE(
        simnet::set_authoritative_player_input(
            world,
            player_id,
            {
                .yaw_right = true,
            }
        )
    );

    auto yaw_at = [&](simnet::Tick tick)
    {
        auto const state = snapshot(world, tick);
        REQUIRE(state.size() == 1U);
        return std::atan2(state.headings.front().x, state.headings.front().z);
    };

    step(world, runtime);
    auto previous_yaw = yaw_at(1U);
    CHECK(previous_yaw < 0.0F);
    CHECK(std::abs(previous_yaw) < 0.01F);

    auto previous_delta = std::abs(previous_yaw);
    for (auto tick = simnet::Tick{2U}; tick <= 12U; ++tick)
    {
        step(world, runtime);
        auto const yaw = yaw_at(tick);
        auto const delta = std::abs(yaw - previous_yaw);
        CHECK(delta + 0.000001F >= previous_delta);
        previous_delta = delta;
        previous_yaw = yaw;
    }

    REQUIRE(simnet::set_authoritative_player_input(world, player_id, {}));
    step(world, runtime);
    auto yaw = yaw_at(13U);
    auto released_delta = std::abs(yaw - previous_yaw);
    CHECK(released_delta < previous_delta);
    previous_yaw = yaw;
    for (auto tick = simnet::Tick{14U}; tick <= 20U; ++tick)
    {
        step(world, runtime);
        yaw = yaw_at(tick);
        auto const delta = std::abs(yaw - previous_yaw);
        CHECK(delta < released_delta);
        released_delta = delta;
        previous_yaw = yaw;
    }

    auto const paused_heading = snapshot(world, 21U).headings.front();
    auto const still_paused_heading = snapshot(world, 22U).headings.front();
    CHECK(paused_heading.x == still_paused_heading.x);
    CHECK(paused_heading.y == still_paused_heading.y);
    CHECK(paused_heading.z == still_paused_heading.z);
    step(world, runtime);
    CHECK(simnet::is_normalized_heading(snapshot(world, 23U).headings.front()));
}

TEST_CASE("player yaw and pitch rates and pitch angle are bounded", "[player]")
{
    auto settings = test_settings();
    auto player_settings = simnet::PlayerMovementSettings{
        .world_half = 100.0F,
        .cruise_speed = 2.0F,
        .boost_speed = 4.0F,
        .slow_speed = 1.0F,
        .speed_change_rate = 10.0F,
        .yaw_acceleration_degrees = 10000.0F,
        .pitch_acceleration_degrees = 10000.0F,
        .yaw_damping = 8.0F,
        .pitch_damping = 8.0F,
        .max_yaw_rate_degrees = 10.0F,
        .max_pitch_rate_degrees = 12.0F,
        .pitch_limit_degrees = 5.0F,
    };
    auto runtime = simnet::ServerGameRuntime{settings, player_settings};
    auto world = flecs::world{};
    simnet::register_server_game(world, runtime);
    auto const player_id = simnet::spawn_authoritative_player(world);
    REQUIRE(player_id != 0U);
    REQUIRE(
        simnet::set_authoritative_player_input(
            world,
            player_id,
            {
                .pitch_up = true,
                .yaw_right = true,
            }
        )
    );

    auto previous_yaw = 0.0F;
    auto previous_pitch = 0.0F;
    auto constexpr delta_time = 0.1F;
    auto constexpr maximum_yaw_step = 0.017454F;
    auto constexpr maximum_pitch_step = 0.020945F;
    auto constexpr pitch_limit = 0.087267F;
    for (auto tick = simnet::Tick{1U}; tick <= 20U; ++tick)
    {
        step(world, runtime, delta_time);
        auto const state = snapshot(world, tick);
        REQUIRE(state.size() == 1U);
        auto const heading = state.headings.front();
        auto const yaw = std::atan2(heading.x, heading.z);
        auto const pitch = std::asin(heading.y);
        CHECK(std::abs(yaw - previous_yaw) <= maximum_yaw_step);
        CHECK(std::abs(pitch - previous_pitch) <= maximum_pitch_step);
        CHECK(std::abs(pitch) <= pitch_limit);
        previous_yaw = yaw;
        previous_pitch = pitch;
    }
    CHECK(previous_yaw < 0.0F);
    CHECK(previous_pitch == Catch::Approx(pitch_limit).margin(0.00001F));
}

TEST_CASE("boid rule toggles remove their steering contribution", "[boids][config]")
{
    auto settings = test_settings();
    settings.enable_separation = false;
    settings.enable_alignment = false;
    settings.enable_cohesion = false;
    settings.enable_containment = false;
    auto runtime = simnet::ServerGameRuntime{settings};
    auto world = flecs::world{};
    simnet::register_server_game(world, runtime);
    REQUIRE(append_boids(
                world,
                {
                    boid(1U, {99.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
                    boid(2U, {98.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}),
                }
    )
                .success());
    runtime.select_boid(1U);
    step(world, runtime);
    auto const debug = runtime.selected_boid_debug();
    REQUIRE(debug.has_value());
    CHECK(simnet::length_squared(debug->separation) == 0.0F);
    CHECK(simnet::length_squared(debug->alignment) == 0.0F);
    CHECK(simnet::length_squared(debug->cohesion) == 0.0F);
    CHECK(simnet::length_squared(debug->containment) == 0.0F);
    CHECK(simnet::length_squared(debug->wander) == 0.0F);
    CHECK_FALSE(debug->wander_active);
}

TEST_CASE("deterministic wander is a capped Server-private steering input", "[boids]")
{
    auto settings = test_settings();
    settings.enable_separation = false;
    settings.enable_alignment = false;
    settings.enable_cohesion = false;
    settings.enable_containment = false;
    settings.enable_wander = true;
    settings.wander_acceleration = 0.5F;
    auto runtime = simnet::ServerGameRuntime{settings};
    auto world = flecs::world{};
    simnet::register_server_game(world, runtime);
    REQUIRE(append_boids(
                world,
                {
                    boid(7U, {}, {1.0F, 0.0F, 0.0F}),
                }
    )
                .success());
    runtime.select_boid(7U);
    step(world, runtime);
    auto const debug = runtime.selected_boid_debug();
    REQUIRE(debug.has_value());
    CHECK(debug->wander_active);
    CHECK(simnet::length(debug->wander) == Catch::Approx(0.5F));
    CHECK(std::abs(simnet::dot(debug->wander, {1.0F, 0.0F, 0.0F})) < 1.0e-5F);
}

TEST_CASE("hue rules use circular deterministic updates", "[boids][hue]")
{
    SECTION("assimilation crosses the hue wrap boundary by the short path")
    {
        auto settings = test_settings();
        settings.enable_hue_assimilation = true;
        settings.enable_hue_drift = false;
        settings.field_of_view_degrees = 360.0F;
        auto runtime = simnet::ServerGameRuntime{settings};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(append_boids(
                    world,
                    {
                        hued_boid(1U, {}, {1.0F, 0.0F, 0.0F}, 250U),
                        hued_boid(2U, {2.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 5U),
                    }
        )
                    .success());
        runtime.select_boid(1U);
        step(world, runtime, 1.0F);
        auto const debug = runtime.selected_boid_debug();
        REQUIRE(debug.has_value());
        CHECK(debug->hue_neighbor_count == 1U);
        CHECK(debug->hue_assimilation_active);
        CHECK_FALSE(debug->hue_drift_active);
        CHECK(debug->hue_delta > 0.0F);
        CHECK(debug->hue_delta < 0.1F);
        CHECK(debug->applied_hue_step > 0.0F);
    }

    SECTION("a tiny negative wrap remains inside the canonical hue range")
    {
        auto settings = test_settings();
        settings.enable_hue_assimilation = true;
        settings.enable_hue_drift = false;
        settings.hue_assimilation_rate = 1.0e-8F;
        settings.field_of_view_degrees = 360.0F;
        auto runtime = simnet::ServerGameRuntime{settings};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(append_boids(
                    world,
                    {
                        hued_boid(1U, {}, {1.0F, 0.0F, 0.0F}, 0U),
                        hued_boid(2U, {2.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 255U),
                    }
        )
                    .success());

        step(world, runtime, 1.0F);
    }

    SECTION("an isolated boid drifts toward its stable target")
    {
        auto settings = test_settings();
        settings.enable_hue_assimilation = true;
        settings.enable_hue_drift = true;
        auto runtime = simnet::ServerGameRuntime{settings};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(append_boids(
                    world,
                    {
                        hued_boid(17U, {}, {1.0F, 0.0F, 0.0F}, 0U),
                    }
        )
                    .success());
        runtime.select_boid(17U);
        step(world, runtime, 1.0F);
        auto const debug = runtime.selected_boid_debug();
        REQUIRE(debug.has_value());
        CHECK(debug->hue_neighbor_count == 0U);
        CHECK_FALSE(debug->hue_assimilation_active);
        CHECK(debug->hue_drift_active);
        CHECK(std::abs(debug->applied_hue_step) > 0.0F);
    }

    SECTION("disabled hue rules preserve hue")
    {
        auto settings = test_settings();
        settings.enable_hue_assimilation = false;
        settings.enable_hue_drift = false;
        auto runtime = simnet::ServerGameRuntime{settings};
        auto world = flecs::world{};
        simnet::register_server_game(world, runtime);
        REQUIRE(append_boids(
                    world,
                    {
                        hued_boid(1U, {}, {1.0F, 0.0F, 0.0F}, 50U),
                        hued_boid(2U, {2.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 200U),
                    }
        )
                    .success());
        runtime.select_boid(1U);
        step(world, runtime, 1.0F);
        auto const debug = runtime.selected_boid_debug();
        REQUIRE(debug.has_value());
        CHECK_FALSE(debug->hue_assimilation_active);
        CHECK_FALSE(debug->hue_drift_active);
        CHECK(debug->applied_hue_step == 0.0F);
        CHECK(snapshot(world, 1U).hues.front() == 50U);
    }
}

TEST_CASE("boid radii must be finite and positive", "[boids][config]")
{
    auto settings = test_settings();
    settings.alignment_radius = 0.0F;
    CHECK_THROWS_AS(simnet::ServerGameRuntime{settings}, std::invalid_argument);
    settings = test_settings();
    settings.cohesion_radius = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(simnet::ServerGameRuntime{settings}, std::invalid_argument);
}

TEST_CASE("invalid computed state is not partially committed", "[boids]")
{
    auto settings = test_settings();
    auto runtime = simnet::ServerGameRuntime{settings};
    auto world = flecs::world{};
    simnet::register_server_game(world, runtime);
    REQUIRE(append_boids(
                world,
                {
                    boid(1U, {}, {1.0F, 0.0F, 0.0F}),
                    boid(2U, {5.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}),
                }
    )
                .success());
    auto const before = snapshot(world, 0U);

    REQUIRE(simnet::prepare_server_game_runtime(world, runtime));
    REQUIRE(world.progress(std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(runtime.last_step_report().valid);
    auto const after = snapshot(world, 1U);
    CHECK(after.ids == before.ids);
    CHECK(canonical_hash(after) == canonical_hash(before));
}

TEST_CASE("boid snapshots are identical across Flecs worker counts", "[boids][determinism]")
{
    auto const serial = run_determinism_case(1U);
    CHECK(run_determinism_case(4U) == serial);
    CHECK(run_determinism_case(8U) == serial);

    auto const lure_serial = run_determinism_case(1U, true, false);
    CHECK(run_determinism_case(4U, true, false) == lure_serial);
    CHECK(run_determinism_case(8U, true, false) == lure_serial);

    auto const predator_serial = run_determinism_case(1U, false, true);
    CHECK(run_determinism_case(4U, false, true) == predator_serial);
    CHECK(run_determinism_case(8U, false, true) == predator_serial);

    auto const combined_serial = run_determinism_case(1U, true, true);
    CHECK(run_determinism_case(4U, true, true) == combined_serial);
    CHECK(run_determinism_case(8U, true, true) == combined_serial);
}

TEST_CASE("selected boid details are available without another simulation tick", "[boids][debug]")
{
    auto runtime = simnet::ServerGameRuntime{test_settings()};
    auto world = flecs::world{};
    simnet::register_server_game(world, runtime);
    REQUIRE(append_boids(
                world,
                {
                    boid(1U, {}, {1.0F, 0.0F, 0.0F}),
                    boid(2U, {2.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
                }
    )
                .success());

    step(world, runtime);
    REQUIRE_FALSE(runtime.selected_boid_debug().has_value());
    auto const& report = runtime.last_step_report();
    CHECK(report.diagnostics.grid.entity_count == 2U);
    CHECK(report.diagnostics.raw_candidates_mean == 1.0);
    CHECK(report.diagnostics.retained_neighbors_mean == 1.0);
    CHECK(report.diagnostics.neighbor_cap_hit_count == 0U);
    CHECK(report.diagnostics.separation_neighbors_mean == 1.0);
    CHECK(report.diagnostics.speed_min > 0.0F);
    CHECK(report.phases.progress_ms >= report.phases.compute_ms);

    auto const before = canonical_hash(snapshot(world, 1U));
    runtime.select_boid(1U);
    auto const debug = runtime.selected_boid_debug();
    REQUIRE(debug.has_value());
    CHECK(debug->id == 1U);
    CHECK(debug->raw_candidate_count == 1U);
    CHECK(debug->retained_neighbor_count == 1U);
    CHECK(debug->queried_cell_bounds.size() > 1U);
    CHECK(debug->alignment_radius == test_settings().alignment_radius);
    CHECK(debug->cohesion_radius == test_settings().cohesion_radius);
    CHECK(debug->maximum_neighbors == test_settings().max_neighbors);
    CHECK(canonical_hash(snapshot(world, 1U)) == before);

    auto normal_runtime = simnet::ServerGameRuntime{test_settings()};
    auto normal_world = flecs::world{};
    simnet::register_server_game(normal_world, normal_runtime);
    REQUIRE(append_boids(
                normal_world,
                {
                    boid(1U, {}, {1.0F, 0.0F, 0.0F}),
                    boid(2U, {2.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}),
                }
    )
                .success());
    normal_runtime.select_boid(1U);
    step(normal_world, normal_runtime);
    auto const normal_debug = normal_runtime.selected_boid_debug();
    REQUIRE(normal_debug.has_value());
    CHECK(normal_debug->velocity.x == debug->velocity.x);
    CHECK(normal_debug->velocity.y == debug->velocity.y);
    CHECK(normal_debug->velocity.z == debug->velocity.z);
    CHECK(normal_debug->acceleration.x == debug->acceleration.x);
    CHECK(normal_debug->acceleration.y == debug->acceleration.y);
    CHECK(normal_debug->acceleration.z == debug->acceleration.z);
    CHECK(normal_debug->raw_candidate_count == debug->raw_candidate_count);
    CHECK(normal_debug->queried_cell_bounds.size() == debug->queried_cell_bounds.size());
}
