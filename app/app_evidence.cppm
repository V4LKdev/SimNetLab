module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

export module simnet.app_evidence;

import simnet.core;
import simnet.game_server;
import simnet.pipeline;
import simnet.telemetry;

export namespace simnet::app
{
    inline constexpr std::uint32_t server_boids_csv_schema_version = 1;
    inline constexpr std::size_t server_boids_csv_buffer_capacity = 256;
    inline constexpr std::size_t server_boids_csv_drain_threshold = 128;

    inline constexpr std::string_view server_boids_csv_header_v1 =
        "schema_version,run_id,process_role,process_started_unix_ns,recorded_at_unix_ns,"
        "elapsed_since_process_start_ns,record_order,tick,entity_count,worker_count,"
        "occupied_cell_count,max_cell_occupancy,average_entities_per_occupied_cell,"
        "raw_candidate_count_mean,raw_candidate_count_max,retained_neighbor_count_mean,"
        "retained_neighbor_count_max,neighbor_cap_hit_count,separation_neighbor_count_mean,"
        "social_neighbor_count_mean,isolated_boid_count,"
        "nearest_neighbor_distance_world_units_mean,speed_world_units_per_second_mean,"
        "speed_world_units_per_second_min,speed_world_units_per_second_max,"
        "acceleration_world_units_per_second_squared_mean,"
        "acceleration_world_units_per_second_squared_max,"
        "acceleration_saturation_count,overlap_recovery_count,hard_wall_guard_count,"
        "polarization_ratio,capture_duration_ms,grid_duration_ms,compute_duration_ms,"
        "validate_duration_ms,commit_duration_ms,progress_duration_ms";

    /// Flattens one production encode report into application-owned research evidence.
    void flatten_server_encode_report(
        ServerReplicationMeasurement& measurement,
        EncodeReport const& report,
        ClientReplicationState const& state
    ) noexcept;

    struct ServerBoidCsvWriterConfig
    {
        bool enabled{};
        std::filesystem::path output_directory{};
        EvidenceRunContext run{};
        double tick_rate_hz{};
        std::uint32_t worker_count{};
    };

    /// Application-owned bounded persistence for sampled Server boid diagnostics.
    class ServerBoidCsvWriter
    {
      public:
        explicit ServerBoidCsvWriter(ServerBoidCsvWriterConfig config);
        ~ServerBoidCsvWriter();

        ServerBoidCsvWriter(ServerBoidCsvWriter const&) = delete;
        ServerBoidCsvWriter& operator=(ServerBoidCsvWriter const&) = delete;
        ServerBoidCsvWriter(ServerBoidCsvWriter&&) = delete;
        ServerBoidCsvWriter& operator=(ServerBoidCsvWriter&&) = delete;

        [[nodiscard]] bool
        sample(Tick tick, ServerGameStepReport const& report, bool force = false);
        [[nodiscard]] bool sample(
            Tick tick,
            ServerGameStepReport const& report,
            EvidenceRecordTimestamp timestamp,
            bool force = false
        );
        [[nodiscard]] bool needs_drain() const noexcept;
        [[nodiscard]] bool drain();
        [[nodiscard]] bool close();
        [[nodiscard]] bool enabled() const noexcept;
        [[nodiscard]] bool healthy() const noexcept;
        [[nodiscard]] std::size_t buffered_count() const noexcept;
        [[nodiscard]] std::uint64_t submitted_count() const noexcept;
        [[nodiscard]] std::string_view error() const noexcept;
        [[nodiscard]] std::filesystem::path const& path() const noexcept;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
