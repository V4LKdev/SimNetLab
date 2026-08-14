#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#include "test_temporary_directory.hpp"

import simnet.app_common;
import simnet.app_compression_dictionary;
import simnet.compression;
import simnet.config;
import simnet.core;
import simnet.pipeline;
import simnet.snapshot;

namespace
{
    using TestTemporaryDirectory = simnet::test::TestTemporaryDirectory;

    class TemporaryConfig
    {
      public:
        TemporaryConfig(std::string_view name, std::string_view contents)
            : directory_(TestTemporaryDirectory{"simnet_config"}), path_(directory_.path() / name)
        {
            auto file = std::ofstream{path_};
            if (!file)
            {
                throw std::runtime_error{"Failed to open temporary config file: " + path_.string()};
            }
            file << contents;
            if (!file)
            {
                throw std::runtime_error{
                    "Failed to write temporary config file: " + path_.string()
                };
            }
            file.flush();
            if (!file)
            {
                throw std::runtime_error{
                    "Failed to flush temporary config file: " + path_.string()
                };
            }
            file.close();
            if (!file)
            {
                throw std::runtime_error{
                    "Failed to close temporary config file: " + path_.string()
                };
            }
        }

        [[nodiscard]] std::filesystem::path const& path() const noexcept
        {
            return path_;
        }

      private:
        TestTemporaryDirectory directory_;
        std::filesystem::path path_;
    };

    [[nodiscard]] std::filesystem::path maintained_config_directory()
    {
        return std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    }

    void
    check_transport_equal(simnet::TransportConfig const& left, simnet::TransportConfig const& right)
    {
        CHECK(left.host == right.host);
        CHECK(left.port == right.port);
        CHECK(left.max_clients == right.max_clients);
        CHECK(left.max_payload_bytes == right.max_payload_bytes);
        CHECK(left.send_size_policy == right.send_size_policy);
    }

    void check_visualization_equal(
        simnet::VisualizationConfig const& left,
        simnet::VisualizationConfig const& right
    )
    {
        CHECK(left.enabled == right.enabled);
        CHECK(left.interpolation_enabled == right.interpolation_enabled);
        CHECK(left.window_width == right.window_width);
        CHECK(left.window_height == right.window_height);
        CHECK(left.panel_width == right.panel_width);
        CHECK(left.target_fps == right.target_fps);
        CHECK(left.entity_scale == right.entity_scale);
        CHECK(left.picking_radius == right.picking_radius);
        CHECK(
            left.stationary_observer_interest_radius == right.stationary_observer_interest_radius
        );
        CHECK(
            left.stationary_observer_vertical_fov_degrees ==
            right.stationary_observer_vertical_fov_degrees
        );
        CHECK(left.max_visible_spatial_cells == right.max_visible_spatial_cells);
        CHECK(left.entity_mesh_path == right.entity_mesh_path);
    }

    void
    check_telemetry_equal(simnet::TelemetryConfig const& left, simnet::TelemetryConfig const& right)
    {
        CHECK(left.console_log_enabled == right.console_log_enabled);
        CHECK(left.file_log_enabled == right.file_log_enabled);
        CHECK(left.log_directory == right.log_directory);
        CHECK(left.min_level == right.min_level);
        CHECK(left.metrics_csv_enabled == right.metrics_csv_enabled);
    }

    void check_shared_equal(simnet::SharedConfig const& left, simnet::SharedConfig const& right)
    {
        CHECK(left.run.seed == right.run.seed);
        CHECK(left.simulation.tick_rate_hz == right.simulation.tick_rate_hz);
        CHECK(left.simulation.world_half == right.simulation.world_half);
        CHECK(left.simulation.initial_boid_count == right.simulation.initial_boid_count);
        CHECK(left.spatial.cell_size == right.spatial.cell_size);
        CHECK(left.spatial.max_neighbors == right.spatial.max_neighbors);
        CHECK(left.boids.enable_separation == right.boids.enable_separation);
        CHECK(left.boids.enable_alignment == right.boids.enable_alignment);
        CHECK(left.boids.enable_cohesion == right.boids.enable_cohesion);
        CHECK(left.boids.enable_containment == right.boids.enable_containment);
        CHECK(left.boids.enable_wander == right.boids.enable_wander);
        CHECK(left.boids.enable_hue_assimilation == right.boids.enable_hue_assimilation);
        CHECK(left.boids.enable_hue_drift == right.boids.enable_hue_drift);
        CHECK(left.boids.min_speed == right.boids.min_speed);
        CHECK(left.boids.cruise_speed == right.boids.cruise_speed);
        CHECK(left.boids.max_speed == right.boids.max_speed);
        CHECK(left.boids.max_acceleration == right.boids.max_acceleration);
        CHECK(left.boids.separation_radius == right.boids.separation_radius);
        CHECK(left.boids.alignment_radius == right.boids.alignment_radius);
        CHECK(left.boids.cohesion_radius == right.boids.cohesion_radius);
        CHECK(left.boids.field_of_view_degrees == right.boids.field_of_view_degrees);
        CHECK(
            left.boids.containment_prediction_seconds == right.boids.containment_prediction_seconds
        );
        CHECK(left.boids.containment_margin == right.boids.containment_margin);
        CHECK(left.boids.separation_acceleration == right.boids.separation_acceleration);
        CHECK(left.boids.containment_acceleration == right.boids.containment_acceleration);
        CHECK(left.boids.alignment_acceleration == right.boids.alignment_acceleration);
        CHECK(left.boids.cohesion_acceleration == right.boids.cohesion_acceleration);
        CHECK(left.boids.wander_acceleration == right.boids.wander_acceleration);
        CHECK(left.boids.wander_frequency_hz == right.boids.wander_frequency_hz);
        CHECK(left.boids.hue_assimilation_rate == right.boids.hue_assimilation_rate);
        CHECK(left.boids.hue_drift_rate == right.boids.hue_drift_rate);
        CHECK(left.boids.player_lure.enabled == right.boids.player_lure.enabled);
        CHECK(left.boids.player_lure.radius == right.boids.player_lure.radius);
        CHECK(left.boids.player_lure.max_acceleration == right.boids.player_lure.max_acceleration);
        CHECK(left.boids.player_predator.enabled == right.boids.player_predator.enabled);
        CHECK(left.boids.player_predator.radius == right.boids.player_predator.radius);
        CHECK(
            left.boids.player_predator.max_acceleration ==
            right.boids.player_predator.max_acceleration
        );
        CHECK(left.player.cruise_speed == right.player.cruise_speed);
        CHECK(left.player.boost_speed == right.player.boost_speed);
        CHECK(left.player.slow_speed == right.player.slow_speed);
        CHECK(left.player.speed_change_rate == right.player.speed_change_rate);
        CHECK(left.player.yaw_acceleration_degrees == right.player.yaw_acceleration_degrees);
        CHECK(left.player.pitch_acceleration_degrees == right.player.pitch_acceleration_degrees);
        CHECK(left.player.yaw_damping == right.player.yaw_damping);
        CHECK(left.player.pitch_damping == right.player.pitch_damping);
        CHECK(left.player.max_yaw_rate_degrees == right.player.max_yaw_rate_degrees);
        CHECK(left.player.max_pitch_rate_degrees == right.player.max_pitch_rate_degrees);
        CHECK(left.player.pitch_limit_degrees == right.player.pitch_limit_degrees);
        CHECK(left.synthetic.has_value() == right.synthetic.has_value());
        if (left.synthetic.has_value() && right.synthetic.has_value())
        {
            CHECK(left.synthetic->pattern == right.synthetic->pattern);
            CHECK(
                left.synthetic->entity_change_fraction == right.synthetic->entity_change_fraction
            );
            CHECK(left.synthetic->field_change_mode == right.synthetic->field_change_mode);
        }
        CHECK(left.pipeline.send_interval_ticks == right.pipeline.send_interval_ticks);
        CHECK(left.pipeline.enable_incremental == right.pipeline.enable_incremental);
        CHECK(left.pipeline.enable_quantization == right.pipeline.enable_quantization);
        CHECK(left.pipeline.enable_oct_heading == right.pipeline.enable_oct_heading);
        CHECK(left.pipeline.enable_delta == right.pipeline.enable_delta);
        CHECK(left.pipeline.enable_delta_field_mask == right.pipeline.enable_delta_field_mask);
        CHECK(left.pipeline.enable_bit_packing == right.pipeline.enable_bit_packing);
        CHECK(left.pipeline.area_of_interest.mode == right.pipeline.area_of_interest.mode);
        CHECK(left.pipeline.area_of_interest.radius == right.pipeline.area_of_interest.radius);
        CHECK(
            left.pipeline.area_of_interest.fov_degrees ==
            right.pipeline.area_of_interest.fov_degrees
        );
        CHECK(left.pipeline.level_of_detail.mode == right.pipeline.level_of_detail.mode);
        CHECK(
            left.pipeline.level_of_detail.near_distance ==
            right.pipeline.level_of_detail.near_distance
        );
        CHECK(
            left.pipeline.level_of_detail.medium_distance ==
            right.pipeline.level_of_detail.medium_distance
        );
        CHECK(
            left.pipeline.level_of_detail.medium_interval_ticks ==
            right.pipeline.level_of_detail.medium_interval_ticks
        );
        CHECK(
            left.pipeline.level_of_detail.far_interval_ticks ==
            right.pipeline.level_of_detail.far_interval_ticks
        );
        CHECK(left.snapshot_delivery.mode == right.snapshot_delivery.mode);
        CHECK(
            left.snapshot_delivery.full_replace_after_unacknowledged_updates ==
            right.snapshot_delivery.full_replace_after_unacknowledged_updates
        );
        CHECK(left.compression.mode == right.compression.mode);
        CHECK(left.compression.level == right.compression.level);
        CHECK(left.compression.dictionary == right.compression.dictionary);
        CHECK(left.packetization.enabled == right.packetization.enabled);
        CHECK(left.packetization.max_payload_bytes == right.packetization.max_payload_bytes);
        CHECK(left.packetization.max_update_bytes == right.packetization.max_update_bytes);
        CHECK(
            left.packetization.max_chunks_per_update == right.packetization.max_chunks_per_update
        );
        CHECK(
            left.packetization.max_in_flight_updates == right.packetization.max_in_flight_updates
        );
        CHECK(left.packetization.max_incomplete_bytes == right.packetization.max_incomplete_bytes);
        CHECK(
            left.packetization.reassembly_timeout_ms == right.packetization.reassembly_timeout_ms
        );
    }

