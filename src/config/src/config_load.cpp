module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include <nlohmann/json.hpp>

module simnet.config;

import :types;

namespace
{
    using Json = nlohmann::json;

    constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ULL;
    constexpr std::uint64_t fnv_prime = 1099511628211ULL;

    template <typename Value>
    void hash_bytes(std::uint64_t& hash, Value const& value) noexcept
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

    template <typename Value>
    void read_optional(Json const& object, char const* key, Value& value)
    {
        auto const found = object.find(key);
        if (found == object.end()) {
            return;
        }

        try {
            value = found->get<Value>();
        } catch (nlohmann::json::exception const& error) {
            throw std::runtime_error(std::string { "invalid config field '" } + key + "': " + error.what());
        }
    }

    Json const* optional_object(Json const& object, char const* key)
    {
        auto const found = object.find(key);
        if (found == object.end()) {
            return nullptr;
        }
        if (!found->is_object()) {
            throw std::runtime_error(std::string { "invalid config section '" } + key + "': expected object");
        }
        return &*found;
    }

    void validate_positive(char const* name, double value)
    {
        if (value <= 0.0) {
            throw std::runtime_error(std::string { "invalid config field '" } + name + "': expected positive value");
        }
    }

    void validate_positive(char const* name, float value)
    {
        if (value <= 0.0F) {
            throw std::runtime_error(std::string { "invalid config field '" } + name + "': expected positive value");
        }
    }

    void validate_bits(char const* name, std::uint8_t value)
    {
        if (value == 0 || value > 32) {
            throw std::runtime_error(std::string { "invalid config field '" } + name + "': expected 1..32");
        }
    }

    void apply_run(Json const& json, simnet::RunConfig& config)
    {
        read_optional(json, "seed", config.seed);
        read_optional(json, "headless", config.headless);
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
    }

    void apply_pipeline(Json const& json, simnet::PipelineConfig& config)
    {
        read_optional(json, "enable_aoi", config.enable_aoi);
        read_optional(json, "enable_incremental", config.enable_incremental);
        read_optional(json, "enable_quantization", config.enable_quantization);
        read_optional(json, "enable_delta", config.enable_delta);
        read_optional(json, "enable_compression", config.enable_compression);
        read_optional(json, "position_bits", config.position_bits);
        read_optional(json, "heading_bits", config.heading_bits);

        validate_bits("pipeline.position_bits", config.position_bits);
        validate_bits("pipeline.heading_bits", config.heading_bits);
    }

    void apply_transport(Json const& json, simnet::TransportConfig& config)
    {
        read_optional(json, "host", config.host);
        read_optional(json, "port", config.port);
        read_optional(json, "max_clients", config.max_clients);

        if (config.port == 0) {
            throw std::runtime_error("invalid config field 'transport.port': expected non-zero port");
        }
    }

    void apply_render(Json const& json, simnet::RenderConfig& config)
    {
        read_optional(json, "enabled", config.enabled);
        read_optional(json, "width", config.width);
        read_optional(json, "height", config.height);

        if (config.width == 0 || config.height == 0) {
            throw std::runtime_error("invalid render dimensions: expected non-zero width and height");
        }
    }

    void apply_telemetry(Json const& json, simnet::TelemetryConfig& config)
    {
        read_optional(json, "enable_tracy", config.enable_tracy);
        read_optional(json, "enable_file_log", config.enable_file_log);
        read_optional(json, "log_directory", config.log_directory);
    }

    void apply_load_ramp(Json const& json, simnet::LoadRampConfig& config)
    {
        read_optional(json, "enabled", config.enabled);
        read_optional(json, "add_boids_per_step", config.add_boids_per_step);
        read_optional(json, "step_interval_seconds", config.step_interval_seconds);
        read_optional(json, "max_boids", config.max_boids);

        validate_positive("benchmark.load_ramp.step_interval_seconds", config.step_interval_seconds);
    }

    void apply_benchmark(Json const& json, simnet::BenchmarkScenarioConfig& config)
    {
        read_optional(json, "enabled", config.enabled);
        read_optional(json, "repetitions", config.repetitions);

        if (auto const* section = optional_object(json, "load_ramp")) {
            apply_load_ramp(*section, config.load_ramp);
        }
    }

    Json load_json(std::filesystem::path const& path)
    {
        std::ifstream file { path };
        if (!file) {
            throw std::runtime_error("failed to open config file: " + path.string());
        }

        try {
            return Json::parse(file);
        } catch (nlohmann::json::exception const& error) {
            throw std::runtime_error("failed to parse config file '" + path.string() + "': " + error.what());
        }
    }

    void apply_common(Json const& json, simnet::RunConfig& run, simnet::SimulationConfig& simulation,
        simnet::SpatialConfig& spatial, simnet::PipelineConfig& pipeline, simnet::TransportConfig& transport,
        simnet::TelemetryConfig& telemetry)
    {
        if (!json.is_object()) {
            throw std::runtime_error("invalid config root: expected object");
        }

        if (auto const* section = optional_object(json, "run")) {
            apply_run(*section, run);
        }
        if (auto const* section = optional_object(json, "simulation")) {
            apply_simulation(*section, simulation);
        }
        if (auto const* section = optional_object(json, "spatial")) {
            apply_spatial(*section, spatial);
        }
        if (auto const* section = optional_object(json, "pipeline")) {
            apply_pipeline(*section, pipeline);
        }
        if (auto const* section = optional_object(json, "transport")) {
            apply_transport(*section, transport);
        }
        if (auto const* section = optional_object(json, "telemetry")) {
            apply_telemetry(*section, telemetry);
        }
    }

