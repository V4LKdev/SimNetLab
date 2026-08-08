module;

#include <algorithm>
#include <bit>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

module simnet.config;

import :types;
import simnet.core;
import simnet.packetization;

namespace
{
    using Json = nlohmann::json;

    // FNV-1a is enough here for stable traceability fingerprints.
    constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ULL;
    constexpr std::uint64_t fnv_prime = 1099511628211ULL;

    template <typename Value> void hash_bytes(std::uint64_t& hash, Value const& value) noexcept
    {
        static_assert(std::is_trivially_copyable_v<Value>);
        auto const* bytes = reinterpret_cast<unsigned char const*>(&value);
        for (std::size_t index = 0; index < sizeof(Value); ++index) {
            hash ^= bytes[index];
            hash *= fnv_prime;
        }
    }

    void hash_string(std::uint64_t& hash, std::string_view value) noexcept
    {
        for (char character : value) {
            hash ^= static_cast<unsigned char>(character);
            hash *= fnv_prime;
        }
    }

    void hash_canonical_byte(std::uint64_t& hash, std::uint8_t value) noexcept
    {
        hash ^= value;
        hash *= fnv_prime;
    }

    void hash_canonical_u32(std::uint64_t& hash, std::uint32_t value) noexcept
    {
        for (auto shift = 24; shift >= 0; shift -= 8) {
            hash_canonical_byte(hash, static_cast<std::uint8_t>(value >> shift));
        }
    }

    void hash_canonical_u64(std::uint64_t& hash, std::uint64_t value) noexcept
    {
        for (auto shift = 56; shift >= 0; shift -= 8) {
            hash_canonical_byte(hash, static_cast<std::uint8_t>(value >> shift));
        }
    }

    void hash_canonical_bool(std::uint64_t& hash, bool value) noexcept
    {
        hash_canonical_byte(hash, value ? 1U : 0U);
    }

    void hash_canonical_float(std::uint64_t& hash, float value) noexcept
    {
        hash_canonical_u32(hash, std::bit_cast<std::uint32_t>(value));
    }

    void hash_canonical_double(std::uint64_t& hash, double value) noexcept
    {
        hash_canonical_u64(hash, std::bit_cast<std::uint64_t>(value));
    }

    template <typename Value> void read_optional(Json const& object, char const* key, Value& value)
    {
        auto const found = object.find(key);
        if (found == object.end()) {
            return;
        }

        try {
            value = found->get<Value>();
        } catch (nlohmann::json::exception const& error) {
            throw std::runtime_error(
                std::string{"invalid config field '"} + key + "': " + error.what()
            );
        }
    }

    void read_optional_u32(Json const& object, char const* key, std::uint32_t& value)
    {
        auto const found = object.find(key);
        if (found == object.end()) {
            return;
        }
        if (!found->is_number_unsigned()) {
            throw std::runtime_error(
                std::string{"invalid config field '"} + key + "': expected unsigned integer"
            );
        }
        auto const parsed = found->get<std::uint64_t>();
        if (parsed > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error(
                std::string{"invalid config field '"} + key + "': value exceeds uint32 range"
            );
        }
        value = static_cast<std::uint32_t>(parsed);
    }

    Json const* optional_object(Json const& object, char const* key)
    {
        auto const found = object.find(key);
        if (found == object.end()) {
            return nullptr;
        }
        if (!found->is_object()) {
            throw std::runtime_error(
                std::string{"invalid config section '"} + key + "': expected object"
            );
        }
        return &*found;
    }

    Json load_json(std::filesystem::path const& path)
    {
        std::ifstream file{path};
        if (!file) {
            throw std::runtime_error("failed to open config file: " + path.string());
        }

        try {
            return Json::parse(file);
        } catch (nlohmann::json::exception const& error) {
            throw std::runtime_error(
                "failed to parse config file '" + path.string() + "': " + error.what()
            );
        }
    }

    void validate_root(Json const& json)
    {
        if (!json.is_object()) {
            throw std::runtime_error("invalid config root: expected object");
        }
    }

    void validate_positive(char const* name, double value)
    {
        if (value <= 0.0) {
            throw std::runtime_error(
                std::string{"invalid config field '"} + name + "': expected positive value"
            );
        }
    }

    void validate_positive(char const* name, float value)
    {
        if (value <= 0.0F) {
            throw std::runtime_error(
                std::string{"invalid config field '"} + name + "': expected positive value"
            );
        }
    }

    void validate_non_zero(char const* name, std::uint32_t value)
    {
        if (value == 0U) {
            throw std::runtime_error(
                std::string{"invalid config field '"} + name + "': expected non-zero value"
            );
        }
    }

    void validate_one_of(
        char const* name,
        std::string_view value,
        std::initializer_list<std::string_view> allowed
    )
    {
        for (auto const option : allowed) {
            if (value == option) {
                return;
            }
        }
        throw std::runtime_error(
            std::string{"invalid config field '"} + name + "': unsupported value"
        );
    }

    void apply_run(Json const& json, simnet::RunConfig& config)
    {
        read_optional(json, "seed", config.seed);
    }

    void apply_simulation(Json const& json, simnet::SimulationConfig& config)
    {
        read_optional(json, "tick_rate_hz", config.tick_rate_hz);
        read_optional(json, "world_half", config.world_half);
        read_optional(json, "initial_boid_count", config.initial_boid_count);

        validate_positive("simulation.tick_rate_hz", config.tick_rate_hz);
        validate_positive("simulation.world_half", config.world_half);
    }

    void apply_spatial(Json const& json, simnet::SpatialConfig& config)
    {
        read_optional(json, "cell_size", config.cell_size);
        read_optional(json, "max_neighbors", config.max_neighbors);

        validate_positive("spatial.cell_size", config.cell_size);
        validate_non_zero("spatial.max_neighbors", config.max_neighbors);
    }