    void check_server_equal(simnet::ServerConfig const& left, simnet::ServerConfig const& right)
    {
        check_transport_equal(left.transport, right.transport);
        CHECK(left.flecs.thread_count == right.flecs.thread_count);
        check_visualization_equal(left.visualization, right.visualization);
        check_telemetry_equal(left.telemetry, right.telemetry);
    }

    void check_client_equal(simnet::ClientConfig const& left, simnet::ClientConfig const& right)
    {
        check_transport_equal(left.transport, right.transport);
        CHECK(left.gameplay.role == right.gameplay.role);
        CHECK(
            left.gameplay.stationary_observer_position ==
            right.gameplay.stationary_observer_position
        );
        check_visualization_equal(left.visualization, right.visualization);
        check_telemetry_equal(left.telemetry, right.telemetry);
    }
}

TEST_CASE("configuration rejects obsolete and unknown fields", "[config][validation]")
{
    auto const obsolete_alias = TemporaryConfig{
        "simnet_config_obsolete_alias.json",
        R"({ "boids": { "perception_radius": 12.0 } })"
    };
    CHECK_THROWS(simnet::load_shared_config(obsolete_alias.path()));

    struct InvalidConfigDocument
    {
        std::string_view name;
        std::string_view contents;
    };
    constexpr auto invalid_shared_configs = std::array{
        InvalidConfigDocument{"root", R"({ "unexpected": true })"},
        InvalidConfigDocument{"run", R"({ "run": { "unexpected": true } })"},
        InvalidConfigDocument{"simulation", R"({ "simulation": { "unexpected": true } })"},
        InvalidConfigDocument{"spatial", R"({ "spatial": { "unexpected": true } })"},
        InvalidConfigDocument{"boids", R"({ "boids": { "unexpected": true } })"},
        InvalidConfigDocument{
            "player_lure",
            R"({ "boids": { "player_lure": { "unexpected": true } } })"
        },
        InvalidConfigDocument{"player", R"({ "player": { "unexpected": true } })"},
        InvalidConfigDocument{"synthetic", R"({ "synthetic": { "unexpected": true } })"},
        InvalidConfigDocument{"pipeline", R"({ "pipeline": { "unexpected": true } })"},
        InvalidConfigDocument{
            "area_of_interest",
            R"({ "pipeline": { "area_of_interest": { "unexpected": true } } })"
        },
        InvalidConfigDocument{
            "level_of_detail",
            R"({ "pipeline": { "level_of_detail": { "unexpected": true } } })"
        },
        InvalidConfigDocument{
            "snapshot_delivery",
            R"({ "snapshot_delivery": { "unexpected": true } })"
        },
        InvalidConfigDocument{"compression", R"({ "compression": { "unexpected": true } })"},
        InvalidConfigDocument{"packetization", R"({ "packetization": { "unexpected": true } })"},
    };
    for (auto const& invalid : invalid_shared_configs)
    {
        CAPTURE(invalid.name);
        auto const file = TemporaryConfig{"simnet_config_unknown_shared.json", invalid.contents};
        CHECK_THROWS(simnet::load_shared_config(file.path()));
    }

    constexpr auto invalid_server_configs = std::array{
        InvalidConfigDocument{"root", R"({ "unexpected": true })"},
        InvalidConfigDocument{"transport", R"({ "transport": { "unexpected": true } })"},
        InvalidConfigDocument{"flecs", R"({ "flecs": { "unexpected": true } })"},
        InvalidConfigDocument{"visualization", R"({ "visualization": { "unexpected": true } })"},
        InvalidConfigDocument{"telemetry", R"({ "telemetry": { "unexpected": true } })"},
    };
    for (auto const& invalid : invalid_server_configs)
    {
        CAPTURE(invalid.name);
        auto const file = TemporaryConfig{"simnet_config_unknown_server.json", invalid.contents};
        CHECK_THROWS(simnet::load_server_config(file.path()));
    }

    constexpr auto invalid_client_configs = std::array{
        InvalidConfigDocument{"root", R"({ "unexpected": true })"},
        InvalidConfigDocument{"gameplay", R"({ "gameplay": { "unexpected": true } })"},
    };
    for (auto const& invalid : invalid_client_configs)
    {
        CAPTURE(invalid.name);
        auto const file = TemporaryConfig{"simnet_config_unknown_client.json", invalid.contents};
        CHECK_THROWS(simnet::load_client_config(file.path()));
    }
}

