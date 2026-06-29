/// \file   telemetry.cppm
/// \brief  Deterministic, tick-aligned telemetry instrumentation layer.
///
/// \details
/// Provides tick identifiers, per-tick records, severity levels, and the
/// TelemetryHub singleton for collecting and exporting simulation metrics.

module;
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <span>
#include <memory>
#include <string>

export module simnet.telemetry;

/// Monotonically increasing tick identifier.
export struct TickId {
    /// Current tick value.
    std::uint64_t value = 0;

    /// Increments the tick and returns a reference.
    TickId advance() noexcept
    {
        ++value;
        return *this;
    }
};

/// Per-tick telemetry row collected on the main thread.
export struct TickRecord {
    /// Tick number for this row.
    std::uint64_t tick = 0;

    /// Wall-clock time spent in the main loop for this tick, in microseconds (realtime).
    double frame_wall_us = 0.0;

    /// Number of active entities in the simulation this tick.
    std::size_t entity_count = 0;

    /// CPU time spent in the simulation step(s) for this tick.
    double sim_time_us = 0.0;
    /// CPU time spent serializing the simulation state for networking.
    double serialization_us = 0.0;
    /// CPU time spent compressing the serialized state.
    double compression_us = 0.0;
    /// CPU time spent sending network packets.
    double net_send_us = 0.0;
    /// CPU time spent receiving network packets.
    double net_recv_us = 0.0;

    /// Network bytes sent and received for this tick.
    std::size_t bytes_sent = 0;
    std::size_t bytes_recv = 0;
    std::size_t packets_sent = 0;
    std::size_t packets_lost = 0;

    /// Snapshot delivery counts for this tick.
    std::size_t snapshots_sent = 0;
    std::size_t snapshots_acked = 0;
    std::size_t snapshots_lost = 0;

    /// Compression statistics for this tick. (aggregate)
    std::size_t uncompressed_bytes = 0;
    std::size_t compressed_bytes = 0;

    /// Pipeline statistics for this tick.
    std::size_t pipeline_input_rows = 0;
    std::size_t pipeline_selected_rows = 0;
    std::size_t pipeline_output_rows = 0;
    std::size_t pipeline_canonical_bytes = 0;
    std::size_t pipeline_encoded_bytes = 0;
    std::size_t pipeline_stage_failures = 0;
    std::size_t pipeline_decode_failures = 0;
    std::size_t pipeline_baseline_hits = 0;
    std::size_t pipeline_baseline_misses = 0;
    std::size_t pipeline_stale_baselines = 0;

    /// Deterministic hash of the simulation state for this tick.
    std::uint64_t state_hash = 0;
    /// Position drift between the local and authoritative simulation states.
    double pos_drift = 0.0;
    /// Velocity drift between the local and authoritative simulation states. (Maybe change to heading drift)
    double vel_drift = 0.0;
    /// Anisotropic drift per axis component.
    double anisotropy_x = 0.0;
    double anisotropy_y = 0.0;
    double anisotropy_z = 0.0;

    /// Number of edges in the neighbor cache.
    std::size_t neighbor_cache_edges = 0;
    /// Number of distance checks performed.
    std::size_t neighbor_cache_checks = 0;
    /// Total number of spatial grid cells.
    std::size_t spatial_grid_cells = 0;
    /// Number of occupied spatial grid cells.
    std::size_t spatial_grid_occupied_cells = 0;

    /// Average flock speed this tick.
    double flock_avg_speed = 0.0;
    /// Average flock steering this tick.
    double flock_avg_steer = 0.0;
    /// Average flock polarization this tick.
    double flock_polarization = 0.0;
};

/// Event severity used for in-run logging
export enum class LogSeverity {
    trace = 0,
    debug = 1,
    info = 2,
    warn = 3,
    error = 4,
    critical = 5,
};

// --- TelemetryHub ---

/// Singleton that collects tick‑aligned telemetry data and optionally
/// exports it to CSV or JSON.
export class TelemetryHub {
public:
    /// Returns the singleton instance of the TelemetryHub.
    static TelemetryHub &instance() noexcept;