    void apply_player_influence_force(
        Json const& json,
        char const* field_name,
        simnet::PlayerInfluenceForceConfig& config
    )
    {
        for (auto const& [key, value] : json.items()) {
            static_cast<void>(value);
            if (key != "enabled" && key != "radius" && key != "max_acceleration") {
                throw std::runtime_error(
                    "invalid config field 'boids." + std::string{field_name} + "." + key
                    + "': unknown field"
                );
            }
        }
        if (!json.contains("enabled")) {
            throw std::runtime_error(
                "invalid config field 'boids." + std::string{field_name}
                + ".enabled': field is required"
            );
        }
        read_optional(json, "enabled", config.enabled);
        auto const has_radius = json.contains("radius");
        auto const has_max_acceleration = json.contains("max_acceleration");
        if (!config.enabled) {
            if (has_radius || has_max_acceleration) {
                throw std::runtime_error(
                    "invalid config section 'boids." + std::string{field_name}
                    + "': disabled influence rejects active fields"
                );
            }
            config.radius = 0.0F;
            config.max_acceleration = 0.0F;
            return;
        }
        if (!has_radius || !has_max_acceleration) {
            throw std::runtime_error(
                "invalid config section 'boids." + std::string{field_name}
                + "': enabled influence requires radius and max_acceleration"
            );
        }
        read_optional(json, "radius", config.radius);
        read_optional(json, "max_acceleration", config.max_acceleration);
        if (!std::isfinite(config.radius) || config.radius <= 0.0F
            || !std::isfinite(config.max_acceleration) || config.max_acceleration <= 0.0F) {
            throw std::runtime_error(
                "invalid config section 'boids." + std::string{field_name}
                + "': active values must be finite and positive"
            );
        }
    }

    void apply_boids(Json const& json, simnet::BoidsConfig& config)
    {
        read_optional(json, "enable_separation", config.enable_separation);
        read_optional(json, "enable_alignment", config.enable_alignment);
        read_optional(json, "enable_cohesion", config.enable_cohesion);
        read_optional(json, "enable_containment", config.enable_containment);
        read_optional(json, "enable_wander", config.enable_wander);
        read_optional(json, "enable_hue_assimilation", config.enable_hue_assimilation);
        read_optional(json, "enable_hue_drift", config.enable_hue_drift);
        read_optional(json, "min_speed", config.min_speed);
        read_optional(json, "cruise_speed", config.cruise_speed);
        read_optional(json, "max_speed", config.max_speed);
        read_optional(json, "max_acceleration", config.max_acceleration);
        auto legacy_perception_radius = config.alignment_radius;
        read_optional(json, "perception_radius", legacy_perception_radius);
        config.alignment_radius = legacy_perception_radius;
        config.cohesion_radius = legacy_perception_radius;
        read_optional(json, "separation_radius", config.separation_radius);
        read_optional(json, "alignment_radius", config.alignment_radius);
        read_optional(json, "cohesion_radius", config.cohesion_radius);
        read_optional(json, "field_of_view_degrees", config.field_of_view_degrees);
        read_optional(
            json,
            "containment_prediction_seconds",
            config.containment_prediction_seconds
        );
        read_optional(json, "containment_margin", config.containment_margin);
        read_optional(json, "separation_acceleration", config.separation_acceleration);
        read_optional(json, "containment_acceleration", config.containment_acceleration);
        read_optional(json, "alignment_acceleration", config.alignment_acceleration);
        read_optional(json, "cohesion_acceleration", config.cohesion_acceleration);
        read_optional(json, "wander_acceleration", config.wander_acceleration);
        read_optional(json, "wander_frequency_hz", config.wander_frequency_hz);
        read_optional(json, "hue_assimilation_rate", config.hue_assimilation_rate);
        read_optional(json, "hue_drift_rate", config.hue_drift_rate);
        if (auto const* section = optional_object(json, "player_lure")) {
            apply_player_influence_force(*section, "player_lure", config.player_lure);
        }
        if (auto const* section = optional_object(json, "player_predator")) {
            apply_player_influence_force(*section, "player_predator", config.player_predator);
        }

        if (config.min_speed < 0.0F || config.min_speed > config.cruise_speed
            || config.cruise_speed > config.max_speed) {
            throw std::runtime_error(
                "invalid boids speed limits: expected 0 <= min_speed <= cruise_speed <= max_speed"
            );
        }
        validate_positive("boids.max_speed", config.max_speed);
        validate_positive("boids.max_acceleration", config.max_acceleration);
        validate_positive("boids.separation_radius", config.separation_radius);
        validate_positive("boids.alignment_radius", config.alignment_radius);
        validate_positive("boids.cohesion_radius", config.cohesion_radius);
        if (config.field_of_view_degrees <= 0.0F || config.field_of_view_degrees > 360.0F) {
            throw std::runtime_error(
                "invalid config field 'boids.field_of_view_degrees': expected (0, 360]"
            );
        }
        validate_positive(
            "boids.containment_prediction_seconds",
            config.containment_prediction_seconds
        );
        validate_positive("boids.containment_margin", config.containment_margin);
        if (config.separation_acceleration < 0.0F || config.containment_acceleration < 0.0F
            || config.alignment_acceleration < 0.0F || config.cohesion_acceleration < 0.0F
            || config.wander_acceleration < 0.0F) {
            throw std::runtime_error(
                "invalid boids rule acceleration: expected non-negative values"
            );
        }
        validate_positive("boids.wander_frequency_hz", config.wander_frequency_hz);
        validate_positive("boids.hue_assimilation_rate", config.hue_assimilation_rate);
        validate_positive("boids.hue_drift_rate", config.hue_drift_rate);
    }

    void validate_player_influence_forces(simnet::SharedConfig const& config)
    {
        auto validate
            = [&](char const* field_name, simnet::PlayerInfluenceForceConfig const& force) {
                  if (!force.enabled) {
                      return;
                  }
                  if (force.radius > config.simulation.world_half * 2.0F) {
                      throw std::runtime_error(
                          "invalid config field 'boids." + std::string{field_name}
                          + ".radius': expected no greater than twice simulation.world_half"
                      );
                  }
                  if (force.max_acceleration > config.boids.max_acceleration) {
                      throw std::runtime_error(
                          "invalid config field 'boids." + std::string{field_name}
                          + ".max_acceleration': expected no greater than boids.max_acceleration"
                      );
                  }
              };
        validate("player_lure", config.boids.player_lure);
        validate("player_predator", config.boids.player_predator);
    }

