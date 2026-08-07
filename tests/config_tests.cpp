#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>

import simnet.app_common;
import simnet.config;
import simnet.core;
import simnet.pipeline;
import simnet.snapshot;

namespace
{
    class TemporaryConfig
    {
    public:
        TemporaryConfig(std::string_view name, std::string_view contents)
            : path_(std::filesystem::temp_directory_path() / name)
        {
            auto file = std::ofstream{path_};
            file << contents;
        }

        ~TemporaryConfig()
        {
            std::error_code error{};
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
        simnet::pipeline_decode_signature(every_tick_pipeline)
        == simnet::pipeline_decode_signature(pipeline)
    );

    auto raw = defaults;
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
        simnet::pipeline_decode_signature(raw_pipeline)
        != simnet::pipeline_decode_signature(quantized_pipeline)
    );
    CHECK(
        simnet::pipeline_decode_signature(quantized_pipeline)
        != simnet::pipeline_decode_signature(oct_pipeline)
    );
    CHECK(
        simnet::pipeline_decode_signature(oct_pipeline)
        != simnet::pipeline_decode_signature(bit_packed_pipeline)
    );

    auto snapshot = simnet::WorldSnapshot{};
    snapshot.tick = 4U;
    snapshot.ids.push_back(1U);
    snapshot.classifications.push_back(simnet::EntityClassification{1U});
    snapshot.positions.push_back({1.25F, -2.5F, 3.75F});
    snapshot.headings.push_back(simnet::normalize_or({1.0F, 2.0F, 3.0F}, {.x = 1.0F}));
    snapshot.hues.push_back(42U);
    auto check_round_trip = [&](simnet::PipelineDefinition const& definition) {
        auto encode_state = simnet::ClientReplicationState{};
        auto decode_state = simnet::ClientReplicationState{};
        auto scratch = simnet::PipelineScratch{};
        auto const encoded
            = simnet::encode_snapshot(definition, encode_state, scratch, {.snapshot = &snapshot});
        REQUIRE(encoded.kind == simnet::EncodeResultKind::Update);
        auto const decoded
            = simnet::decode_update(definition, decode_state, {.bytes = encoded.update.bytes});
        REQUIRE(decoded.report.valid);
        REQUIRE(decoded.update.upserts.size() == 1U);
        CHECK(decoded.update.upserts.front().id == 1U);
    };
    check_round_trip(raw_pipeline);
    check_round_trip(quantized_pipeline);
    check_round_trip(oct_pipeline);
    check_round_trip(bit_packed_pipeline);

    auto const runtime_fingerprint
        = simnet::fingerprint_runtime_config(defaults, simnet::default_server_config());
    for (auto changed : {loaded, quantized, oct, bit_packed}) {
        CHECK(
            simnet::fingerprint_runtime_config(changed, simnet::default_server_config()).value
            != runtime_fingerprint.value
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
         }) {
        auto const invalid
            = TemporaryConfig{"simnet_pipeline_representation_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }

    auto const maximum_interval = TemporaryConfig{
        "simnet_pipeline_maximum_interval.json",
        R"({ "pipeline": { "send_interval_ticks": 4294967295 } })"
    };
    CHECK(
        simnet::load_shared_config(maximum_interval.path()).pipeline.send_interval_ticks
        == std::numeric_limits<std::uint32_t>::max()
    );
}

TEST_CASE("maintained representation and cadence profiles are matched", "[config][pipeline]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto raw = simnet::load_shared_config(
        directory / "shared_representation_raw_aoi_radius_visual.json"
    );
    auto quantized = simnet::load_shared_config(
        directory / "shared_representation_quantized_aoi_radius_visual.json"
    );
    auto oct = simnet::load_shared_config(
        directory / "shared_representation_oct_heading_aoi_radius_visual.json"
    );
    auto bit_packed = simnet::load_shared_config(
        directory / "shared_representation_bit_packed_aoi_radius_visual.json"
    );
    auto cadence
        = simnet::load_shared_config(directory / "shared_cadence_reduced_aoi_radius_visual.json");

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
        simnet::fingerprint_network_compatibility(quantized_control).value
        == simnet::fingerprint_network_compatibility(raw).value
    );
    auto oct_control = oct;
    oct_control.pipeline.enable_oct_heading = false;
    CHECK(
        simnet::fingerprint_network_compatibility(oct_control).value
        == simnet::fingerprint_network_compatibility(quantized).value
    );
    auto bit_packed_control = bit_packed;
    bit_packed_control.pipeline.enable_bit_packing = false;
    CHECK(
        simnet::fingerprint_network_compatibility(bit_packed_control).value
        == simnet::fingerprint_network_compatibility(oct).value
    );
    auto cadence_control = cadence;
    cadence_control.pipeline.send_interval_ticks = 1U;
    CHECK(
        simnet::fingerprint_network_compatibility(cadence_control).value
        == simnet::fingerprint_network_compatibility(raw).value
    );