TEST_CASE("telemetry log levels accept only the current vocabulary", "[config][telemetry]")
{
    for (auto const level : {
             "trace",
             "debug",
             "info",
             "warn",
             "error",
             "critical",
             "off",
         })
    {
        CAPTURE(level);
        auto const contents =
            std::string{R"({ "telemetry": { "min_level": ")"} + level + R"(" } })";
        auto const file = TemporaryConfig{"simnet_config_log_level.json", contents};
        CHECK(simnet::load_server_config(file.path()).telemetry.min_level == level);
        CHECK(simnet::load_client_config(file.path()).telemetry.min_level == level);
    }

    for (auto const level : {"TRACE", "Info", " info", "info ", "", "err", "warning", "verbose"})
    {
        CAPTURE(level);
        auto const contents =
            std::string{R"({ "telemetry": { "min_level": ")"} + level + R"(" } })";
        auto const file = TemporaryConfig{"simnet_config_invalid_log_level.json", contents};
        CHECK_THROWS(simnet::load_server_config(file.path()));
        CHECK_THROWS(simnet::load_client_config(file.path()));
    }
}

TEST_CASE("integral configuration fields reject numeric conversion", "[config][validation]")
{
    for (auto const contents : {
             R"({ "run": { "seed": -1 } })",
             R"({ "simulation": { "initial_boid_count": 1.5 } })",
             R"({ "packetization": { "max_in_flight_updates": -1 } })",
             R"({ "compression": { "mode": "whole_update", "level": 1.5 } })",
         })
    {
        auto const file = TemporaryConfig{"simnet_config_invalid_integer.json", contents};
        CHECK_THROWS(simnet::load_shared_config(file.path()));
    }

    auto const invalid_port = TemporaryConfig{
        "simnet_config_invalid_port.json",
        R"({ "transport": { "port": 7777.5 } })"
    };
    CHECK_THROWS(simnet::load_server_config(invalid_port.path()));

    auto const maximum_seed = TemporaryConfig{
        "simnet_config_maximum_seed.json",
        R"({ "run": { "seed": 18446744073709551615 } })"
    };
    CHECK(
        simnet::load_shared_config(maximum_seed.path()).run.seed ==
        std::numeric_limits<std::uint64_t>::max()
    );
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
    ++changed.pipeline.send_interval_ticks;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    changed.pipeline.enable_quantization = true;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    changed.pipeline.enable_quantization = true;
    changed.pipeline.enable_oct_heading = true;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    changed.pipeline.enable_quantization = true;
    changed.pipeline.enable_oct_heading = true;
    changed.pipeline.enable_bit_packing = true;
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

    changed = baseline;
    changed.packetization.enabled = !changed.packetization.enabled;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    ++changed.packetization.max_payload_bytes;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    --changed.packetization.max_update_bytes;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    --changed.packetization.max_chunks_per_update;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    --changed.packetization.max_in_flight_updates;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    --changed.packetization.max_incomplete_bytes;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    --changed.packetization.reassembly_timeout_ms;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    changed.compression.mode = "whole_update";
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    ++changed.compression.level;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    changed.snapshot_delivery.mode = "unreliable_sequenced";
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);

    changed = baseline;
    ++changed.snapshot_delivery.full_replace_after_unacknowledged_updates;
    CHECK(simnet::fingerprint_network_compatibility(changed).value != fingerprint.value);
}

TEST_CASE(
    "pipeline representation configuration is strict and maps through the application",
    "[config][pipeline]"
)
{
    auto const defaults = simnet::default_shared_config();
    CHECK(defaults.pipeline.send_interval_ticks == 1U);
    CHECK_FALSE(defaults.pipeline.enable_quantization);
    CHECK_FALSE(defaults.pipeline.enable_oct_heading);
    CHECK_FALSE(defaults.pipeline.enable_bit_packing);
    CHECK_FALSE(defaults.pipeline.enable_delta_field_mask);

    auto const accepted = TemporaryConfig{
        "simnet_pipeline_representation_accepted.json",
        R"({
            "simulation": { "world_half": 70.0 },
            "pipeline": {
                "send_interval_ticks": 4,
                "enable_quantization": true,
                "enable_oct_heading": true,
                "enable_bit_packing": true
            }
        })"
    };
    auto const loaded = simnet::load_shared_config(accepted.path());
    CHECK(loaded.pipeline.send_interval_ticks == 4U);
    CHECK(loaded.pipeline.enable_quantization);
    CHECK(loaded.pipeline.enable_oct_heading);
    CHECK(loaded.pipeline.enable_bit_packing);

    auto const pipeline = simnet::app::make_snapshot_pipeline(loaded);
    CHECK(simnet::has_all_flags(pipeline.techniques, simnet::PipelineTechniqueFlags::SendInterval));
    CHECK(simnet::has_all_flags(pipeline.techniques, simnet::PipelineTechniqueFlags::Quantization));
    CHECK(simnet::has_all_flags(pipeline.techniques, simnet::PipelineTechniqueFlags::OctHeading));
    CHECK(simnet::has_all_flags(pipeline.techniques, simnet::PipelineTechniqueFlags::BitPacking));
    CHECK(pipeline.send_interval.interval_ticks == 4U);
    CHECK(pipeline.quantization.position_bounds.min.x == -70.0F);
    CHECK(pipeline.quantization.position_bounds.max.x == 70.0F);

    auto every_tick = loaded;
    every_tick.pipeline.send_interval_ticks = 1U;
    auto const every_tick_pipeline = simnet::app::make_snapshot_pipeline(every_tick);
    CHECK_FALSE(
        simnet::has_all_flags(
            every_tick_pipeline.techniques,
            simnet::PipelineTechniqueFlags::SendInterval
        )
    );
    CHECK(every_tick_pipeline.send_interval.interval_ticks == 1U);
    CHECK(
        simnet::pipeline_decode_signature(every_tick_pipeline) ==
        simnet::pipeline_decode_signature(pipeline)
    );

    auto const& raw = defaults;
    auto quantized = defaults;
    quantized.pipeline.enable_quantization = true;
    auto oct = quantized;
    oct.pipeline.enable_oct_heading = true;
    auto bit_packed = oct;
    bit_packed.pipeline.enable_bit_packing = true;
    auto const raw_pipeline = simnet::app::make_snapshot_pipeline(raw);
    auto const quantized_pipeline = simnet::app::make_snapshot_pipeline(quantized);
    auto const oct_pipeline = simnet::app::make_snapshot_pipeline(oct);
    auto const bit_packed_pipeline = simnet::app::make_snapshot_pipeline(bit_packed);
    CHECK(
        simnet::pipeline_decode_signature(raw_pipeline) !=
        simnet::pipeline_decode_signature(quantized_pipeline)
    );
    CHECK(
        simnet::pipeline_decode_signature(quantized_pipeline) !=
        simnet::pipeline_decode_signature(oct_pipeline)
    );
    CHECK(
        simnet::pipeline_decode_signature(oct_pipeline) !=
        simnet::pipeline_decode_signature(bit_packed_pipeline)
    );

    auto snapshot = simnet::WorldSnapshot{};
    snapshot.tick = 4U;
    snapshot.ids.push_back(1U);
    snapshot.classifications.push_back(simnet::EntityClassification{1U});
    snapshot.positions.push_back({1.25F, -2.5F, 3.75F});
    snapshot.headings.push_back(simnet::normalize_or({1.0F, 2.0F, 3.0F}, {.x = 1.0F}));
    snapshot.hues.push_back(42U);
    auto check_round_trip = [&](simnet::PipelineDefinition const& definition)
    {
        auto encode_state = simnet::ClientReplicationState{};
        auto decode_state = simnet::ClientReplicationState{};
        auto scratch = simnet::PipelineScratch{};
        auto const encoded =
            simnet::encode_snapshot(definition, encode_state, scratch, {.snapshot = &snapshot});
        REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
        auto const decoded =
            simnet::decode_update(definition, decode_state, {.bytes = encoded.update.bytes});
        REQUIRE(decoded.report.valid);
        REQUIRE(decoded.update.upserts.size() == 1U);
        CHECK(decoded.update.upserts.front().id == 1U);
    };
    check_round_trip(raw_pipeline);
    check_round_trip(quantized_pipeline);
    check_round_trip(oct_pipeline);
    check_round_trip(bit_packed_pipeline);

    auto const runtime_fingerprint =
        simnet::fingerprint_runtime_config(defaults, simnet::default_server_config());
    for (auto const& changed : {loaded, quantized, oct, bit_packed})
    {
        CHECK(
            simnet::fingerprint_runtime_config(changed, simnet::default_server_config()).value !=
            runtime_fingerprint.value
        );
    }

    for (auto const contents : {
             R"({ "pipeline": { "send_interval_ticks": 0 } })",
             R"({ "pipeline": { "send_interval_ticks": -1 } })",
             R"({ "pipeline": { "send_interval_ticks": 1.5 } })",
             R"({ "pipeline": { "send_interval_ticks": 4294967296 } })",
             R"({ "pipeline": { "send_interval_ticks": "4" } })",
             R"({ "pipeline": { "enable_oct_heading": true } })",
             R"({ "pipeline": { "enable_bit_packing": true } })",
             R"({ "pipeline": { "enable_quantization": true, "enable_bit_packing": true } })",
             R"({ "pipeline": { "enable_quantization": "true" } })",
             R"({ "pipeline": { "enable_oct_heading": 1 } })",
             R"({ "pipeline": { "enable_bit_packing": 1 } })",
             R"({ "pipeline": { "representation": "quantized" } })",
         })
    {
        auto const invalid =
            TemporaryConfig{"simnet_pipeline_representation_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }

    auto const maximum_interval = TemporaryConfig{
        "simnet_pipeline_maximum_interval.json",
        R"({ "pipeline": { "send_interval_ticks": 4294967295 } })"
    };
    CHECK(
        simnet::load_shared_config(maximum_interval.path()).pipeline.send_interval_ticks ==
        std::numeric_limits<std::uint32_t>::max()
    );
}

TEST_CASE("field-mask Delta configuration is strict and fingerprinted", "[config][pipeline]")
{
    auto const accepted = TemporaryConfig{
        "simnet_pipeline_delta_field_mask_accepted.json",
        R"({ "pipeline": { "enable_delta": true, "enable_delta_field_mask": true } })"
    };
    auto const loaded = simnet::load_shared_config(accepted.path());
    CHECK(loaded.pipeline.enable_delta);
    CHECK(loaded.pipeline.enable_delta_field_mask);
    auto const pipeline = simnet::app::make_snapshot_pipeline(loaded);
    CHECK(simnet::has_all_flags(pipeline.techniques, simnet::PipelineTechniqueFlags::Delta));
    CHECK(
        simnet::has_all_flags(pipeline.techniques, simnet::PipelineTechniqueFlags::DeltaFieldMask)
    );

    auto control = loaded;
    control.pipeline.enable_delta_field_mask = false;
    CHECK(
        simnet::fingerprint_network_compatibility(loaded).value !=
        simnet::fingerprint_network_compatibility(control).value
    );
    CHECK(
        simnet::fingerprint_runtime_config(loaded, simnet::default_server_config()).value !=
        simnet::fingerprint_runtime_config(control, simnet::default_server_config()).value
    );
    CHECK(
        simnet::pipeline_decode_signature(pipeline) !=
        simnet::pipeline_decode_signature(simnet::app::make_snapshot_pipeline(control))
    );

    for (auto const contents : {
             R"({ "pipeline": { "enable_delta_field_mask": true } })",
             R"({ "pipeline": { "enable_delta_field_mask": 1 } })",
             R"({ "pipeline": { "enable_delta_field_mask": "true" } })",
         })
    {
        auto const invalid = TemporaryConfig{
            "simnet_pipeline_delta_field_mask_invalid.json",
            contents,
        };
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }
}

TEST_CASE("maintained representation and cadence profiles are matched", "[config][pipeline]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto raw =
        simnet::load_shared_config(directory / "shared_representation_raw_aoi_radius_visual.json");
    auto quantized = simnet::load_shared_config(
        directory / "shared_representation_quantized_aoi_radius_visual.json"
    );
    auto oct = simnet::load_shared_config(
        directory / "shared_representation_oct_heading_aoi_radius_visual.json"
    );
    auto bit_packed = simnet::load_shared_config(
        directory / "shared_representation_bit_packed_aoi_radius_visual.json"
    );
    auto cadence =
        simnet::load_shared_config(directory / "shared_cadence_reduced_aoi_radius_visual.json");

    CHECK(raw.simulation.initial_boid_count == 1500U);
    CHECK(raw.pipeline.area_of_interest.mode == "radius");
    CHECK(raw.pipeline.area_of_interest.radius == 80.0F);
    CHECK(raw.packetization.max_payload_bytes == 1200U);
    CHECK(raw.pipeline.send_interval_ticks == 1U);
    CHECK_FALSE(raw.pipeline.enable_quantization);
    CHECK(quantized.pipeline.enable_quantization);
    CHECK_FALSE(quantized.pipeline.enable_oct_heading);
    CHECK(oct.pipeline.enable_quantization);
    CHECK(oct.pipeline.enable_oct_heading);
    CHECK_FALSE(oct.pipeline.enable_bit_packing);
    CHECK(bit_packed.pipeline.enable_bit_packing);
    CHECK(cadence.pipeline.send_interval_ticks == 4U);
    CHECK_FALSE(cadence.pipeline.enable_quantization);

    auto quantized_control = quantized;
    quantized_control.pipeline.enable_quantization = false;
    CHECK(
        simnet::fingerprint_network_compatibility(quantized_control).value ==
        simnet::fingerprint_network_compatibility(raw).value
    );
    auto oct_control = oct;
    oct_control.pipeline.enable_oct_heading = false;
    CHECK(
        simnet::fingerprint_network_compatibility(oct_control).value ==
        simnet::fingerprint_network_compatibility(quantized).value
    );
    auto bit_packed_control = bit_packed;
    bit_packed_control.pipeline.enable_bit_packing = false;
    CHECK(
        simnet::fingerprint_network_compatibility(bit_packed_control).value ==
        simnet::fingerprint_network_compatibility(oct).value
    );
    auto cadence_control = cadence;
    cadence_control.pipeline.send_interval_ticks = 1U;
    CHECK(
        simnet::fingerprint_network_compatibility(cadence_control).value ==
        simnet::fingerprint_network_compatibility(raw).value
    );

    auto normalize = [](simnet::SharedConfig& config)
    {
        config.pipeline.send_interval_ticks = 1U;
        config.pipeline.enable_quantization = false;
        config.pipeline.enable_oct_heading = false;
        config.pipeline.enable_bit_packing = false;
    };
    normalize(raw);
    normalize(quantized);
    normalize(oct);
    normalize(bit_packed);
    normalize(cadence);
    auto const expected = simnet::fingerprint_network_compatibility(raw).value;
    CHECK(simnet::fingerprint_network_compatibility(quantized).value == expected);
    CHECK(simnet::fingerprint_network_compatibility(oct).value == expected);
    CHECK(simnet::fingerprint_network_compatibility(bit_packed).value == expected);
    CHECK(simnet::fingerprint_network_compatibility(cadence).value == expected);
}

TEST_CASE("every maintained JSON profile parses through its production loader", "[config]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    for (auto const& entry : std::filesystem::directory_iterator{directory})
    {
        auto const& path = entry.path();
        if (!entry.is_regular_file() || path.extension() != ".json")
        {
            continue;
        }
        auto const name = path.filename().string();
        if (name.starts_with("shared_"))
        {
            REQUIRE_NOTHROW(simnet::load_shared_config(path));
        }
        else if (name.starts_with("server_"))
        {
            REQUIRE_NOTHROW(simnet::load_server_config(path));
        }
        else if (name.starts_with("client_"))
        {
            REQUIRE_NOTHROW(simnet::load_client_config(path));
        }
        else
        {
            FAIL("unowned maintained JSON profile: " << name);
        }
    }
}

TEST_CASE("field-mask Delta profiles are a matched pair", "[config][pipeline][profile]")
{
    auto const directory = maintained_config_directory();
    auto control =
        simnet::load_shared_config(directory / "shared_delta_whole_record_aoi_radius_visual.json");
    auto treatment =
        simnet::load_shared_config(directory / "shared_delta_field_mask_aoi_radius_visual.json");
    CHECK_FALSE(control.pipeline.enable_delta_field_mask);
    CHECK(treatment.pipeline.enable_delta_field_mask);
    CHECK(control.pipeline.enable_delta);
    CHECK(treatment.pipeline.enable_delta);
    CHECK_FALSE(control.pipeline.enable_incremental);
    CHECK(control.pipeline.level_of_detail.mode == "none");
    CHECK_FALSE(control.pipeline.enable_quantization);
    CHECK_FALSE(control.pipeline.enable_oct_heading);
    CHECK_FALSE(control.pipeline.enable_bit_packing);
    CHECK(control.compression.mode == "none");
    CHECK(control.snapshot_delivery.mode == "reliable_sequenced");
    CHECK(control.pipeline.area_of_interest.mode == "radius");
    treatment.pipeline.enable_delta_field_mask = false;
    check_shared_equal(control, treatment);
}

TEST_CASE("synthetic workload configuration is strict and fingerprinted", "[config][synthetic]")
{
    auto const defaults = simnet::default_shared_config();
    REQUIRE_FALSE(defaults.synthetic.has_value());
    auto const legacy_fingerprint = simnet::fingerprint_network_compatibility(defaults);

    auto const accepted = TemporaryConfig{
        "simnet_synthetic_accepted.json",
        R"({
            "synthetic": {
                "pattern": "grid",
                "entity_change_fraction": 0.125,
                "field_change_mode": "position_only"
            }
        })"
    };
    auto const loaded = simnet::load_shared_config(accepted.path());
    REQUIRE(loaded.synthetic.has_value());
    CHECK(loaded.synthetic->pattern == "grid");
    CHECK(loaded.synthetic->entity_change_fraction == 0.125);
    CHECK(loaded.synthetic->field_change_mode == "position_only");
    CHECK(simnet::fingerprint_network_compatibility(loaded).value != legacy_fingerprint.value);

    auto changed = loaded;
    changed.synthetic->pattern = "random_uniform";
    CHECK(
        simnet::fingerprint_network_compatibility(changed).value !=
        simnet::fingerprint_network_compatibility(loaded).value
    );
    changed = loaded;
    changed.synthetic->entity_change_fraction = 0.25;
    CHECK(
        simnet::fingerprint_network_compatibility(changed).value !=
        simnet::fingerprint_network_compatibility(loaded).value
    );
    changed = loaded;
    changed.synthetic->field_change_mode = "heading_only";
    CHECK(
        simnet::fingerprint_network_compatibility(changed).value !=
        simnet::fingerprint_network_compatibility(loaded).value
    );

    for (
        auto const contents : {
            R"({ "synthetic": {} })",
            R"({ "synthetic": { "pattern": "grid", "entity_change_fraction": 1.0 } })",
            R"({ "synthetic": { "pattern": "unknown", "entity_change_fraction": 1.0, "field_change_mode": "all" } })",
            R"({ "synthetic": { "pattern": "grid", "entity_change_fraction": -0.01, "field_change_mode": "all" } })",
            R"({ "synthetic": { "pattern": "grid", "entity_change_fraction": 1.01, "field_change_mode": "all" } })",
            R"({ "synthetic": { "pattern": "grid", "entity_change_fraction": "1", "field_change_mode": "all" } })",
            R"({ "synthetic": { "pattern": "grid", "entity_change_fraction": 1.0, "field_change_mode": "unknown" } })",
            R"({ "synthetic": { "pattern": "grid", "entity_change_fraction": 1.0, "field_change_mode": "all", "extra": true } })",
        })
    {
        auto const invalid = TemporaryConfig{"simnet_synthetic_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }
}

TEST_CASE("synthetic Delta profiles are matched treatments", "[config][synthetic][profile]")
{
    auto const directory = maintained_config_directory();
    auto full = simnet::load_shared_config(directory / "shared_synthetic_delta_full_change.json");
    auto sparse_entities =
        simnet::load_shared_config(directory / "shared_synthetic_delta_sparse_entities.json");
    auto sparse_fields_whole = simnet::load_shared_config(
        directory / "shared_synthetic_delta_sparse_fields_whole_record.json"
    );
    auto sparse_fields_mask = simnet::load_shared_config(
        directory / "shared_synthetic_delta_sparse_fields_field_mask.json"
    );

    REQUIRE(full.synthetic.has_value());
    REQUIRE(sparse_entities.synthetic.has_value());
    REQUIRE(sparse_fields_whole.synthetic.has_value());
    REQUIRE(sparse_fields_mask.synthetic.has_value());
    CHECK(full.synthetic->entity_change_fraction == 1.0);
    CHECK(full.synthetic->field_change_mode == "all");
    CHECK(sparse_entities.synthetic->entity_change_fraction == 0.125);
    CHECK(sparse_entities.synthetic->field_change_mode == "all");
    CHECK(sparse_fields_whole.synthetic->entity_change_fraction == 0.125);
    CHECK(sparse_fields_whole.synthetic->field_change_mode == "position_only");
    CHECK_FALSE(sparse_fields_whole.pipeline.enable_delta_field_mask);
    CHECK(sparse_fields_mask.pipeline.enable_delta_field_mask);

    sparse_entities.synthetic = full.synthetic;
    check_shared_equal(full, sparse_entities);
    sparse_fields_whole.synthetic = full.synthetic;
    check_shared_equal(full, sparse_fields_whole);
    sparse_fields_mask.synthetic = full.synthetic;
    sparse_fields_mask.pipeline.enable_delta_field_mask = false;
    check_shared_equal(full, sparse_fields_mask);
}

TEST_CASE("typed defaults equal shipped default profiles", "[config][defaults]")
{
    auto const directory = maintained_config_directory();
    auto const typed_shared = simnet::default_shared_config();
    auto const typed_server = simnet::default_server_config();
    auto const typed_client = simnet::default_client_config();
    auto const shipped_shared = simnet::load_shared_config(directory / "shared_default.json");
    auto const shipped_server = simnet::load_server_config(directory / "server_default.json");
    auto const shipped_client = simnet::load_client_config(directory / "client_default.json");

    check_shared_equal(typed_shared, shipped_shared);
    check_server_equal(typed_server, shipped_server);
    check_client_equal(typed_client, shipped_client);
    CHECK(
        simnet::fingerprint_network_compatibility(typed_shared).value ==
        simnet::fingerprint_network_compatibility(shipped_shared).value
    );
    CHECK(
        simnet::fingerprint_runtime_config(typed_shared, typed_server).value ==
        simnet::fingerprint_runtime_config(shipped_shared, shipped_server).value
    );
    CHECK(
        simnet::fingerprint_runtime_config(typed_shared, typed_client).value ==
        simnet::fingerprint_runtime_config(shipped_shared, shipped_client).value
    );

    auto const reloaded_shared = simnet::load_shared_config(directory / "shared_default.json");
    auto const reloaded_server = simnet::load_server_config(directory / "server_default.json");
    auto const reloaded_client = simnet::load_client_config(directory / "client_default.json");
    check_shared_equal(shipped_shared, reloaded_shared);
    check_server_equal(shipped_server, reloaded_server);
    check_client_equal(shipped_client, reloaded_client);
}

TEST_CASE("maintained profile fingerprints preserve normalized semantics", "[config][defaults]")
{
    struct SharedFingerprint
    {
        std::string_view name;
        std::uint64_t network;
    };
    constexpr auto shared_fingerprints = std::array{
        SharedFingerprint{"shared_default.json", 14551407725952482035ULL},
        SharedFingerprint{"shared_demo_network.json", 14688557305349597786ULL},
        SharedFingerprint{"shared_demo_visual.json", 10153676311785215047ULL},
        SharedFingerprint{"shared_stress_50k.json", 2148465817834599327ULL},
        SharedFingerprint{"shared_aoi_radius_visual.json", 147685930129622393ULL},
        SharedFingerprint{"shared_aoi_fov_visual.json", 16733286516763073780ULL},
        SharedFingerprint{"shared_packetization_aoi_radius_visual.json", 5820348207436308927ULL},
        SharedFingerprint{
            "shared_compression_whole_aoi_radius_visual.json",
            5775175471029376935ULL
        },
        SharedFingerprint{
            "shared_compression_per_packet_aoi_radius_visual.json",
            12403191858096298200ULL
        },
        SharedFingerprint{
            "shared_compression_none_aoi_radius_visual.json",
            13258101794867737506ULL
        },
        SharedFingerprint{
            "shared_compression_zstd_delta_field_mask_aoi_radius_visual.json",
            11761613649863971200ULL
        },
        SharedFingerprint{
            "shared_compression_zstd_pipeline_v1_delta_field_mask_aoi_radius_visual.json",
            16930381709287303858ULL
        },
        SharedFingerprint{
            "shared_delivery_reliable_aoi_radius_visual.json",
            11683916058568800636ULL
        },
        SharedFingerprint{
            "shared_delivery_unreliable_aoi_radius_visual.json",
            11634272762312594509ULL
        },
        SharedFingerprint{"shared_lod_none_aoi_radius_visual.json", 13872606770787437061ULL},
        SharedFingerprint{
            "shared_lod_distance_bands_aoi_radius_visual.json",
            14877914366436949275ULL
        },
        SharedFingerprint{"shared_player_influence_control_visual.json", 1319605066963474642ULL},
        SharedFingerprint{"shared_player_lure_visual.json", 15773174323224214929ULL},
        SharedFingerprint{"shared_player_predator_visual.json", 6162015880269545229ULL},
        SharedFingerprint{
            "shared_representation_raw_aoi_radius_visual.json",
            147685930129622393ULL
        },
        SharedFingerprint{
            "shared_representation_quantized_aoi_radius_visual.json",
            17808255628541615226ULL
        },
        SharedFingerprint{
            "shared_representation_oct_heading_aoi_radius_visual.json",
            11104672302595821387ULL
        },
        SharedFingerprint{
            "shared_representation_bit_packed_aoi_radius_visual.json",
            14776041096246557698ULL
        },
        SharedFingerprint{"shared_cadence_reduced_aoi_radius_visual.json", 666291167247402478ULL},
        SharedFingerprint{"shared_synthetic_delta_full_change.json", 11621082009713136448ULL},
        SharedFingerprint{"shared_synthetic_delta_sparse_entities.json", 11346589131469884240ULL},
        SharedFingerprint{
            "shared_synthetic_delta_sparse_fields_whole_record.json",
            2082947159746639717ULL
        },
        SharedFingerprint{
            "shared_synthetic_delta_sparse_fields_field_mask.json",
            9470061669881578492ULL
        },
    };

    auto const directory = maintained_config_directory();
    for (auto const& expected : shared_fingerprints)
    {
        CAPTURE(expected.name);
        auto const config = simnet::load_shared_config(directory / expected.name);
        CHECK(simnet::fingerprint_network_compatibility(config).value == expected.network);
    }
}

TEST_CASE("every maintained JSON file loads through its production loader", "[config][profiles]")
{
    auto loaded_count = std::size_t{};
    for (auto const& entry : std::filesystem::directory_iterator{maintained_config_directory()})
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        auto const name = entry.path().filename().string();
        CAPTURE(name);
        if (name.starts_with("shared_"))
        {
            static_cast<void>(simnet::load_shared_config(entry.path()));
        }
        else if (name.starts_with("server_"))
        {
            static_cast<void>(simnet::load_server_config(entry.path()));
        }
        else if (name.starts_with("client_"))
        {
            static_cast<void>(simnet::load_client_config(entry.path()));
        }
        else
        {
            FAIL("maintained JSON filename has no production loader owner");
        }
        ++loaded_count;
    }
    CHECK(loaded_count == 36U);
}