    void apply_player(Json const& json, simnet::PlayerConfig& config)
    {
        read_optional(json, "cruise_speed", config.cruise_speed);
        read_optional(json, "boost_speed", config.boost_speed);
        read_optional(json, "slow_speed", config.slow_speed);
        read_optional(json, "speed_change_rate", config.speed_change_rate);
        read_optional(json, "yaw_acceleration_degrees", config.yaw_acceleration_degrees);
        read_optional(json, "pitch_acceleration_degrees", config.pitch_acceleration_degrees);
        read_optional(json, "yaw_damping", config.yaw_damping);
        read_optional(json, "pitch_damping", config.pitch_damping);
        read_optional(json, "max_yaw_rate_degrees", config.max_yaw_rate_degrees);
        read_optional(json, "max_pitch_rate_degrees", config.max_pitch_rate_degrees);
        read_optional(json, "pitch_limit_degrees", config.pitch_limit_degrees);
        if (!std::isfinite(config.slow_speed) || !std::isfinite(config.cruise_speed)
            || !std::isfinite(config.boost_speed) || !std::isfinite(config.speed_change_rate)
            || !std::isfinite(config.yaw_acceleration_degrees)
            || !std::isfinite(config.pitch_acceleration_degrees)
            || !std::isfinite(config.yaw_damping) || !std::isfinite(config.pitch_damping)
            || !std::isfinite(config.max_yaw_rate_degrees)
            || !std::isfinite(config.max_pitch_rate_degrees)
            || !std::isfinite(config.pitch_limit_degrees) || config.slow_speed < 0.0F
            || config.slow_speed > config.cruise_speed
            || config.cruise_speed > config.boost_speed) {
            throw std::runtime_error(
                "invalid player speed limits: expected 0 <= slow_speed <= cruise_speed <= boost_speed"
            );
        }
        validate_positive("player.boost_speed", config.boost_speed);
        validate_positive("player.speed_change_rate", config.speed_change_rate);
        validate_positive("player.yaw_acceleration_degrees", config.yaw_acceleration_degrees);
        validate_positive("player.pitch_acceleration_degrees", config.pitch_acceleration_degrees);
        validate_positive("player.yaw_damping", config.yaw_damping);
        validate_positive("player.pitch_damping", config.pitch_damping);
        validate_positive("player.max_yaw_rate_degrees", config.max_yaw_rate_degrees);
        validate_positive("player.max_pitch_rate_degrees", config.max_pitch_rate_degrees);
        if (config.pitch_limit_degrees <= 0.0F || config.pitch_limit_degrees > 85.0F) {
            throw std::runtime_error(
                "invalid config field 'player.pitch_limit_degrees': expected (0, 85]"
            );
        }
    }

    void apply_gameplay(Json const& json, simnet::GameplayConfig& config)
    {
        read_optional(json, "role", config.role);
        read_optional(json, "stationary_observer_position", config.stationary_observer_position);
        validate_one_of("gameplay.role", config.role, {"stationary_observer", "player"});
        if (!std::ranges::all_of(config.stationary_observer_position, [](float value) {
                return std::isfinite(value);
            })) {
            throw std::runtime_error(
                "invalid config field 'gameplay.stationary_observer_position': expected finite values"
            );
        }
    }

    void apply_area_of_interest(Json const& json, simnet::AreaOfInterestConfig& config)
    {
        for (auto const& [key, value] : json.items()) {
            static_cast<void>(value);
            if (key != "mode" && key != "radius" && key != "fov_degrees") {
                throw std::runtime_error(
                    "invalid config field 'pipeline.area_of_interest." + key + "': unknown field"
                );
            }
        }

        read_optional(json, "mode", config.mode);
        validate_one_of("pipeline.area_of_interest.mode", config.mode, {"none", "radius", "fov"});
        auto const has_radius = json.contains("radius");
        auto const has_fov = json.contains("fov_degrees");
        if (config.mode == "none") {
            if (has_radius || has_fov) {
                throw std::runtime_error(
                    "invalid pipeline.area_of_interest: none mode accepts no geometry fields"
                );
            }
            return;
        }

        if (!has_radius) {
            throw std::runtime_error(
                "invalid pipeline.area_of_interest: active mode requires radius"
            );
        }
        read_optional(json, "radius", config.radius);
        if (!std::isfinite(config.radius) || config.radius <= 0.0F) {
            throw std::runtime_error(
                "invalid config field 'pipeline.area_of_interest.radius': expected positive finite value"
            );
        }
        if (config.mode == "radius") {
            if (has_fov) {
                throw std::runtime_error(
                    "invalid pipeline.area_of_interest: radius mode accepts no FOV field"
                );
            }
            return;
        }

        if (!has_fov) {
            throw std::runtime_error(
                "invalid pipeline.area_of_interest: fov mode requires fov_degrees"
            );
        }
        read_optional(json, "fov_degrees", config.fov_degrees);
        if (!std::isfinite(config.fov_degrees) || config.fov_degrees <= 0.0F
            || config.fov_degrees > 180.0F) {
            throw std::runtime_error(
                "invalid config field 'pipeline.area_of_interest.fov_degrees': expected (0, 180]"
            );
        }
    }

    void apply_level_of_detail(Json const& json, simnet::LevelOfDetailConfig& config)
    {
        for (auto const& [key, value] : json.items()) {
            static_cast<void>(value);
            if (key != "mode" && key != "near_distance" && key != "medium_distance"
                && key != "medium_interval_ticks" && key != "far_interval_ticks") {
                throw std::runtime_error(
                    "invalid config field 'pipeline.level_of_detail." + key + "': unknown field"
                );
            }
        }

        read_optional(json, "mode", config.mode);
        validate_one_of("pipeline.level_of_detail.mode", config.mode, {"none", "distance_bands"});
        auto const has_near = json.contains("near_distance");
        auto const has_medium = json.contains("medium_distance");
        auto const has_medium_interval = json.contains("medium_interval_ticks");
        auto const has_far_interval = json.contains("far_interval_ticks");
        if (config.mode == "none") {
            if (has_near || has_medium || has_medium_interval || has_far_interval) {
                throw std::runtime_error(
                    "invalid pipeline.level_of_detail: none mode accepts no band fields"
                );
            }
            return;
        }
        if (!has_near || !has_medium || !has_medium_interval || !has_far_interval) {
            throw std::runtime_error(
                "invalid pipeline.level_of_detail: distance_bands requires every band field"
            );
        }

        read_optional(json, "near_distance", config.near_distance);
        read_optional(json, "medium_distance", config.medium_distance);
        read_optional(json, "medium_interval_ticks", config.medium_interval_ticks);
        read_optional(json, "far_interval_ticks", config.far_interval_ticks);
        if (!std::isfinite(config.near_distance) || config.near_distance <= 0.0F
            || !std::isfinite(config.medium_distance) || config.medium_distance <= 0.0F) {
            throw std::runtime_error(
                "invalid pipeline.level_of_detail: distances must be positive and finite"
            );
        }
        if (config.near_distance >= config.medium_distance) {
            throw std::runtime_error(
                "invalid pipeline.level_of_detail: near_distance must be below medium_distance"
            );
        }
        if (config.medium_interval_ticks < 2U) {
            throw std::runtime_error(
                "invalid pipeline.level_of_detail: medium interval must be at least 2"
            );
        }
        if (config.far_interval_ticks <= config.medium_interval_ticks
            || config.far_interval_ticks > 65'535U) {
            throw std::runtime_error(
                "invalid pipeline.level_of_detail: far interval must be greater and at most 65535"
            );
        }
    }

