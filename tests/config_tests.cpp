#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string_view>

import simnet.config;

namespace
{
    class TemporaryConfig
    {
    public:
        TemporaryConfig(std::string_view name, std::string_view contents)
            : path_(std::filesystem::temp_directory_path() / name)
        {
            auto file = std::ofstream { path_ };
            file << contents;
        }

        ~TemporaryConfig()
        {
            std::error_code error {};
            std::filesystem::remove(path_, error);
        }

        [[nodiscard]] std::filesystem::path const& path() const noexcept
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };
}

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
    changed.player.yaw_acceleration_degrees += 1.0F;
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

TEST_CASE("legacy observer configuration normalizes to stationary observer", "[config]")
{
    auto const legacy = TemporaryConfig {
        "simnet_legacy_observer_config.json",
        R"({
            "gameplay": { "role": "observer" },
            "visualization": {
                "debug_observer_interest_radius": 42.0,
                "debug_observer_vertical_fov_degrees": 55.0
            }
        })"
    };
    auto const config = simnet::load_client_config(legacy.path());

    CHECK(config.gameplay.role == "stationary_observer");
    CHECK(config.visualization.stationary_observer_interest_radius == 42.0F);
    CHECK(config.visualization.stationary_observer_vertical_fov_degrees == 55.0F);
}

TEST_CASE("canonical and legacy observer visualization keys conflict", "[config]")
{
    auto const conflicting = TemporaryConfig {
        "simnet_conflicting_observer_config.json",
        R"({
            "visualization": {
                "stationary_observer_interest_radius": 42.0,
                "debug_observer_interest_radius": 42.0
            }
        })"
    };

    CHECK_THROWS(simnet::load_client_config(conflicting.path()));
}

TEST_CASE("player pitch limit stays clear of the vertical camera singularity", "[config][player]")
{
    auto const accepted = TemporaryConfig {
        "simnet_player_pitch_accepted.json",
        R"({ "player": { "pitch_limit_degrees": 85.0 } })"
    };
    CHECK(simnet::load_shared_config(accepted.path()).player.pitch_limit_degrees
        == 85.0F);

    auto const rejected = TemporaryConfig {
        "simnet_player_pitch_rejected.json",
        R"({ "player": { "pitch_limit_degrees": 85.1 } })"
    };
    CHECK_THROWS(simnet::load_shared_config(rejected.path()));
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
    CHECK(config.player.yaw_acceleration_degrees == 360.0F);
    CHECK(config.player.pitch_acceleration_degrees == 300.0F);
    CHECK(config.player.max_yaw_rate_degrees == 120.0F);
    CHECK(config.player.max_pitch_rate_degrees == 90.0F);
}