    auto normalize = [](simnet::SharedConfig& config) {
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
    for (auto const& entry : std::filesystem::directory_iterator{directory}) {
        auto const path = entry.path();
        if (!entry.is_regular_file() || path.extension() != ".json") {
            continue;
        }
        auto const name = path.filename().string();
        if (name.starts_with("shared_")) {
            REQUIRE_NOTHROW(simnet::load_shared_config(path));
        } else if (name.starts_with("server_")) {
            REQUIRE_NOTHROW(simnet::load_server_config(path));
        } else if (name.starts_with("client_")) {
            REQUIRE_NOTHROW(simnet::load_client_config(path));
        } else {
            FAIL("unowned maintained JSON profile: " << name);
        }
    }
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
        }) {
        auto const invalid = TemporaryConfig{"simnet_delivery_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }

    auto const obsolete_local = TemporaryConfig{
        "simnet_delivery_obsolete_local.json",
        R"({ "transport": { "snapshot_delivery": "reliable_sequenced" } })"
    };
    CHECK_THROWS(simnet::load_server_config(obsolete_local.path()));
}

TEST_CASE("maintained delivery treatments differ only by mode", "[config][delivery]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto reliable
        = simnet::load_shared_config(directory / "shared_delivery_reliable_aoi_radius_visual.json");
    auto unreliable = simnet::load_shared_config(
        directory / "shared_delivery_unreliable_aoi_radius_visual.json"
    );
    CHECK(reliable.snapshot_delivery.mode == "reliable_sequenced");
    CHECK(unreliable.snapshot_delivery.mode == "unreliable_sequenced");
    unreliable.snapshot_delivery.mode = reliable.snapshot_delivery.mode;
    CHECK(
        simnet::fingerprint_network_compatibility(unreliable).value
        == simnet::fingerprint_network_compatibility(reliable).value
    );
}

TEST_CASE("visual interpolation is local runtime configuration", "[config]")
{
    auto const shared = simnet::default_shared_config();
    auto const baseline = simnet::default_server_config();
    auto changed = baseline;
    changed.visualization.interpolation_enabled = !changed.visualization.interpolation_enabled;

    CHECK(
        simnet::fingerprint_runtime_config(shared, changed).value
        != simnet::fingerprint_runtime_config(shared, baseline).value
    );
}

TEST_CASE("transport client capacity is strictly bounded", "[config][peer]")
{
    for (auto const capacity : {1U, 64U}) {
        auto const accepted = TemporaryConfig{
            "simnet_client_capacity_accepted_" + std::to_string(capacity) + ".json",
            "{ \"transport\": { \"max_clients\": " + std::to_string(capacity) + " } }"
        };
        CHECK(simnet::load_server_config(accepted.path()).transport.max_clients == capacity);
    }
    for (auto const capacity : {0U, 65U}) {
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
        simnet::fingerprint_runtime_config(simnet::default_shared_config(), treatment).value
        == simnet::fingerprint_runtime_config(simnet::default_shared_config(), control).value
    );
}

TEST_CASE("client gameplay role is local runtime configuration", "[config][player]")
{
    auto const shared = simnet::default_shared_config();
    auto const stationary_observer_config = simnet::default_client_config();
    auto player = stationary_observer_config;
    player.gameplay.role = "player";

    CHECK(
        simnet::fingerprint_runtime_config(shared, player).value
        != simnet::fingerprint_runtime_config(shared, stationary_observer_config).value
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
        }) {
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
    auto const runtime
        = simnet::fingerprint_runtime_config(treatment, simnet::default_server_config());
    auto check_changed = [&](simnet::SharedConfig changed) {
        CHECK(simnet::fingerprint_network_compatibility(changed).value != compatibility.value);
        CHECK(
            simnet::fingerprint_runtime_config(changed, simnet::default_server_config()).value
            != runtime.value
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
    auto control
        = simnet::load_shared_config(directory / "shared_player_influence_control_visual.json");
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
        simnet::fingerprint_network_compatibility(radius_config).value
        != simnet::fingerprint_network_compatibility(simnet::default_shared_config()).value
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
        }) {
        auto const invalid = TemporaryConfig{"simnet_aoi_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }
}

TEST_CASE("maintained AOI visual profiles load as distinct treatments", "[config][aoi]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto const radius = simnet::load_shared_config(directory / "shared_aoi_radius_visual.json");
    auto const fov = simnet::load_shared_config(directory / "shared_aoi_fov_visual.json");
    CHECK(radius.pipeline.area_of_interest.mode == "radius");
    CHECK(radius.pipeline.area_of_interest.radius == 80.0F);
    CHECK(fov.pipeline.area_of_interest.mode == "fov");
    CHECK(fov.pipeline.area_of_interest.radius == 80.0F);
    CHECK(fov.pipeline.area_of_interest.fov_degrees == 120.0F);
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
        simnet::fingerprint_network_compatibility(loaded).value
        != simnet::fingerprint_network_compatibility(simnet::default_shared_config()).value
    );
    auto changed = loaded;
    changed.pipeline.level_of_detail.near_distance += 1.0F;
    CHECK(
        simnet::fingerprint_network_compatibility(changed).value
        != simnet::fingerprint_network_compatibility(loaded).value
    );
    CHECK(
        simnet::fingerprint_runtime_config(changed, simnet::default_server_config()).value
        != simnet::fingerprint_runtime_config(loaded, simnet::default_server_config()).value
    );
    changed = loaded;
    changed.pipeline.level_of_detail.medium_distance += 1.0F;
    CHECK(
        simnet::fingerprint_network_compatibility(changed).value
        != simnet::fingerprint_network_compatibility(loaded).value
    );
    changed = loaded;
    ++changed.pipeline.level_of_detail.medium_interval_ticks;
    CHECK(
        simnet::fingerprint_network_compatibility(changed).value
        != simnet::fingerprint_network_compatibility(loaded).value
    );
    changed = loaded;
    ++changed.pipeline.level_of_detail.far_interval_ticks;
    CHECK(
        simnet::fingerprint_network_compatibility(changed).value
        != simnet::fingerprint_network_compatibility(loaded).value
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
        }) {
        auto const invalid = TemporaryConfig{"simnet_lod_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }
}

TEST_CASE("maintained LOD treatments differ only by level of detail", "[config][lod]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto none = simnet::load_shared_config(directory / "shared_lod_none_aoi_radius_visual.json");
    auto distance = simnet::load_shared_config(
        directory / "shared_lod_distance_bands_aoi_radius_visual.json"
    );
    CHECK_FALSE(none.pipeline.enable_incremental);
    CHECK(none.pipeline.enable_delta);
    CHECK(distance.pipeline.level_of_detail.mode == "distance_bands");
    CHECK(distance.pipeline.level_of_detail.near_distance == 40.0F);
    CHECK(distance.pipeline.level_of_detail.medium_distance == 100.0F);
    CHECK(distance.pipeline.level_of_detail.medium_interval_ticks == 4U);
    CHECK(distance.pipeline.level_of_detail.far_interval_ticks == 16U);
    distance.pipeline.level_of_detail = none.pipeline.level_of_detail;
    CHECK(
        simnet::fingerprint_network_compatibility(distance).value
        == simnet::fingerprint_network_compatibility(none).value
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
        simnet::fingerprint_network_compatibility(loaded).value
        != simnet::fingerprint_network_compatibility(simnet::default_shared_config()).value
    );

    for (auto const contents : {
             R"({ "packetization": { "enabled": true, "unexpected": 1 } })",
             R"({ "packetization": { "enabled": true, "max_payload_bytes": 25 } })",
             R"({ "packetization": { "enabled": true, "max_update_bytes": 4194305 } })",
             R"({ "packetization": { "enabled": true, "max_chunks_per_update": 4097 } })",
             R"({ "packetization": { "enabled": true, "max_in_flight_updates": 0 } })",
             R"({ "packetization": { "enabled": true, "max_incomplete_bytes": 1 } })",
             R"({ "packetization": { "enabled": true, "reassembly_timeout_ms": 0 } })",
         }) {
        auto const invalid = TemporaryConfig{"simnet_packetization_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }
}

TEST_CASE("maintained forced packetization treatment loads", "[config][packetization][aoi]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto const config
        = simnet::load_shared_config(directory / "shared_packetization_aoi_radius_visual.json");
    CHECK(config.packetization.enabled);
    CHECK(config.packetization.max_payload_bytes == 256U);
    CHECK(config.pipeline.area_of_interest.mode == "radius");
    CHECK(config.pipeline.area_of_interest.radius == 160.0F);
}

TEST_CASE("compression configuration is strict and fingerprinted", "[config][compression]")
{
    auto const accepted = TemporaryConfig{
        "simnet_compression_accepted.json",
        R"({ "compression": { "mode": "whole_update", "level": 1 } })"
    };
    auto const loaded = simnet::load_shared_config(accepted.path());
    CHECK(loaded.compression.mode == "whole_update");
    CHECK(loaded.compression.level == 1);

    for (auto const contents : {
             R"({ "compression": { "mode": "none", "level": 1 } })",
             R"({ "compression": { "mode": "whole_update" } })",
             R"({ "compression": { "mode": "per_packet", "level": 0 } })",
             R"({ "compression": { "mode": "per_packet", "level": 20 } })",
             R"({ "compression": { "mode": "unknown", "level": 1 } })",
             R"({ "compression": { "mode": "whole_update", "level": 1, "extra": true } })",
         }) {
        auto const invalid = TemporaryConfig{"simnet_compression_invalid.json", contents};
        CHECK_THROWS(simnet::load_shared_config(invalid.path()));
    }
}

TEST_CASE("maintained compression treatments load with matching controls", "[config][compression]")
{
    auto const directory = std::filesystem::path{__FILE__}.parent_path().parent_path() / "config";
    auto const none
        = simnet::load_shared_config(directory / "shared_compression_none_aoi_radius_visual.json");
    auto const whole
        = simnet::load_shared_config(directory / "shared_compression_whole_aoi_radius_visual.json");
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
        simnet::fingerprint_network_compatibility(normalized_whole).value
        == control_fingerprint.value
    );
    CHECK(
        simnet::fingerprint_network_compatibility(normalized_per_packet).value
        == control_fingerprint.value
    );
}

// TEST_CASE("boids demo profile loads a conservative deterministic scenario", "[config]")
// {
//     auto const path = simnet::default_shared_config_path().parent_path()
//         / "shared_boids_demo.json";
//     auto const config = simnet::load_shared_config(path);
//
//     CHECK(config.simulation.initial_boid_count == 1000U);
//     CHECK(config.simulation.world_half == 65.0F);
//     CHECK(config.spatial.cell_size == 18.0F);
//     CHECK(config.spatial.max_neighbors == 64U);
//     CHECK(config.boids.min_speed <= config.boids.cruise_speed);
//     CHECK(config.boids.cruise_speed <= config.boids.max_speed);
//     CHECK(config.boids.alignment_radius == 18.0F);
//     CHECK(config.boids.cohesion_radius == 18.0F);
//     CHECK(config.boids.enable_wander);
//     CHECK(config.boids.enable_hue_assimilation);
//     CHECK(config.player.yaw_acceleration_degrees == 360.0F);
//     CHECK(config.player.pitch_acceleration_degrees == 300.0F);
//     CHECK(config.player.max_yaw_rate_degrees == 120.0F);
//     CHECK(config.player.max_pitch_rate_degrees == 90.0F);
// }