    void apply_pipeline(Json const& json, simnet::PipelineConfig& config)
    {
        for (auto const& [key, value] : json.items()) {
            static_cast<void>(value);
            if (key != "send_interval_ticks" && key != "enable_incremental"
                && key != "enable_quantization" && key != "enable_oct_heading"
                && key != "enable_delta" && key != "enable_delta_field_mask"
                && key != "enable_bit_packing" && key != "area_of_interest"
                && key != "level_of_detail") {
                throw std::runtime_error(
                    "invalid config field 'pipeline." + key + "': unknown field"
                );
            }
        }
        read_optional_u32(json, "send_interval_ticks", config.send_interval_ticks);
        read_optional(json, "enable_incremental", config.enable_incremental);
        read_optional(json, "enable_quantization", config.enable_quantization);
        read_optional(json, "enable_oct_heading", config.enable_oct_heading);
        read_optional(json, "enable_delta", config.enable_delta);
        read_optional(json, "enable_delta_field_mask", config.enable_delta_field_mask);
        read_optional(json, "enable_bit_packing", config.enable_bit_packing);
        validate_non_zero("pipeline.send_interval_ticks", config.send_interval_ticks);
        if (config.enable_oct_heading && !config.enable_quantization) {
            throw std::runtime_error(
                "invalid pipeline configuration: oct heading requires quantization"
            );
        }
        if (config.enable_bit_packing
            && (!config.enable_quantization || !config.enable_oct_heading)) {
            throw std::runtime_error(
                "invalid pipeline configuration: bit packing requires quantization and oct heading"
            );
        }
        if (config.enable_delta_field_mask && !config.enable_delta) {
            throw std::runtime_error(
                "invalid pipeline configuration: delta field mask requires Delta"
            );
        }
        if (auto const* section = optional_object(json, "area_of_interest")) {
            apply_area_of_interest(*section, config.area_of_interest);
        }
        if (auto const* section = optional_object(json, "level_of_detail")) {
            apply_level_of_detail(*section, config.level_of_detail);
        }
        if (config.level_of_detail.mode == "distance_bands") {
            if (config.area_of_interest.mode == "none") {
                throw std::runtime_error(
                    "invalid pipeline.level_of_detail: distance_bands requires radius or FOV AOI"
                );
            }
            if (config.level_of_detail.medium_distance >= config.area_of_interest.radius) {
                throw std::runtime_error(
                    "invalid pipeline.level_of_detail: medium_distance must be below AOI radius"
                );
            }
        }
    }

    void apply_packetization(Json const& json, simnet::PacketizationConfig& config)
    {
        for (auto const& [key, value] : json.items()) {
            static_cast<void>(value);
            if (key != "enabled" && key != "max_payload_bytes" && key != "max_update_bytes"
                && key != "max_chunks_per_update" && key != "max_in_flight_updates"
                && key != "max_incomplete_bytes" && key != "reassembly_timeout_ms") {
                throw std::runtime_error(
                    "invalid config field 'packetization." + key + "': unknown field"
                );
            }
        }

        read_optional(json, "enabled", config.enabled);
        read_optional(json, "max_payload_bytes", config.max_payload_bytes);
        read_optional(json, "max_update_bytes", config.max_update_bytes);
        read_optional(json, "max_chunks_per_update", config.max_chunks_per_update);
        read_optional(json, "max_in_flight_updates", config.max_in_flight_updates);
        read_optional(json, "max_incomplete_bytes", config.max_incomplete_bytes);
        read_optional(json, "reassembly_timeout_ms", config.reassembly_timeout_ms);

        simnet::validate_packetization_settings({
            .enabled = config.enabled,
            .max_payload_bytes = config.max_payload_bytes,
            .max_group_bytes = config.max_update_bytes,
            .max_chunks_per_group = config.max_chunks_per_update,
            .max_in_flight_groups = config.max_in_flight_updates,
            .max_incomplete_bytes = config.max_incomplete_bytes,
            .reassembly_timeout = simnet::Nanoseconds{
                static_cast<std::int64_t>(config.reassembly_timeout_ms) * 1'000'000
            },
        });
    }

    void apply_compression(Json const& json, simnet::CompressionConfig& config)
    {
        for (auto const& [key, value] : json.items()) {
            static_cast<void>(value);
            if (key != "mode" && key != "level" && key != "dictionary") {
                throw std::runtime_error(
                    "invalid config field 'compression." + key + "': unknown field"
                );
            }
        }

        auto const has_level = json.contains("level");
        auto const has_dictionary = json.contains("dictionary");
        read_optional(json, "mode", config.mode);
        if (config.mode == "none") {
            if (has_level || has_dictionary) {
                throw std::runtime_error(
                    "invalid compression config: none mode accepts no level or dictionary"
                );
            }
            return;
        }
        if (config.mode != "whole_update" && config.mode != "per_packet") {
            throw std::runtime_error(
                "invalid config field 'compression.mode': expected none, whole_update, or per_packet"
            );
        }
        if (!has_level) {
            throw std::runtime_error("invalid compression config: active mode requires level");
        }
        read_optional(json, "level", config.level);
        read_optional(json, "dictionary", config.dictionary);
        if (config.level < 1 || config.level > 19) {
            throw std::runtime_error(
                "invalid config field 'compression.level': expected integer in [1, 19]"
            );
        }
        if (config.dictionary != "none" && config.dictionary != "pipeline_v1") {
            throw std::runtime_error(
                "invalid config field 'compression.dictionary': expected none or pipeline_v1"
            );
        }
        if (config.mode == "per_packet" && config.dictionary != "none") {
            throw std::runtime_error(
                "invalid compression config: per_packet mode does not support dictionaries"
            );
        }
    }