TEST_CASE("default alignment preserves inherited treatment tuning", "[config][defaults]")
{
    auto const directory = maintained_config_directory();
    for (auto const name : {
             "shared_delivery_reliable_aoi_radius_visual.json",
             "shared_delivery_unreliable_aoi_radius_visual.json",
             "shared_lod_none_aoi_radius_visual.json",
             "shared_lod_distance_bands_aoi_radius_visual.json",
         })
    {
        CAPTURE(name);
        auto const config = simnet::load_shared_config(directory / name);
        CHECK(config.boids.alignment_radius == 18.0F);
        CHECK(config.boids.cohesion_radius == 18.0F);
        CHECK(config.boids.alignment_acceleration == 3.0F);
        CHECK(config.boids.wander_acceleration == 0.35F);
        CHECK(config.boids.hue_assimilation_rate == 0.25F);
        CHECK(config.boids.hue_drift_rate == 0.02F);
    }

    CHECK(simnet::load_server_config(directory / "server_visual.json").transport.max_clients == 1U);
    CHECK(
        simnet::load_server_config(directory / "server_multi_client_visual.json")
            .transport.max_clients == 2U
    );
    auto observer = simnet::load_client_config(directory / "client_visual.json");
    auto player = simnet::load_client_config(directory / "client_player_visual.json");
    CHECK(observer.transport.max_clients == 1U);
    CHECK(player.transport.max_clients == 1U);
    CHECK(observer.gameplay.role == "stationary_observer");
    CHECK(player.gameplay.role == "player");
    player.gameplay = observer.gameplay;
    CHECK(
        simnet::fingerprint_runtime_config(simnet::default_shared_config(), player).value ==
        simnet::fingerprint_runtime_config(simnet::default_shared_config(), observer).value
    );
}

