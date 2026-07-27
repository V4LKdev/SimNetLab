module;

#include <cstdint>
#include <string>

/// @brief Runtime configuration data contracts.
export module simnet.config:types;

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
        /// Initial boid count. Zero is valid for empty-world and edge-case runs.
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
        std::uint32_t max_payload_bytes { 1200 };
        std::string send_size_policy { "enforce_limit" };
        std::string snapshot_delivery { "reliable_sequenced" };
    };

    /// Server-local Flecs scheduler settings.
    struct FlecsConfig
    {
        std::uint32_t thread_count { 1 };
    };

    /// Local visualization settings.
    struct VisualizationConfig
    {
        bool enabled {};
        std::uint32_t window_width { 1800 };
        std::uint32_t window_height { 1080 };
        std::uint32_t panel_width { 360 };
        std::uint32_t target_fps { 60 };
        float entity_scale { 1.0F };
        float picking_radius { 1.0F };
        float debug_observer_interest_radius { 150.0F };
        float debug_observer_vertical_fov_degrees { 60.0F };
        std::uint32_t max_visible_spatial_cells { 2048 };
        std::string entity_mesh_path {};
    };

    /// Logging settings and reserved metrics-export vocabulary.
    struct TelemetryConfig
    {
        bool console_log_enabled { true };
        bool file_log_enabled { true };
        std::string log_directory { "logs" };
        std::string min_level { "info" };
        bool metrics_csv_enabled { true };   /// Parsed; export is not implemented.
        bool metrics_json_enabled { false }; /// Parsed; export is not implemented.
    };

    /// Reserved load-ramp settings. Application behavior is not implemented.
    struct LoadRampConfig
    {
        bool enabled { false };
        std::uint32_t add_boids_per_step { 500 };
        double step_interval_seconds { 30.0 };
        std::uint32_t max_boids { 1000000 };
    };

    /// Reserved benchmark settings. Application behavior is not implemented.
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
        TransportConfig transport {};
        FlecsConfig flecs {};
        VisualizationConfig visualization {};
        TelemetryConfig telemetry {};
        BenchmarkScenarioConfig benchmark {};
    };

    /// Client-local runtime configuration.
    struct ClientConfig
    {
        TransportConfig transport {};
        VisualizationConfig visualization {};
        TelemetryConfig telemetry {};
    };

    /// Stable non-cryptographic config fingerprint.
    struct ConfigFingerprint
    {
        std::uint64_t value {};
    };
}