    void apply_snapshot_delivery(Json const& json, simnet::SnapshotDeliveryConfig& config)
    {
        for (auto const& [key, value] : json.items()) {
            static_cast<void>(value);
            if (key != "mode" && key != "full_replace_after_unacknowledged_updates") {
                throw std::runtime_error(
                    "invalid config field 'snapshot_delivery." + key + "': unknown field"
                );
            }
        }

        if (!json.contains("mode") || !json.contains("full_replace_after_unacknowledged_updates")) {
            throw std::runtime_error(
                "invalid snapshot_delivery config: mode and recovery threshold are required"
            );
        }
        read_optional(json, "mode", config.mode);
        read_optional(
            json,
            "full_replace_after_unacknowledged_updates",
            config.full_replace_after_unacknowledged_updates
        );
        validate_one_of(
            "snapshot_delivery.mode",
            config.mode,
            {"reliable_sequenced", "unreliable_sequenced"}
        );
        if (config.full_replace_after_unacknowledged_updates == 0U
            || config.full_replace_after_unacknowledged_updates >= 64U) {
            throw std::runtime_error(
                "invalid config field 'snapshot_delivery.full_replace_after_unacknowledged_updates': expected 1..63"
            );
        }
    }

    void apply_transport(Json const& json, simnet::TransportConfig& config)
    {
        for (auto const& [key, value] : json.items()) {
            static_cast<void>(value);
            if (key != "host" && key != "port" && key != "max_clients" && key != "max_payload_bytes"
                && key != "send_size_policy") {
                throw std::runtime_error(
                    "invalid config field 'transport." + key + "': unknown field"
                );
            }
        }
        read_optional(json, "host", config.host);
        read_optional(json, "port", config.port);
        read_optional(json, "max_clients", config.max_clients);
        read_optional(json, "max_payload_bytes", config.max_payload_bytes);
        read_optional(json, "send_size_policy", config.send_size_policy);

        if (config.port == 0) {
            throw std::runtime_error(
                "invalid config field 'transport.port': expected non-zero port"
            );
        }
        validate_non_zero("transport.max_clients", config.max_clients);
        if (config.max_clients > 64U) {
            throw std::runtime_error(
                "invalid config field 'transport.max_clients': expected 1..64"
            );
        }
        validate_non_zero("transport.max_payload_bytes", config.max_payload_bytes);
        validate_one_of(
            "transport.send_size_policy",
            config.send_size_policy,
            {"enforce_limit", "allow_backend_fragmentation"}
        );
    }

    void apply_flecs(Json const& json, simnet::FlecsConfig& config)
    {
        read_optional(json, "thread_count", config.thread_count);
        validate_non_zero("flecs.thread_count", config.thread_count);
        if (config.thread_count > 64U) {
            throw std::runtime_error("invalid config field 'flecs.thread_count': expected 1..64");
        }
    }

    void apply_visualization(Json const& json, simnet::VisualizationConfig& config)
    {
        read_optional(json, "enabled", config.enabled);
        read_optional(json, "interpolation_enabled", config.interpolation_enabled);
        read_optional(json, "window_width", config.window_width);
        read_optional(json, "window_height", config.window_height);
        read_optional(json, "panel_width", config.panel_width);
        read_optional(json, "target_fps", config.target_fps);
        read_optional(json, "entity_scale", config.entity_scale);
        read_optional(json, "picking_radius", config.picking_radius);
        read_optional(
            json,
            "stationary_observer_interest_radius",
            config.stationary_observer_interest_radius
        );
        read_optional(
            json,
            "stationary_observer_vertical_fov_degrees",
            config.stationary_observer_vertical_fov_degrees
        );
        read_optional(json, "max_visible_spatial_cells", config.max_visible_spatial_cells);
        read_optional(json, "entity_mesh_path", config.entity_mesh_path);

        if (config.window_width == 0 || config.window_height == 0) {
            throw std::runtime_error(
                "invalid visualization dimensions: expected non-zero width and height"
            );
        }
        if (config.panel_width >= config.window_width) {
            throw std::runtime_error(
                "invalid visualization panel_width: expected less than window_width"
            );
        }
        if (config.target_fps == 0) {
            throw std::runtime_error("invalid visualization target_fps: expected non-zero value");
        }
        if (config.entity_scale <= 0.0F) {
            throw std::runtime_error("invalid visualization entity_scale: expected positive value");
        }
        if (config.picking_radius <= 0.0F) {
            throw std::runtime_error(
                "invalid visualization picking_radius: expected positive value"
            );
        }
        if (config.stationary_observer_interest_radius <= 0.0F) {
            throw std::runtime_error(
                "invalid visualization stationary_observer_interest_radius: expected positive value"
            );
        }
        if (config.stationary_observer_vertical_fov_degrees <= 0.0F
            || config.stationary_observer_vertical_fov_degrees >= 180.0F) {
            throw std::runtime_error(
                "invalid visualization stationary_observer_vertical_fov_degrees: expected range (0, 180)"
            );
        }
        if (config.max_visible_spatial_cells == 0U) {
            throw std::runtime_error(
                "invalid visualization max_visible_spatial_cells: expected non-zero value"
            );
        }
    }

    void apply_telemetry(Json const& json, simnet::TelemetryConfig& config)
    {
        read_optional(json, "console_log_enabled", config.console_log_enabled);
        read_optional(json, "file_log_enabled", config.file_log_enabled);
        read_optional(json, "log_directory", config.log_directory);
        read_optional(json, "min_level", config.min_level);
        read_optional(json, "metrics_csv_enabled", config.metrics_csv_enabled);
    }

