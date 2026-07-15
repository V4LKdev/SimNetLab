module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
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

    // FNV-1a is enough here for stable traceability fingerprints.
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

    void validate_root(Json const& json)
    {
        if (!json.is_object()) {
            throw std::runtime_error("invalid config root: expected object");
        }
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

    void validate_non_zero(char const* name, std::uint32_t value)
    {
        if (value == 0U) {
            throw std::runtime_error(std::string { "invalid config field '" } + name + "': expected non-zero value");
        }
    }

    void validate_one_of(char const* name, std::string_view value, std::initializer_list<std::string_view> allowed)
    {
        for (auto const option : allowed) {
            if (value == option) {
                return;
            }
        }
        throw std::runtime_error(std::string { "invalid config field '" } + name + "': unsupported value");
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
        read_optional(json, "backend", config.backend);
        read_optional(json, "host", config.host);
        read_optional(json, "port", config.port);
        read_optional(json, "max_clients", config.max_clients);
        read_optional(json, "max_payload_bytes", config.max_payload_bytes);
        read_optional(json, "send_size_policy", config.send_size_policy);
        read_optional(json, "snapshot_delivery", config.snapshot_delivery);

        validate_one_of("transport.backend", config.backend, { "enet", "local_ipc" });
        if (config.port == 0) {
            throw std::runtime_error("invalid config field 'transport.port': expected non-zero port");
        }
        validate_non_zero("transport.max_clients", config.max_clients);
        validate_non_zero("transport.max_payload_bytes", config.max_payload_bytes);
        validate_one_of(
            "transport.send_size_policy",
            config.send_size_policy,
            { "enforce_limit", "allow_backend_fragmentation" });
        validate_one_of(
            "transport.snapshot_delivery",
            config.snapshot_delivery,
            {
                "reliable_sequenced",
                "unreliable_sequenced",
                "unreliable_unsequenced",
                "unreliable_fragmented",
            });
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
        read_optional(json, "tracy_enabled", config.tracy_enabled);
        read_optional(json, "console_log_enabled", config.console_log_enabled);
        read_optional(json, "file_log_enabled", config.file_log_enabled);
        read_optional(json, "log_directory", config.log_directory);
        read_optional(json, "min_level", config.min_level);
        read_optional(json, "metrics_csv_enabled", config.metrics_csv_enabled);
        read_optional(json, "metrics_json_enabled", config.metrics_json_enabled);
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
        if (auto const* section = optional_object(json, "pipeline")) {
            apply_pipeline(*section, config.pipeline);
        }

        return config;
    }

    simnet::ServerConfig parse_server_config(Json const& json)
    {
        validate_root(json);

        // Server-local config owns transport, telemetry, and benchmark knobs.
        auto config = simnet::default_server_config();

        read_optional(json, "headless", config.headless);
        if (auto const* section = optional_object(json, "transport")) {
            apply_transport(*section, config.transport);
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

        read_optional(json, "headless", config.headless);
        if (auto const* section = optional_object(json, "transport")) {
            apply_transport(*section, config.transport);
        }
        if (auto const* section = optional_object(json, "render")) {
            apply_render(*section, config.render);
        }
        if (auto const* section = optional_object(json, "telemetry")) {
            apply_telemetry(*section, config.telemetry);
        }

        return config;
    }

    void hash_shared(std::uint64_t& hash, simnet::SharedConfig const& config) noexcept
    {
        // Network compatibility is based only on shared deterministic settings.
        hash_bytes(hash, config.run.seed);
        hash_bytes(hash, config.simulation.tick_rate_hz);
        hash_bytes(hash, config.simulation.world_half);
        hash_bytes(hash, config.simulation.initial_boid_count);
        hash_bytes(hash, config.spatial.cell_size);
        hash_bytes(hash, config.spatial.max_neighbors);
        hash_bytes(hash, config.pipeline.enable_aoi);
        hash_bytes(hash, config.pipeline.enable_incremental);
        hash_bytes(hash, config.pipeline.enable_quantization);
        hash_bytes(hash, config.pipeline.enable_delta);
        hash_bytes(hash, config.pipeline.enable_compression);
        hash_bytes(hash, config.pipeline.position_bits);
        hash_bytes(hash, config.pipeline.heading_bits);
    }

    void hash_transport_and_telemetry(std::uint64_t& hash, simnet::TransportConfig const& transport,
        simnet::TelemetryConfig const& telemetry) noexcept
    {
        hash_string(hash, transport.backend);
        hash_string(hash, transport.host);
        hash_bytes(hash, transport.port);
        hash_bytes(hash, transport.max_clients);
        hash_bytes(hash, transport.max_payload_bytes);
        hash_string(hash, transport.send_size_policy);
        hash_string(hash, transport.snapshot_delivery);
        hash_bytes(hash, telemetry.tracy_enabled);
        hash_bytes(hash, telemetry.console_log_enabled);
        hash_bytes(hash, telemetry.file_log_enabled);
        hash_string(hash, telemetry.min_level);
        hash_bytes(hash, telemetry.metrics_csv_enabled);
        hash_bytes(hash, telemetry.metrics_json_enabled);
    }
}

namespace simnet
{
    std::filesystem::path default_shared_config_path()
    {
        return std::filesystem::path { SIMNET_DEFAULT_CONFIG_DIR } / "shared_default.json";
    }

    std::filesystem::path default_server_config_path()
    {
        return std::filesystem::path { SIMNET_DEFAULT_CONFIG_DIR } / "server_default.json";
    }

    std::filesystem::path default_client_config_path()
    {
        return std::filesystem::path { SIMNET_DEFAULT_CONFIG_DIR } / "client_default.json";
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

    ConfigFingerprint fingerprint_runtime_config(SharedConfig const& shared, ServerConfig const& local) noexcept
    {
        auto hash = fnv_offset_basis;

        hash_shared(hash, shared);
        hash_bytes(hash, local.headless);
        hash_transport_and_telemetry(hash, local.transport, local.telemetry);
        hash_bytes(hash, local.benchmark.enabled);
        hash_bytes(hash, local.benchmark.repetitions);
        hash_bytes(hash, local.benchmark.load_ramp.enabled);
        hash_bytes(hash, local.benchmark.load_ramp.add_boids_per_step);
        hash_bytes(hash, local.benchmark.load_ramp.step_interval_seconds);
        hash_bytes(hash, local.benchmark.load_ramp.max_boids);

        return { .value = hash };
    }

    ConfigFingerprint fingerprint_runtime_config(SharedConfig const& shared, ClientConfig const& local) noexcept
    {
        auto hash = fnv_offset_basis;

        hash_shared(hash, shared);
        hash_bytes(hash, local.headless);
        hash_transport_and_telemetry(hash, local.transport, local.telemetry);
        hash_bytes(hash, local.render.enabled);

        return { .value = hash };
    }

    ConfigFingerprint fingerprint_network_compatibility(SharedConfig const& config) noexcept
    {
        auto hash = fnv_offset_basis;
        hash_shared(hash, config);
        return { .value = hash };
    }
}