    /// Starts a new benchmark run, resetting counters.
    void begin_run();

    /// Ends the current benchmark run, exporting data if enabled.
    void end_run();

    /// Sets the human-readable node identifier.
    void set_node_id(std::string_view id);

    /// Sets the run identifier.
    void set_run_id(std::string_view id);

    /// Clears any previously set identifier.
    void clear_run_id();

    /// Sets an arbitrary run tag for filtering.
    void set_run_tag(std::string_view tag);

    /// Adds a key-value pair to the technique parameters for this run.
    void add_technique_parameter(std::string_view key, std::string_view value);

    /// Advances the tick counter and returns the new tick identifier.
    TickId advance_tick();

    /// Logs an event with the specified severity and message.
    void log_event(LogSeverity severity, std::string_view message);

    /// Records the wall-clock time spent in the main loop for this tick, in microseconds.
    void record_frame_wall_time(double us);

    /// Records the number of active entities in the simulation for this tick.
    void record_entity_count(std::size_t count);

    /// Records the CPU time spent in the simulation step(s) for this tick, in microseconds.
    void record_simulation_time(double us);

    /// Records the CPU time spent serializing the simulation state for networking, in microseconds.
    void record_serialization_time(double us);

    /// Records compression CPU time and byte counts.
    void record_compression_stats(double cpu_us,
                                  std::size_t uncompressed,
                                  std::size_t compressed);

    /// Records pipeline row counts for input, selected, and output stages.
    void record_pipeline_rows(std::size_t input,
                              std::size_t selected,
                              std::size_t output);

    /// Records pipeline byte counts for canonical and encoded data.
    void record_pipeline_bytes(std::size_t canonical,
                               std::size_t encoded);

    /// Records pipeline failure counts for stage and decode failures.
    void record_pipeline_failures(std::size_t stage_failures,
                                  std::size_t decode_failures);

    /// Records pipeline baseline statistics for hits, misses, and stale baselines.
    void record_pipeline_baselines(std::size_t hits,
                                   std::size_t misses,
                                   std::size_t stale);

    /// Records CPU time spent sending network packets, in microseconds.
    void record_cpu_network_send(double us);

    /// Records CPU time spent receiving network packets, in microseconds.
    void record_cpu_network_recv(double us);

    /// Records the number of bytes sent and received over the network.
    void record_network_bytes(std::size_t sent, std::size_t recv);

    /// Records the number of packets sent and lost over the network.
    void record_packet_stats(std::size_t sent, std::size_t lost);

    /// Records snapshot delivery statistics for this tick.
    void record_snapshot_delivery(std::size_t sent,
                                  std::size_t acked,
                                  std::size_t lost);

    /// Records the position and velocity drift between the local and authoritative simulation states.
    void record_divergence(double pos_drift, double vel_drift);

    /// Records the anisotropic drift components.
    void record_anisotropy_drift(double x, double y, double z);

    /// Records the deterministic hash of the simulation state for this tick.
    void record_state_hash(std::uint64_t hash);

    /// Records neighbor cache and spatial grid diagnostics.
    void record_neighbor_cache(std::size_t edges,
                               std::size_t distance_checks,
                               std::size_t grid_cells,
                               std::size_t occupied_grid_cells);

    /// Records flock aggregate statistics.
    void record_flock_stats(double avg_speed,
                            double avg_steer,
                            double polarization);

    /// Enables CSV export to the specified file path.
    void enable_csv_export(std::string_view filepath);

    /// Enables JSON export to the specified file path.
    void enable_json_export(std::string_view filepath);

    /// Disables any previously enabled export.
    void disable_export();

    /// Returns a snapshot of all tick records collected so far (read-only).
    [[nodiscard]] std::span<const TickRecord> snapshot() const noexcept;

    /// Non-copyable. (Singleton)
    TelemetryHub(const TelemetryHub &) = delete;

    TelemetryHub &operator=(const TelemetryHub &) = delete;

private:
    TelemetryHub();

    ~TelemetryHub();

    /// Pointer-to-implementation for TelemetryHub internals.
    class Impl;
    std::unique_ptr<Impl> impl_;
};