    void apply_load_ramp(Json const& json, simnet::LoadRampConfig& config)
    {
        read_optional(json, "enabled", config.enabled);
        read_optional(json, "add_boids_per_step", config.add_boids_per_step);
        read_optional(json, "step_interval_seconds", config.step_interval_seconds);
        read_optional(json, "max_boids", config.max_boids);

        validate_positive(
            "benchmark.load_ramp.step_interval_seconds",
            config.step_interval_seconds
        );
    }

    void apply_benchmark(Json const& json, simnet::BenchmarkScenarioConfig& config)
    {
        read_optional(json, "enabled", config.enabled);
        read_optional(json, "repetitions", config.repetitions);

        if (auto const* section = optional_object(json, "load_ramp")) {
            apply_load_ramp(*section, config.load_ramp);
        }
    }

    simnet::SharedConfig parse_shared_config(Json const& json)
    {
        validate_root(json);

        // Missing fields intentionally keep their typed defaults.
        auto config = simnet::default_shared_config();

        if (auto const* section = optional_object(json, "run")) {
            apply_run(*section, config.run);
        }
        if (auto const* section = optional_object(json, "simulation")) {
            apply_simulation(*section, config.simulation);
        }
        if (auto const* section = optional_object(json, "spatial")) {
            apply_spatial(*section, config.spatial);
        }
        if (auto const* section = optional_object(json, "boids")) {
            apply_boids(*section, config.boids);
        }
        if (auto const* section = optional_object(json, "player")) {
            apply_player(*section, config.player);
        }
        if (auto const* section = optional_object(json, "pipeline")) {
            apply_pipeline(*section, config.pipeline);
        }
        if (auto const* section = optional_object(json, "snapshot_delivery")) {
            apply_snapshot_delivery(*section, config.snapshot_delivery);
        }
        if (auto const* section = optional_object(json, "compression")) {
            apply_compression(*section, config.compression);
        }
        if (auto const* section = optional_object(json, "packetization")) {
            apply_packetization(*section, config.packetization);
        }

        validate_player_influence_forces(config);

        return config;
    }

    simnet::ServerConfig parse_server_config(Json const& json)
    {
        validate_root(json);

        // Server-local config owns transport, telemetry, and benchmark knobs.
        auto config = simnet::default_server_config();

        if (auto const* section = optional_object(json, "transport")) {
            apply_transport(*section, config.transport);
        }
        if (auto const* section = optional_object(json, "flecs")) {
            apply_flecs(*section, config.flecs);
        }
        if (auto const* section = optional_object(json, "visualization")) {
            apply_visualization(*section, config.visualization);
        }
        if (auto const* section = optional_object(json, "telemetry")) {
            apply_telemetry(*section, config.telemetry);
        }
        if (auto const* section = optional_object(json, "benchmark")) {
            apply_benchmark(*section, config.benchmark);
        }

        return config;
    }

    simnet::ClientConfig parse_client_config(Json const& json)
    {
        validate_root(json);

        // Client-local config owns transport, telemetry, and rendering knobs.
        auto config = simnet::default_client_config();

        if (auto const* section = optional_object(json, "transport")) {
            apply_transport(*section, config.transport);
        }
        if (auto const* section = optional_object(json, "gameplay")) {
            apply_gameplay(*section, config.gameplay);
        }
        if (auto const* section = optional_object(json, "visualization")) {
            apply_visualization(*section, config.visualization);
        }
        if (auto const* section = optional_object(json, "telemetry")) {
            apply_telemetry(*section, config.telemetry);
        }

        return config;
    }

    void hash_shared_native(std::uint64_t& hash, simnet::SharedConfig const& config) noexcept
    {
        // Runtime trace fingerprints preserve their existing native representation.
        hash_bytes(hash, config.run.seed);
        hash_bytes(hash, config.simulation.tick_rate_hz);
        hash_bytes(hash, config.simulation.world_half);
        hash_bytes(hash, config.simulation.initial_boid_count);
        hash_bytes(hash, config.spatial.cell_size);
        hash_bytes(hash, config.spatial.max_neighbors);
        hash_bytes(hash, config.boids.min_speed);
        hash_bytes(hash, config.boids.cruise_speed);
        hash_bytes(hash, config.boids.max_speed);
        hash_bytes(hash, config.boids.max_acceleration);
        hash_bytes(hash, config.boids.enable_separation);
        hash_bytes(hash, config.boids.enable_alignment);
        hash_bytes(hash, config.boids.enable_cohesion);
        hash_bytes(hash, config.boids.enable_containment);
        hash_bytes(hash, config.boids.enable_wander);
        hash_bytes(hash, config.boids.enable_hue_assimilation);
        hash_bytes(hash, config.boids.enable_hue_drift);
        hash_bytes(hash, config.boids.separation_radius);
        hash_bytes(hash, config.boids.alignment_radius);
        hash_bytes(hash, config.boids.cohesion_radius);
        hash_bytes(hash, config.boids.field_of_view_degrees);
        hash_bytes(hash, config.boids.containment_prediction_seconds);
        hash_bytes(hash, config.boids.containment_margin);
        hash_bytes(hash, config.boids.separation_acceleration);
        hash_bytes(hash, config.boids.containment_acceleration);
        hash_bytes(hash, config.boids.alignment_acceleration);
        hash_bytes(hash, config.boids.cohesion_acceleration);
        hash_bytes(hash, config.boids.wander_acceleration);
        hash_bytes(hash, config.boids.wander_frequency_hz);
        hash_bytes(hash, config.boids.hue_assimilation_rate);
        hash_bytes(hash, config.boids.hue_drift_rate);
        hash_bytes(hash, config.boids.player_lure.enabled);
        hash_bytes(hash, config.boids.player_lure.radius);
        hash_bytes(hash, config.boids.player_lure.max_acceleration);
        hash_bytes(hash, config.boids.player_predator.enabled);
        hash_bytes(hash, config.boids.player_predator.radius);
        hash_bytes(hash, config.boids.player_predator.max_acceleration);
        hash_bytes(hash, config.player.cruise_speed);
        hash_bytes(hash, config.player.boost_speed);
        hash_bytes(hash, config.player.slow_speed);
        hash_bytes(hash, config.player.speed_change_rate);
        hash_bytes(hash, config.player.yaw_acceleration_degrees);
        hash_bytes(hash, config.player.pitch_acceleration_degrees);
        hash_bytes(hash, config.player.yaw_damping);
        hash_bytes(hash, config.player.pitch_damping);
        hash_bytes(hash, config.player.max_yaw_rate_degrees);
        hash_bytes(hash, config.player.max_pitch_rate_degrees);
        hash_bytes(hash, config.player.pitch_limit_degrees);
        hash_bytes(hash, config.pipeline.send_interval_ticks);
        hash_bytes(hash, config.pipeline.enable_incremental);
        hash_bytes(hash, config.pipeline.enable_quantization);
        hash_bytes(hash, config.pipeline.enable_oct_heading);
        hash_bytes(hash, config.pipeline.enable_delta);
        if (config.pipeline.enable_delta_field_mask) {
            hash_string(hash, "delta_field_mask");
            hash_bytes(hash, config.pipeline.enable_delta_field_mask);
        }
        hash_bytes(hash, config.pipeline.enable_bit_packing);
        hash_string(hash, config.pipeline.area_of_interest.mode);
        hash_bytes(hash, config.pipeline.area_of_interest.radius);
        hash_bytes(hash, config.pipeline.area_of_interest.fov_degrees);
        hash_string(hash, config.pipeline.level_of_detail.mode);
        hash_bytes(hash, config.pipeline.level_of_detail.near_distance);
        hash_bytes(hash, config.pipeline.level_of_detail.medium_distance);
        hash_bytes(hash, config.pipeline.level_of_detail.medium_interval_ticks);
        hash_bytes(hash, config.pipeline.level_of_detail.far_interval_ticks);
        hash_string(hash, config.snapshot_delivery.mode);
        hash_bytes(hash, config.snapshot_delivery.full_replace_after_unacknowledged_updates);
        hash_string(hash, config.compression.mode);
        hash_bytes(hash, config.compression.level);
        if (config.compression.dictionary != "none") {
            hash_string(hash, config.compression.dictionary);
        }
        hash_bytes(hash, config.packetization.enabled);
        hash_bytes(hash, config.packetization.max_payload_bytes);
        hash_bytes(hash, config.packetization.max_update_bytes);
        hash_bytes(hash, config.packetization.max_chunks_per_update);
        hash_bytes(hash, config.packetization.max_in_flight_updates);
        hash_bytes(hash, config.packetization.max_incomplete_bytes);
        hash_bytes(hash, config.packetization.reassembly_timeout_ms);
    }