TEST_CASE("snapshot delivery configuration is strict shared treatment", "[config][delivery]")
{
    auto const accepted = TemporaryConfig{
        "simnet_delivery_accepted.json",
        R"({
            "snapshot_delivery": {
                "mode": "unreliable_sequenced",
                "full_replace_after_unacknowledged_updates": 32
            }
        })"
    };
    auto const loaded = simnet::load_shared_config(accepted.path());
    CHECK(loaded.snapshot_delivery.mode == "unreliable_sequenced");
    CHECK(loaded.snapshot_delivery.full_replace_after_unacknowledged_updates == 32U);

    for (
        auto const contents : {
            R"({ "snapshot_delivery": { "mode": "unreliable", "full_replace_after_unacknowledged_updates": 32 } })",
            R"({ "snapshot_delivery": { "mode": "reliable_sequenced" } })",
            R"({ "snapshot_delivery": { "mode": "reliable_sequenced", "full_replace_after_unacknowledged_updates": 0 } })",
            R"({ "snapshot_delivery": { "mode": "reliable_sequenced", "full_replace_after_unacknowledged_updates": 64 } })",
            R"({ "snapshot_delivery": { "mode": "reliable_sequenced", "full_replace_after_unacknowledged_updates": 32, "extra": true } })",
        })
    {
        auto const invalid = TemporaryConfig{"simnet_delivery_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }
}

TEST_CASE("maintained delivery treatments differ only by mode", "[config][delivery]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto reliable =
        simnet::load_shared_config(directory / "shared_delivery_reliable_aoi_radius_visual.json");
    auto unreliable =
        simnet::load_shared_config(directory / "shared_delivery_unreliable_aoi_radius_visual.json");
    CHECK(reliable.snapshot_delivery.mode == "reliable_sequenced");
    CHECK(unreliable.snapshot_delivery.mode == "unreliable_sequenced");
    unreliable.snapshot_delivery.mode = reliable.snapshot_delivery.mode;
    CHECK(
        simnet::fingerprint_network_compatibility(unreliable).value ==
        simnet::fingerprint_network_compatibility(reliable).value
    );
}

TEST_CASE("visual interpolation is local runtime configuration", "[config]")
{
    auto const shared = simnet::default_shared_config();
    auto const baseline = simnet::default_server_config();
    auto changed = baseline;
    changed.visualization.interpolation_enabled = !changed.visualization.interpolation_enabled;

    CHECK(
        simnet::fingerprint_runtime_config(shared, changed).value !=
        simnet::fingerprint_runtime_config(shared, baseline).value
    );
}

TEST_CASE("transport client capacity is strictly bounded", "[config][peer]")
{
    for (auto const capacity : {1U, 64U})
    {
        auto const accepted = TemporaryConfig{
            "simnet_client_capacity_accepted_" + std::to_string(capacity) + ".json",
            "{ \"transport\": { \"max_clients\": " + std::to_string(capacity) + " } }"
        };
        CHECK(simnet::load_server_config(accepted.path()).transport.max_clients == capacity);
    }
    for (auto const capacity : {0U, 65U})
    {
        auto const rejected = TemporaryConfig{
            "simnet_client_capacity_rejected_" + std::to_string(capacity) + ".json",
            "{ \"transport\": { \"max_clients\": " + std::to_string(capacity) + " } }"
        };
        CHECK_THROWS(simnet::load_server_config(rejected.path()));
    }
}

TEST_CASE("multi-client visual profile changes only Server capacity", "[config][peer]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto control = simnet::load_server_config(directory / "server_visual.json");
    auto treatment = simnet::load_server_config(directory / "server_multi_client_visual.json");
    CHECK(control.transport.max_clients == 1U);
    CHECK(treatment.transport.max_clients == 2U);
    treatment.transport.max_clients = control.transport.max_clients;
    CHECK(
        simnet::fingerprint_runtime_config(simnet::default_shared_config(), treatment).value ==
        simnet::fingerprint_runtime_config(simnet::default_shared_config(), control).value
    );
}

TEST_CASE("client gameplay role is local runtime configuration", "[config][player]")
{
    auto const shared = simnet::default_shared_config();
    auto const stationary_observer_config = simnet::default_client_config();
    auto player = stationary_observer_config;
    player.gameplay.role = "player";

    CHECK(
        simnet::fingerprint_runtime_config(shared, player).value !=
        simnet::fingerprint_runtime_config(shared, stationary_observer_config).value
    );
}

TEST_CASE("player pitch limit stays clear of the vertical camera singularity", "[config][player]")
{
    auto const accepted = TemporaryConfig{
        "simnet_player_pitch_accepted.json",
        R"({ "player": { "pitch_limit_degrees": 85.0 } })"
    };
    CHECK(simnet::load_shared_config(accepted.path()).player.pitch_limit_degrees == 85.0F);

    auto const rejected = TemporaryConfig{
        "simnet_player_pitch_rejected.json",
        R"({ "player": { "pitch_limit_degrees": 85.1 } })"
    };
    CHECK_THROWS(simnet::load_shared_config(rejected.path()));
}

TEST_CASE("Player influence configuration is strict bounded and fingerprinted", "[config][player]")
{
    auto const accepted = TemporaryConfig{
        "simnet_player_influence_accepted.json",
        R"({
            "simulation": { "world_half": 20.0 },
            "boids": {
                "max_acceleration": 12.0,
                "player_lure": {
                    "enabled": true,
                    "radius": 35.0,
                    "max_acceleration": 5.0
                },
                "player_predator": {
                    "enabled": true,
                    "radius": 24.0,
                    "max_acceleration": 10.0
                }
            }
        })"
    };
    auto const loaded = simnet::load_shared_config(accepted.path());
    CHECK(loaded.boids.player_lure.enabled);
    CHECK(loaded.boids.player_lure.radius == 35.0F);
    CHECK(loaded.boids.player_lure.max_acceleration == 5.0F);
    CHECK(loaded.boids.player_predator.enabled);
    CHECK(loaded.boids.player_predator.radius == 24.0F);
    CHECK(loaded.boids.player_predator.max_acceleration == 10.0F);

    for (
        auto const contents : {
            R"({ "boids": { "player_lure": {} } })",
            R"({ "boids": { "player_lure": { "enabled": false, "radius": 1.0 } } })",
            R"({ "boids": { "player_lure": { "enabled": true, "radius": 1.0 } } })",
            R"({ "boids": { "player_lure": { "enabled": true, "radius": 0.0, "max_acceleration": 1.0 } } })",
            R"({ "boids": { "player_lure": { "enabled": true, "radius": 1.0, "max_acceleration": -1.0 } } })",
            R"({ "boids": { "player_lure": { "enabled": true, "radius": 1e999, "max_acceleration": 1.0 } } })",
            R"({ "boids": { "player_lure": { "enabled": true, "radius": 801.0, "max_acceleration": 1.0 } } })",
            R"({ "boids": { "player_predator": { "enabled": true, "radius": 1.0, "max_acceleration": 13.0 } } })",
            R"({ "boids": { "player_predator": { "enabled": false, "extra": true } } })",
        })
    {
        auto const invalid = TemporaryConfig{"simnet_player_influence_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }

    auto const baseline = simnet::default_shared_config();
    auto treatment = baseline;
    treatment.boids.player_lure = {
        .enabled = true,
        .radius = 35.0F,
        .max_acceleration = 5.0F,
    };
    treatment.boids.player_predator = {
        .enabled = true,
        .radius = 24.0F,
        .max_acceleration = 10.0F,
    };
    auto const compatibility = simnet::fingerprint_network_compatibility(treatment);
    auto const runtime =
        simnet::fingerprint_runtime_config(treatment, simnet::default_server_config());
    auto check_changed = [&](simnet::SharedConfig const& changed)
    {
        CHECK(simnet::fingerprint_network_compatibility(changed).value != compatibility.value);
        CHECK(
            simnet::fingerprint_runtime_config(changed, simnet::default_server_config()).value !=
            runtime.value
        );
    };
    auto changed = treatment;
    changed.boids.player_lure.enabled = false;
    check_changed(changed);
    changed = treatment;
    changed.boids.player_lure.radius += 1.0F;
    check_changed(changed);
    changed = treatment;
    changed.boids.player_lure.max_acceleration += 1.0F;
    check_changed(changed);
    changed = treatment;
    changed.boids.player_predator.enabled = false;
    check_changed(changed);
    changed = treatment;
    changed.boids.player_predator.radius += 1.0F;
    check_changed(changed);
    changed = treatment;
    changed.boids.player_predator.max_acceleration += 1.0F;
    check_changed(changed);
}

TEST_CASE("maintained Player influence treatments match their control", "[config][player]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto control =
        simnet::load_shared_config(directory / "shared_player_influence_control_visual.json");
    auto lure = simnet::load_shared_config(directory / "shared_player_lure_visual.json");
    auto predator = simnet::load_shared_config(directory / "shared_player_predator_visual.json");
    CHECK(control.simulation.initial_boid_count == 300U);
    CHECK(control.simulation.world_half == 60.0F);
    CHECK(control.spatial.cell_size == 10.0F);
    CHECK_FALSE(control.boids.player_lure.enabled);
    CHECK_FALSE(control.boids.player_predator.enabled);
    CHECK(lure.boids.player_lure.radius == 35.0F);
    CHECK(lure.boids.player_lure.max_acceleration == 5.0F);
    CHECK(predator.boids.player_predator.radius == 24.0F);
    CHECK(predator.boids.player_predator.max_acceleration == 10.0F);

    control.boids.player_lure = {};
    control.boids.player_predator = {};
    lure.boids.player_lure = {};
    lure.boids.player_predator = {};
    predator.boids.player_lure = {};
    predator.boids.player_predator = {};
    auto const fingerprint = simnet::fingerprint_network_compatibility(control);
    CHECK(simnet::fingerprint_network_compatibility(lure).value == fingerprint.value);
    CHECK(simnet::fingerprint_network_compatibility(predator).value == fingerprint.value);
}

TEST_CASE("AOI configuration is mode-specific strict and fingerprinted", "[config][aoi]")
{
    auto const radius = TemporaryConfig{
        "simnet_aoi_radius.json",
        R"({ "pipeline": { "area_of_interest": { "mode": "radius", "radius": 80.0 } } })"
    };
    auto const radius_config = simnet::load_shared_config(radius.path());
    CHECK(radius_config.pipeline.area_of_interest.mode == "radius");
    CHECK(radius_config.pipeline.area_of_interest.radius == 80.0F);
    CHECK(
        simnet::fingerprint_network_compatibility(radius_config).value !=
        simnet::fingerprint_network_compatibility(simnet::default_shared_config()).value
    );

    auto const fov = TemporaryConfig{
        "simnet_aoi_fov.json",
        R"({ "pipeline": { "area_of_interest": { "mode": "fov", "radius": 80.0, "fov_degrees": 120.0 } } })"
    };
    auto const fov_config = simnet::load_shared_config(fov.path());
    CHECK(fov_config.pipeline.area_of_interest.mode == "fov");
    CHECK(fov_config.pipeline.area_of_interest.fov_degrees == 120.0F);

    for (
        auto const contents : {
            R"({ "pipeline": { "area_of_interest": { "mode": "none", "radius": 1.0 } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "radius" } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "radius", "radius": 1.0, "fov_degrees": 90.0 } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "fov", "radius": 1.0, "fov_degrees": 0.0 } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "fov", "radius": 1.0, "fov_degrees": 180.1 } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "radius", "radius": 1.0, "unexpected": true } } })",
        })
    {
        auto const invalid = TemporaryConfig{"simnet_aoi_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }
}

TEST_CASE("maintained AOI visual profiles load as distinct treatments", "[config][aoi]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto const radius = simnet::load_shared_config(directory / "shared_aoi_radius_visual.json");
    auto fov = simnet::load_shared_config(directory / "shared_aoi_fov_visual.json");
    CHECK(radius.pipeline.area_of_interest.mode == "radius");
    CHECK(radius.pipeline.area_of_interest.radius == 80.0F);
    CHECK(fov.pipeline.area_of_interest.mode == "fov");
    CHECK(fov.pipeline.area_of_interest.radius == 80.0F);
    CHECK(fov.pipeline.area_of_interest.fov_degrees == 120.0F);
    fov.pipeline.area_of_interest = radius.pipeline.area_of_interest;
    CHECK(
        simnet::fingerprint_network_compatibility(fov).value ==
        simnet::fingerprint_network_compatibility(radius).value
    );
}

TEST_CASE("level-of-detail configuration is strict shared treatment", "[config][lod]")
{
    auto const accepted = TemporaryConfig{
        "simnet_lod_accepted.json",
        R"({
            "pipeline": {
                "area_of_interest": { "mode": "radius", "radius": 160.0 },
                "level_of_detail": {
                    "mode": "distance_bands",
                    "near_distance": 40.0,
                    "medium_distance": 100.0,
                    "medium_interval_ticks": 4,
                    "far_interval_ticks": 16
                }
            }
        })"
    };
    auto const loaded = simnet::load_shared_config(accepted.path());
    CHECK(loaded.pipeline.level_of_detail.mode == "distance_bands");
    CHECK(loaded.pipeline.level_of_detail.near_distance == 40.0F);
    CHECK(loaded.pipeline.level_of_detail.medium_distance == 100.0F);
    CHECK(loaded.pipeline.level_of_detail.medium_interval_ticks == 4U);
    CHECK(loaded.pipeline.level_of_detail.far_interval_ticks == 16U);
    CHECK_FALSE(loaded.pipeline.enable_incremental);
    CHECK(
        simnet::fingerprint_network_compatibility(loaded).value !=
        simnet::fingerprint_network_compatibility(simnet::default_shared_config()).value
    );
    auto changed = loaded;
    changed.pipeline.level_of_detail.near_distance += 1.0F;
    CHECK(
        simnet::fingerprint_network_compatibility(changed).value !=
        simnet::fingerprint_network_compatibility(loaded).value
    );
    CHECK(
        simnet::fingerprint_runtime_config(changed, simnet::default_server_config()).value !=
        simnet::fingerprint_runtime_config(loaded, simnet::default_server_config()).value
    );
    changed = loaded;
    changed.pipeline.level_of_detail.medium_distance += 1.0F;
    CHECK(
        simnet::fingerprint_network_compatibility(changed).value !=
        simnet::fingerprint_network_compatibility(loaded).value
    );
    changed = loaded;
    ++changed.pipeline.level_of_detail.medium_interval_ticks;
    CHECK(
        simnet::fingerprint_network_compatibility(changed).value !=
        simnet::fingerprint_network_compatibility(loaded).value
    );
    changed = loaded;
    ++changed.pipeline.level_of_detail.far_interval_ticks;
    CHECK(
        simnet::fingerprint_network_compatibility(changed).value !=
        simnet::fingerprint_network_compatibility(loaded).value
    );

    for (
        auto const contents : {
            R"({ "pipeline": { "level_of_detail": { "mode": "none", "near_distance": 1.0 } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "radius", "radius": 160.0 }, "level_of_detail": { "mode": "distance_bands", "near_distance": 40.0, "medium_distance": 100.0, "medium_interval_ticks": 4 } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "radius", "radius": 160.0 }, "level_of_detail": { "mode": "distance", "near_distance": 40.0, "medium_distance": 100.0, "medium_interval_ticks": 4, "far_interval_ticks": 16 } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "radius", "radius": 160.0 }, "level_of_detail": { "mode": "distance_bands", "near_distance": 100.0, "medium_distance": 40.0, "medium_interval_ticks": 4, "far_interval_ticks": 16 } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "radius", "radius": 100.0 }, "level_of_detail": { "mode": "distance_bands", "near_distance": 40.0, "medium_distance": 100.0, "medium_interval_ticks": 4, "far_interval_ticks": 16 } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "radius", "radius": 160.0 }, "level_of_detail": { "mode": "distance_bands", "near_distance": 40.0, "medium_distance": 100.0, "medium_interval_ticks": 1, "far_interval_ticks": 16 } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "radius", "radius": 160.0 }, "level_of_detail": { "mode": "distance_bands", "near_distance": 40.0, "medium_distance": 100.0, "medium_interval_ticks": 4, "far_interval_ticks": 4 } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "radius", "radius": 160.0 }, "level_of_detail": { "mode": "distance_bands", "near_distance": 40.0, "medium_distance": 100.0, "medium_interval_ticks": 4, "far_interval_ticks": 65536 } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "radius", "radius": 160.0 }, "level_of_detail": { "mode": "distance_bands", "near_distance": 40.0, "medium_distance": 100.0, "medium_interval_ticks": 4, "far_interval_ticks": 16, "extra": true } } })",
            R"({ "pipeline": { "area_of_interest": { "mode": "radius", "radius": 160.0 }, "unexpected": true } })",
        })
    {
        auto const invalid = TemporaryConfig{"simnet_lod_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }
}

TEST_CASE("maintained LOD treatments differ only by level of detail", "[config][lod]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto none = simnet::load_shared_config(directory / "shared_lod_none_aoi_radius_visual.json");
    auto distance =
        simnet::load_shared_config(directory / "shared_lod_distance_bands_aoi_radius_visual.json");
    CHECK_FALSE(none.pipeline.enable_incremental);
    CHECK(none.pipeline.enable_delta);
    CHECK(distance.pipeline.level_of_detail.mode == "distance_bands");
    CHECK(distance.pipeline.level_of_detail.near_distance == 40.0F);
    CHECK(distance.pipeline.level_of_detail.medium_distance == 100.0F);
    CHECK(distance.pipeline.level_of_detail.medium_interval_ticks == 4U);
    CHECK(distance.pipeline.level_of_detail.far_interval_ticks == 16U);
    distance.pipeline.level_of_detail = none.pipeline.level_of_detail;
    CHECK(
        simnet::fingerprint_network_compatibility(distance).value ==
        simnet::fingerprint_network_compatibility(none).value
    );
}

TEST_CASE(
    "packetization configuration is strict bounded and fingerprinted",
    "[config][packetization]"
)
{
    auto const accepted = TemporaryConfig{
        "simnet_packetization_accepted.json",
        R"({
            "packetization": {
                "enabled": true,
                "max_payload_bytes": 256,
                "max_update_bytes": 524288,
                "max_chunks_per_update": 4096,
                "max_in_flight_updates": 4,
                "max_incomplete_bytes": 1048576,
                "reassembly_timeout_ms": 5000
            }
        })"
    };
    auto const loaded = simnet::load_shared_config(accepted.path());
    CHECK(loaded.packetization.enabled);
    CHECK(loaded.packetization.max_payload_bytes == 256U);
    CHECK(loaded.packetization.max_update_bytes == 524288U);
    CHECK(
        simnet::fingerprint_network_compatibility(loaded).value !=
        simnet::fingerprint_network_compatibility(simnet::default_shared_config()).value
    );

    for (auto const contents : {
             R"({ "packetization": { "enabled": true, "unexpected": 1 } })",
             R"({ "packetization": { "enabled": true, "max_payload_bytes": 25 } })",
             R"({ "packetization": { "enabled": true, "max_update_bytes": 4194305 } })",
             R"({ "packetization": { "enabled": true, "max_chunks_per_update": 4097 } })",
             R"({ "packetization": { "enabled": true, "max_in_flight_updates": 0 } })",
             R"({ "packetization": { "enabled": true, "max_incomplete_bytes": 1 } })",
             R"({ "packetization": { "enabled": true, "reassembly_timeout_ms": 0 } })",
         })
    {
        auto const invalid = TemporaryConfig{"simnet_packetization_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }
}

TEST_CASE("maintained forced packetization treatment loads", "[config][packetization][aoi]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto const config =
        simnet::load_shared_config(directory / "shared_packetization_aoi_radius_visual.json");
    CHECK(config.packetization.enabled);
    CHECK(config.packetization.max_payload_bytes == 256U);
    CHECK(config.pipeline.area_of_interest.mode == "radius");
    CHECK(config.pipeline.area_of_interest.radius == 160.0F);
}

TEST_CASE("compression configuration is strict and fingerprinted", "[config][compression]")
{
    auto const omitted_dictionary = TemporaryConfig{
        "simnet_compression_omitted_dictionary.json",
        R"({ "compression": { "mode": "whole_update", "level": 1 } })"
    };
    auto const loaded = simnet::load_shared_config(omitted_dictionary.path());
    CHECK(loaded.compression.mode == "whole_update");
    CHECK(loaded.compression.level == 1);
    CHECK(loaded.compression.dictionary == "none");

    auto const explicit_none = TemporaryConfig{
        "simnet_compression_explicit_none.json",
        R"({ "compression": { "mode": "whole_update", "level": 1, "dictionary": "none" } })"
    };
    auto const ordinary = simnet::load_shared_config(explicit_none.path());
    CHECK(ordinary.compression.dictionary == "none");
    CHECK(
        simnet::fingerprint_network_compatibility(ordinary).value ==
        simnet::fingerprint_network_compatibility(loaded).value
    );

    auto const selected_dictionary = TemporaryConfig{
        "simnet_compression_pipeline_v1.json",
        R"({ "compression": { "mode": "whole_update", "level": 1, "dictionary": "pipeline_v1" } })"
    };
    auto const dictionary = simnet::load_shared_config(selected_dictionary.path());
    CHECK(dictionary.compression.dictionary == "pipeline_v1");
    CHECK(
        simnet::fingerprint_network_compatibility(dictionary).value !=
        simnet::fingerprint_network_compatibility(ordinary).value
    );

    for (
        auto const contents : {
            R"({ "compression": { "mode": "none", "level": 1 } })",
            R"({ "compression": { "mode": "none", "dictionary": "none" } })",
            R"({ "compression": { "mode": "none", "dictionary": "pipeline_v1" } })",
            R"({ "compression": { "mode": "whole_update" } })",
            R"({ "compression": { "mode": "per_packet", "level": 0 } })",
            R"({ "compression": { "mode": "per_packet", "level": 20 } })",
            R"({ "compression": { "mode": "per_packet", "level": 1, "dictionary": "pipeline_v1" } })",
            R"({ "compression": { "mode": "whole_update", "level": 1, "dictionary": "unknown" } })",
            R"({ "compression": { "mode": "unknown", "level": 1 } })",
            R"({ "compression": { "mode": "whole_update", "level": 1, "extra": true } })",
        })
    {
        auto const invalid = TemporaryConfig{"simnet_compression_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }
}

TEST_CASE(
    "pipeline_v1 loading and session identity are fixed before transport",
    "[config][compression][dictionary][transport]"
)
{
    auto const directory = maintained_config_directory();
    auto const ordinary = simnet::load_shared_config(
        directory / "shared_compression_zstd_delta_field_mask_aoi_radius_visual.json"
    );
    auto const selected = simnet::load_shared_config(
        directory / "shared_compression_zstd_pipeline_v1_delta_field_mask_aoi_radius_visual.json"
    );
    auto const ordinary_settings = simnet::app::make_compression_settings(ordinary);
    auto const selected_settings = simnet::app::make_compression_settings(selected);
    CHECK_FALSE(simnet::app::load_compression_dictionary(ordinary_settings).has_value());
    CHECK_THROWS(
        simnet::app::load_compression_dictionary({
            .mode = simnet::app::CompressionMode::WholeUpdate,
            .level = 1,
            .dictionary = "unknown",
        })
    );
    CHECK_THROWS(
        simnet::app::load_compression_dictionary({
            .mode = simnet::app::CompressionMode::PerPacket,
            .level = 1,
            .dictionary = "pipeline_v1",
        })
    );
    auto loaded = simnet::app::load_compression_dictionary(selected_settings);
    REQUIRE(loaded.has_value());
    CHECK(loaded->name == "pipeline_v1");
    CHECK(loaded->dictionary.identity().dictionary_id == 0x534E0001U);
    CHECK(loaded->dictionary.identity().byte_count == 16384U);
    CHECK(loaded->dictionary.identity().content_fingerprint == 0x5fe43e7c3e7804a1ULL);

    auto const pipeline = simnet::app::make_snapshot_pipeline(selected);
    CHECK_THROWS(simnet::app::make_session_identity(selected, pipeline));
    CHECK_THROWS(
        simnet::app::make_session_identity(ordinary, pipeline, &loaded->dictionary.identity())
    );
    auto const ordinary_identity = simnet::app::make_session_identity(ordinary, pipeline);
    auto const selected_identity =
        simnet::app::make_session_identity(selected, pipeline, &loaded->dictionary.identity());
    auto mismatched = loaded->dictionary.identity();
    ++mismatched.content_fingerprint;
    auto const mismatched_identity =
        simnet::app::make_session_identity(selected, pipeline, &mismatched);
    CHECK(
        selected_identity.compatibility_fingerprint != ordinary_identity.compatibility_fingerprint
    );
    CHECK(
        selected_identity.compatibility_fingerprint != mismatched_identity.compatibility_fingerprint
    );
    CHECK(
        selected_identity.application_wire_fingerprint ==
        ordinary_identity.application_wire_fingerprint
    );
    CHECK(
        selected_identity.application_wire_fingerprint ==
        simnet::pipeline_decode_signature(pipeline)
    );
}

TEST_CASE(
    "matched dictionary profiles differ only by dictionary selection",
    "[config][compression][dictionary][profile]"
)
{
    auto const directory = maintained_config_directory();
    auto const control = simnet::load_shared_config(
        directory / "shared_compression_zstd_delta_field_mask_aoi_radius_visual.json"
    );
    auto treatment = simnet::load_shared_config(
        directory / "shared_compression_zstd_pipeline_v1_delta_field_mask_aoi_radius_visual.json"
    );
    CHECK(control.compression.dictionary == "none");
    CHECK(treatment.compression.dictionary == "pipeline_v1");
    treatment.compression.dictionary = control.compression.dictionary;
    check_shared_equal(control, treatment);
}

TEST_CASE("maintained compression treatments load with matching controls", "[config][compression]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto const none =
        simnet::load_shared_config(directory / "shared_compression_none_aoi_radius_visual.json");
    auto const whole =
        simnet::load_shared_config(directory / "shared_compression_whole_aoi_radius_visual.json");
    auto const per_packet = simnet::load_shared_config(
        directory / "shared_compression_per_packet_aoi_radius_visual.json"
    );
    CHECK(none.compression.mode == "none");
    CHECK(whole.compression.mode == "whole_update");
    CHECK(per_packet.compression.mode == "per_packet");
    CHECK(whole.compression.level == 1);
    CHECK(per_packet.compression.level == 1);
    CHECK(none.simulation.initial_boid_count == 1500U);
    CHECK(none.packetization.max_payload_bytes == 1200U);

    auto normalized_none = none;
    auto normalized_whole = whole;
    auto normalized_per_packet = per_packet;
    normalized_none.compression = {};
    normalized_whole.compression = {};
    normalized_per_packet.compression = {};
    auto const control_fingerprint = simnet::fingerprint_network_compatibility(normalized_none);
    CHECK(
        simnet::fingerprint_network_compatibility(normalized_whole).value ==
        control_fingerprint.value
    );
    CHECK(
        simnet::fingerprint_network_compatibility(normalized_per_packet).value ==
        control_fingerprint.value
    );
}
