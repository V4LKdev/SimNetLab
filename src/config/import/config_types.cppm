module;

#include <cstdint>
#include <string>

export module simnet.config:types;

import simnet.core;

export namespace simnet
{
    /// Shared deterministic run configuration.
    struct RunConfig
    {
        std::uint64_t seed { 12345 };
    };

    /// Simulation world and tick settings.
    struct SimulationConfig
    {
        double tick_rate_hz { 60.0 };
        float world_half { 400.0F };
        std::uint32_t initial_boid_count { 1000 };
    };

    /// Spatial acceleration settings.
    struct SpatialConfig
    {
        float cell_size { 20.0F };
        std::uint32_t max_neighbors { 64 };
    };

    /// Snapshot processing pipeline settings.
    struct PipelineConfig
    {
        bool enable_aoi { false };
        bool enable_incremental { false };
        bool enable_quantization { false };
        bool enable_delta { false };
        bool enable_compression { false };
        std::uint8_t position_bits { 16 };
        std::uint8_t heading_bits { 16 };
    };

    /// Network transport settings.
    struct TransportConfig
    {
        std::string host { "127.0.0.1" };
        std::uint16_t port { 7777 };
        std::uint32_t max_clients { 8 };
    };

    /// Visualization settings.
    struct RenderConfig
    {
        bool enabled { true };
        std::uint32_t width { 1280 };
        std::uint32_t height { 720 };
    };

    /// Logging and profiling settings.
    struct TelemetryConfig
    {
        bool enable_tracy { false };
        bool enable_file_log { true };
        std::string log_directory { "logs" };
    };

    /// Declarative load ramp settings.
    struct LoadRampConfig
    {
        bool enabled { false };
        std::uint32_t add_boids_per_step { 500 };
        double step_interval_seconds { 30.0 };
        std::uint32_t max_boids { 1000000 };
    };

    /// Benchmark scenario settings.
    struct BenchmarkScenarioConfig
    {
        bool enabled { false };
        std::uint32_t repetitions { 10 };
        LoadRampConfig load_ramp {};
    };

    /// Shared network-compatible runtime configuration.
    struct SharedConfig
    {
        RunConfig run {};
        SimulationConfig simulation {};
        SpatialConfig spatial {};
        PipelineConfig pipeline {};
    };

    /// Server-local runtime configuration.
    struct ServerConfig
    {
        bool headless { true };
        TransportConfig transport {};
        TelemetryConfig telemetry {};
        BenchmarkScenarioConfig benchmark {};
    };

    /// Client-local runtime configuration.
    struct ClientConfig
    {
        bool headless { false };
        TransportConfig transport {};
        RenderConfig render {};
        TelemetryConfig telemetry {};
    };

    /// Stable non-cryptographic config fingerprint.
    struct ConfigFingerprint
    {
        std::uint64_t value {};
    };
}