    simnet::ServerConfig parse_server_config(Json const& json)
    {
        auto config = simnet::default_server_config();

        apply_common(json, config.run, config.simulation, config.spatial, config.pipeline, config.transport,
            config.telemetry);

        if (auto const* section = optional_object(json, "benchmark")) {
            apply_benchmark(*section, config.benchmark);
        }

        return config;
    }

    simnet::ClientConfig parse_client_config(Json const& json)
    {
        auto config = simnet::default_client_config();

        apply_common(json, config.run, config.simulation, config.spatial, config.pipeline, config.transport,
            config.telemetry);

        if (auto const* section = optional_object(json, "render")) {
            apply_render(*section, config.render);
        }

        return config;
    }

    void hash_network_config(std::uint64_t& hash, simnet::SimulationConfig const& simulation,
        simnet::SpatialConfig const& spatial, simnet::PipelineConfig const& pipeline) noexcept
    {
        hash_bytes(hash, simulation.tick_rate_hz);
        hash_bytes(hash, simulation.world_half);
        hash_bytes(hash, spatial.cell_size);
        hash_bytes(hash, spatial.max_neighbors);
        hash_bytes(hash, pipeline.enable_aoi);
        hash_bytes(hash, pipeline.enable_incremental);
        hash_bytes(hash, pipeline.enable_quantization);
        hash_bytes(hash, pipeline.enable_delta);
        hash_bytes(hash, pipeline.enable_compression);
        hash_bytes(hash, pipeline.position_bits);
        hash_bytes(hash, pipeline.heading_bits);
    }

    void hash_runtime_common(std::uint64_t& hash, simnet::RunConfig const& run,
        simnet::SimulationConfig const& simulation, simnet::SpatialConfig const& spatial,
        simnet::PipelineConfig const& pipeline, simnet::TransportConfig const& transport,
        simnet::TelemetryConfig const& telemetry) noexcept
    {
        hash_bytes(hash, run.seed);
        hash_bytes(hash, run.headless);
        hash_bytes(hash, simulation.tick_rate_hz);
        hash_bytes(hash, simulation.world_half);
        hash_bytes(hash, simulation.initial_boid_count);
        hash_bytes(hash, spatial.cell_size);
        hash_bytes(hash, spatial.max_neighbors);
        hash_bytes(hash, pipeline.enable_aoi);
        hash_bytes(hash, pipeline.enable_incremental);
        hash_bytes(hash, pipeline.enable_quantization);
        hash_bytes(hash, pipeline.enable_delta);
        hash_bytes(hash, pipeline.enable_compression);
        hash_bytes(hash, pipeline.position_bits);
        hash_bytes(hash, pipeline.heading_bits);
        hash_string(hash, transport.host);
        hash_bytes(hash, transport.port);
        hash_bytes(hash, transport.max_clients);
        hash_bytes(hash, telemetry.enable_tracy);
        hash_bytes(hash, telemetry.enable_file_log);
    }
}

namespace simnet
{
    ServerConfig default_server_config()
    {
        auto config = ServerConfig {};
        config.run.headless = true;
        return config;
    }

    ClientConfig default_client_config()
    {
        return {};
    }

    ServerConfig load_server_config(std::filesystem::path const& path)
    {
        return parse_server_config(load_json(path));
    }

    ClientConfig load_client_config(std::filesystem::path const& path)
    {
        return parse_client_config(load_json(path));
    }

    ConfigFingerprint fingerprint_runtime_config(ServerConfig const& config) noexcept
    {
        auto hash = fnv_offset_basis;

        hash_runtime_common(hash, config.run, config.simulation, config.spatial, config.pipeline, config.transport,
            config.telemetry);
        hash_bytes(hash, config.benchmark.enabled);
        hash_bytes(hash, config.benchmark.repetitions);
        hash_bytes(hash, config.benchmark.load_ramp.enabled);
        hash_bytes(hash, config.benchmark.load_ramp.add_boids_per_step);
        hash_bytes(hash, config.benchmark.load_ramp.step_interval_seconds);
        hash_bytes(hash, config.benchmark.load_ramp.max_boids);

        return { .value = hash };
    }

    ConfigFingerprint fingerprint_runtime_config(ClientConfig const& config) noexcept
    {
        auto hash = fnv_offset_basis;

        hash_runtime_common(hash, config.run, config.simulation, config.spatial, config.pipeline, config.transport,
            config.telemetry);
        hash_bytes(hash, config.render.enabled);

        return { .value = hash };
    }

    ConfigFingerprint fingerprint_network_compatibility(ServerConfig const& config) noexcept
    {
        auto hash = fnv_offset_basis;
        hash_network_config(hash, config.simulation, config.spatial, config.pipeline);
        return { .value = hash };
    }

    ConfigFingerprint fingerprint_network_compatibility(ClientConfig const& config) noexcept
    {
        auto hash = fnv_offset_basis;
        hash_network_config(hash, config.simulation, config.spatial, config.pipeline);
        return { .value = hash };
    }
}