    void
    hash_network_compatibility(std::uint64_t& hash, simnet::SharedConfig const& config) noexcept
    {
        // Handshake compatibility must not depend on host byte order or bool layout.
        static_assert(std::numeric_limits<float>::is_iec559);
        static_assert(std::numeric_limits<double>::is_iec559);
        hash_canonical_u64(hash, config.run.seed);
        hash_canonical_double(hash, config.simulation.tick_rate_hz);
        hash_canonical_float(hash, config.simulation.world_half);
        hash_canonical_u32(hash, config.simulation.initial_boid_count);
        hash_canonical_float(hash, config.spatial.cell_size);
        hash_canonical_u32(hash, config.spatial.max_neighbors);
        hash_canonical_float(hash, config.boids.min_speed);
        hash_canonical_float(hash, config.boids.cruise_speed);
        hash_canonical_float(hash, config.boids.max_speed);
        hash_canonical_float(hash, config.boids.max_acceleration);
        hash_canonical_bool(hash, config.boids.enable_separation);
        hash_canonical_bool(hash, config.boids.enable_alignment);
        hash_canonical_bool(hash, config.boids.enable_cohesion);
        hash_canonical_bool(hash, config.boids.enable_containment);
        hash_canonical_bool(hash, config.boids.enable_wander);
        hash_canonical_bool(hash, config.boids.enable_hue_assimilation);
        hash_canonical_bool(hash, config.boids.enable_hue_drift);
        hash_canonical_float(hash, config.boids.separation_radius);
        hash_canonical_float(hash, config.boids.alignment_radius);
        hash_canonical_float(hash, config.boids.cohesion_radius);
        hash_canonical_float(hash, config.boids.field_of_view_degrees);
        hash_canonical_float(hash, config.boids.containment_prediction_seconds);
        hash_canonical_float(hash, config.boids.containment_margin);
        hash_canonical_float(hash, config.boids.separation_acceleration);
        hash_canonical_float(hash, config.boids.containment_acceleration);
        hash_canonical_float(hash, config.boids.alignment_acceleration);
        hash_canonical_float(hash, config.boids.cohesion_acceleration);
        hash_canonical_float(hash, config.boids.wander_acceleration);
        hash_canonical_float(hash, config.boids.wander_frequency_hz);
        hash_canonical_float(hash, config.boids.hue_assimilation_rate);
        hash_canonical_float(hash, config.boids.hue_drift_rate);
        hash_canonical_bool(hash, config.boids.player_lure.enabled);
        hash_canonical_float(hash, config.boids.player_lure.radius);
        hash_canonical_float(hash, config.boids.player_lure.max_acceleration);
        hash_canonical_bool(hash, config.boids.player_predator.enabled);
        hash_canonical_float(hash, config.boids.player_predator.radius);
        hash_canonical_float(hash, config.boids.player_predator.max_acceleration);
        hash_canonical_float(hash, config.player.cruise_speed);
        hash_canonical_float(hash, config.player.boost_speed);
        hash_canonical_float(hash, config.player.slow_speed);
        hash_canonical_float(hash, config.player.speed_change_rate);
        hash_canonical_float(hash, config.player.yaw_acceleration_degrees);
        hash_canonical_float(hash, config.player.pitch_acceleration_degrees);
        hash_canonical_float(hash, config.player.yaw_damping);
        hash_canonical_float(hash, config.player.pitch_damping);
        hash_canonical_float(hash, config.player.max_yaw_rate_degrees);
        hash_canonical_float(hash, config.player.max_pitch_rate_degrees);
        hash_canonical_float(hash, config.player.pitch_limit_degrees);
        hash_canonical_u32(hash, config.pipeline.send_interval_ticks);
        hash_canonical_bool(hash, config.pipeline.enable_incremental);
        hash_canonical_bool(hash, config.pipeline.enable_quantization);
        hash_canonical_bool(hash, config.pipeline.enable_oct_heading);
        hash_canonical_bool(hash, config.pipeline.enable_delta);
        if (config.pipeline.enable_delta_field_mask) {
            hash_string(hash, "delta_field_mask");
            hash_canonical_bool(hash, config.pipeline.enable_delta_field_mask);
        }
        hash_canonical_bool(hash, config.pipeline.enable_bit_packing);
        hash_string(hash, config.pipeline.area_of_interest.mode);
        hash_canonical_float(hash, config.pipeline.area_of_interest.radius);
        hash_canonical_float(hash, config.pipeline.area_of_interest.fov_degrees);
        hash_string(hash, config.pipeline.level_of_detail.mode);
        hash_canonical_float(hash, config.pipeline.level_of_detail.near_distance);
        hash_canonical_float(hash, config.pipeline.level_of_detail.medium_distance);
        hash_canonical_u32(hash, config.pipeline.level_of_detail.medium_interval_ticks);
        hash_canonical_u32(hash, config.pipeline.level_of_detail.far_interval_ticks);
        hash_string(hash, config.snapshot_delivery.mode);
        hash_canonical_u32(
            hash,
            config.snapshot_delivery.full_replace_after_unacknowledged_updates
        );
        hash_string(hash, config.compression.mode);
        hash_canonical_u32(hash, static_cast<std::uint32_t>(config.compression.level));
        if (config.compression.dictionary != "none") {
            hash_string(hash, config.compression.dictionary);
        }
        hash_canonical_bool(hash, config.packetization.enabled);
        hash_canonical_u32(hash, config.packetization.max_payload_bytes);
        hash_canonical_u32(hash, config.packetization.max_update_bytes);
        hash_canonical_u32(hash, config.packetization.max_chunks_per_update);
        hash_canonical_u32(hash, config.packetization.max_in_flight_updates);
        hash_canonical_u32(hash, config.packetization.max_incomplete_bytes);
        hash_canonical_u32(hash, config.packetization.reassembly_timeout_ms);
    }

