module;

#include <array>
#include <cstdint>
#include <optional>
#include <string>

/// @brief Runtime configuration data contracts.
export module simnet.config:types;

export namespace simnet
{
    /// Shared deterministic run configuration.
    struct RunConfig
    {
        std::uint64_t seed{12345};
    };

    /// Simulation world and tick settings.
    struct SimulationConfig
    {
        double tick_rate_hz{60.0};
        float world_half{220.0F};
        /// Initial boid count. Zero is valid for empty-world and edge-case runs.
        std::uint32_t initial_boid_count{1000};
    };

    /// Spatial acceleration settings.
    struct SpatialConfig
    {
        float cell_size{18.0F};
        std::uint32_t max_neighbors{64};
    };

    /// One bounded authoritative Player influence on Boid acceleration.
    struct PlayerInfluenceForceConfig
    {
        bool enabled{};
        float radius{};
        float max_acceleration{};
    };

    /// Deterministic authoritative flocking parameters.
    struct BoidsConfig
    {
        bool enable_separation{true};
        bool enable_alignment{true};
        bool enable_cohesion{true};
        bool enable_containment{true};
        bool enable_wander{true};
        bool enable_hue_assimilation{true};
        bool enable_hue_drift{true};
        float min_speed{6.0F};
        float cruise_speed{8.0F};
        float max_speed{10.0F};
        float max_acceleration{12.0F};
        float separation_radius{3.6F};
        float alignment_radius{12.0F};
        float cohesion_radius{24.0F};
        float field_of_view_degrees{240.0F};
        float containment_prediction_seconds{0.75F};
        float containment_margin{22.5F};
        float separation_acceleration{10.0F};
        float containment_acceleration{9.0F};
        float alignment_acceleration{1.2F};
        float cohesion_acceleration{2.0F};
        float wander_acceleration{0.55F};
        float wander_frequency_hz{0.35F};
        float hue_assimilation_rate{0.05F};
        float hue_drift_rate{0.008F};
        PlayerInfluenceForceConfig player_lure{};
        PlayerInfluenceForceConfig player_predator{};
    };

    /// Deterministic authoritative player movement parameters.
    struct PlayerConfig
    {
        float cruise_speed{8.0F};
        float boost_speed{14.0F};
        float slow_speed{3.0F};
        float speed_change_rate{12.0F};
        float yaw_acceleration_degrees{360.0F};
        float pitch_acceleration_degrees{300.0F};
        float yaw_damping{8.0F};
        float pitch_damping{8.0F};
        float max_yaw_rate_degrees{120.0F};
        float max_pitch_rate_degrees{90.0F};
        float pitch_limit_degrees{80.0F};
    };

    /// Snapshot processing pipeline settings.
    struct AreaOfInterestConfig
    {
        std::string mode{"none"};
        float radius{};
        float fov_degrees{};
    };

    /// Shared temporal distance-LOD treatment settings.
    struct LevelOfDetailConfig
    {
        std::string mode{"none"};
        float near_distance{};
        float medium_distance{};
        std::uint32_t medium_interval_ticks{};
        std::uint32_t far_interval_ticks{};
    };

    /// Snapshot processing pipeline settings.
    struct PipelineConfig
    {
        std::uint32_t send_interval_ticks{1U};
        bool enable_incremental{false};
        bool enable_quantization{false};
        bool enable_oct_heading{false};
        bool enable_delta{false};
        bool enable_delta_field_mask{false};
        bool enable_bit_packing{false};
        AreaOfInterestConfig area_of_interest{};
        LevelOfDetailConfig level_of_detail{};
    };

    /// Optional deterministic synthetic authoritative workload policy.
    struct SyntheticWorkloadConfig
    {
        std::string pattern{"random_uniform"};
        double entity_change_fraction{1.0};
        std::string field_change_mode{"all"};
    };

    /// Shared opaque application byte-group packetization settings.
    struct PacketizationConfig
    {
        bool enabled{true};
        std::uint32_t max_payload_bytes{1200U};
        std::uint32_t max_update_bytes{4U * 1024U * 1024U};
        std::uint32_t max_chunks_per_update{4096U};
        std::uint32_t max_in_flight_updates{4U};
        std::uint32_t max_incomplete_bytes{8U * 1024U * 1024U};
        std::uint32_t reassembly_timeout_ms{5000U};
    };

    /// Shared opaque byte compression settings.
    struct CompressionConfig
    {
        std::string mode{"none"};
        int level{1};
    };

    /// Shared snapshot delivery and bounded recovery treatment.
    struct SnapshotDeliveryConfig
    {
        std::string mode{"reliable_sequenced"};
        std::uint32_t full_replace_after_unacknowledged_updates{32U};
    };

    /// Network transport settings.
    struct TransportConfig
    {
        std::string host{"127.0.0.1"};
        std::uint16_t port{7777};
        std::uint32_t max_clients{1};
        std::uint32_t max_payload_bytes{1200};
    };

    /// Server-local Flecs scheduler settings.
    struct FlecsConfig
    {
        std::uint32_t thread_count{1};
    };

    /// Client-local role requested after transport session readiness.
    struct GameplayConfig
    {
        std::string role{"stationary_observer"};
        std::array<float, 3> stationary_observer_position{};
    };

    /// Local visualization settings.
    struct VisualizationConfig
    {
        bool enabled{};
        bool interpolation_enabled{true};
        std::uint32_t window_width{1800};
        std::uint32_t window_height{1080};
        std::uint32_t panel_width{420};
        std::uint32_t target_fps{60};
        float entity_scale{1.0F};
        float picking_radius{1.0F};
        float stationary_observer_interest_radius{150.0F};
        float stationary_observer_vertical_fov_degrees{60.0F};
        std::uint32_t max_visible_spatial_cells{2048};
        std::string entity_mesh_path{};
    };

    /// Logging and replication evidence settings.
    struct TelemetryConfig
    {
        bool console_log_enabled{true};
        bool file_log_enabled{true};
        /// Directory for enabled log files and CSV evidence files.
        std::string log_directory{"logs"};
        std::string min_level{"info"};
        /// Enables the role-specific replication CSV.
        bool metrics_csv_enabled{true};
    };

    /// Shared network-compatible runtime configuration.
    struct SharedConfig
    {
        RunConfig run{};
        SimulationConfig simulation{};
        SpatialConfig spatial{};
        BoidsConfig boids{};
        PlayerConfig player{};
        std::optional<SyntheticWorkloadConfig> synthetic{};
        PipelineConfig pipeline{};
        SnapshotDeliveryConfig snapshot_delivery{};
        CompressionConfig compression{};
        PacketizationConfig packetization{};
    };

    /// Server-local runtime configuration.
    struct ServerConfig
    {
        TransportConfig transport{};
        FlecsConfig flecs{};
        VisualizationConfig visualization{};
        TelemetryConfig telemetry{};
    };

    /// Client-local runtime configuration.
    struct ClientConfig
    {
        TransportConfig transport{};
        GameplayConfig gameplay{};
        VisualizationConfig visualization{};
        TelemetryConfig telemetry{};
    };

    /// Stable non-cryptographic config fingerprint.
    struct ConfigFingerprint
    {
        std::uint64_t value{};
    };
}
