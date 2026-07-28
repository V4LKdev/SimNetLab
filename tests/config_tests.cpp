#include <catch2/catch_test_macros.hpp>

import simnet.config;

TEST_CASE("network compatibility fingerprint covers shared configuration", "[config]")
{
    auto const baseline = simnet::default_shared_config();
    auto const fingerprint = simnet::fingerprint_network_compatibility(baseline);

    auto changed = baseline;
    changed.simulation.tick_rate_hz = 30.0;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    changed.pipeline.enable_delta = !changed.pipeline.enable_delta;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    changed.boids.separation_radius += 1.0F;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    changed.boids.enable_wander = !changed.boids.enable_wander;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    changed.boids.alignment_radius += 1.0F;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    changed.player.yaw_rate_degrees += 1.0F;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);
}

TEST_CASE("visual interpolation is local runtime configuration", "[config]")
{
    auto const shared = simnet::default_shared_config();
    auto const baseline = simnet::default_server_config();
    auto changed = baseline;
    changed.visualization.interpolation_enabled =
        !changed.visualization.interpolation_enabled;

    CHECK(simnet::fingerprint_runtime_config(shared, changed).value
        != simnet::fingerprint_runtime_config(shared, baseline).value);
}

TEST_CASE("client gameplay role is local runtime configuration", "[config][player]")
{
    auto const shared = simnet::default_shared_config();
    auto const observer = simnet::default_client_config();
    auto player = observer;
    player.gameplay.role = "player";

    CHECK(simnet::fingerprint_runtime_config(shared, player).value
        != simnet::fingerprint_runtime_config(shared, observer).value);
}

TEST_CASE("boids demo profile loads a conservative deterministic scenario", "[config]")
{
    auto const path = simnet::default_shared_config_path().parent_path()
        / "shared_boids_demo.json";
    auto const config = simnet::load_shared_config(path);

    CHECK(config.simulation.initial_boid_count == 1000U);
    CHECK(config.simulation.world_half == 65.0F);
    CHECK(config.spatial.cell_size == 18.0F);
    CHECK(config.spatial.max_neighbors == 64U);
    CHECK(config.boids.min_speed <= config.boids.cruise_speed);
    CHECK(config.boids.cruise_speed <= config.boids.max_speed);
    CHECK(config.boids.alignment_radius == 18.0F);
    CHECK(config.boids.cohesion_radius == 18.0F);
    CHECK(config.boids.enable_wander);
    CHECK(config.boids.enable_hue_assimilation);
}