    void hash_transport_and_telemetry(
        std::uint64_t& hash,
        simnet::TransportConfig const& transport,
        simnet::TelemetryConfig const& telemetry
    ) noexcept
    {
        hash_string(hash, transport.host);
        hash_bytes(hash, transport.port);
        hash_bytes(hash, transport.max_clients);
        hash_bytes(hash, transport.max_payload_bytes);
        hash_string(hash, transport.send_size_policy);
        hash_bytes(hash, telemetry.console_log_enabled);
        hash_bytes(hash, telemetry.file_log_enabled);
        hash_string(hash, telemetry.min_level);
        hash_bytes(hash, telemetry.metrics_csv_enabled);
    }

    void hash_visualization(
        std::uint64_t& hash,
        simnet::VisualizationConfig const& visualization
    ) noexcept
    {
        hash_bytes(hash, visualization.enabled);
        hash_bytes(hash, visualization.interpolation_enabled);
        hash_bytes(hash, visualization.window_width);
        hash_bytes(hash, visualization.window_height);
        hash_bytes(hash, visualization.panel_width);
        hash_bytes(hash, visualization.target_fps);
        hash_bytes(hash, visualization.entity_scale);
        hash_bytes(hash, visualization.picking_radius);
        hash_bytes(hash, visualization.stationary_observer_interest_radius);
        hash_bytes(hash, visualization.stationary_observer_vertical_fov_degrees);
        hash_bytes(hash, visualization.max_visible_spatial_cells);
        hash_string(hash, visualization.entity_mesh_path);
    }
}

namespace simnet
{
    std::filesystem::path default_shared_config_path()
    {
        return std::filesystem::path{SIMNET_DEFAULT_CONFIG_DIR} / "shared_default.json";
    }

    std::filesystem::path default_server_config_path()
    {
        return std::filesystem::path{SIMNET_DEFAULT_CONFIG_DIR} / "server_default.json";
    }

    std::filesystem::path default_client_config_path()
    {
        return std::filesystem::path{SIMNET_DEFAULT_CONFIG_DIR} / "client_default.json";
    }

    SharedConfig default_shared_config()
    {
        return {};
    }

    ServerConfig default_server_config()
    {
        return {};
    }

    ClientConfig default_client_config()
    {
        return {};
    }

    SharedConfig load_shared_config(std::filesystem::path const& path)
    {
        return parse_shared_config(load_json(path));
    }

    ServerConfig load_server_config(std::filesystem::path const& path)
    {
        return parse_server_config(load_json(path));
    }

    ClientConfig load_client_config(std::filesystem::path const& path)
    {
        return parse_client_config(load_json(path));
    }

    ConfigFingerprint
    fingerprint_runtime_config(SharedConfig const& shared, ServerConfig const& local) noexcept
    {
        auto hash = fnv_offset_basis;

        hash_shared_native(hash, shared);
        hash_transport_and_telemetry(hash, local.transport, local.telemetry);
        hash_bytes(hash, local.flecs.thread_count);
        hash_visualization(hash, local.visualization);
        hash_bytes(hash, local.benchmark.enabled);
        hash_bytes(hash, local.benchmark.repetitions);
        hash_bytes(hash, local.benchmark.load_ramp.enabled);
        hash_bytes(hash, local.benchmark.load_ramp.add_boids_per_step);
        hash_bytes(hash, local.benchmark.load_ramp.step_interval_seconds);
        hash_bytes(hash, local.benchmark.load_ramp.max_boids);

        return {.value = hash};
    }

    ConfigFingerprint
    fingerprint_runtime_config(SharedConfig const& shared, ClientConfig const& local) noexcept
    {
        auto hash = fnv_offset_basis;

        hash_shared_native(hash, shared);
        hash_transport_and_telemetry(hash, local.transport, local.telemetry);
        hash_string(hash, local.gameplay.role);
        for (auto const value : local.gameplay.stationary_observer_position) {
            hash_bytes(hash, value);
        }
        hash_visualization(hash, local.visualization);

        return {.value = hash};
    }

    ConfigFingerprint fingerprint_network_compatibility(SharedConfig const& config) noexcept
    {
        auto hash = fnv_offset_basis;
        hash_network_compatibility(hash, config);
        return {.value = hash};
    }
}
