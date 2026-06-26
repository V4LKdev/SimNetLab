/// \file   telemetry.cppm
/// \brief  Deterministic, tick-aligned telemetry instrumentation layer.

module;
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <span>
#include <memory>

export module telemetry;

export struct TickId {
    std::uint64_t value = 0;

    TickId advance() noexcept
    {
        ++value;
        return *this;
    }
};

export struct TickRecord {
    std::uint64_t tick = 0;

    // wall‑clock duration of this frame (real time)
    double frame_wall_us = 0.0;

    // scale
    std::size_t entity_count = 0;

    // cpu breakdown
    double sim_time_us = 0.0;
    double serialization_us = 0.0;
    double compression_us = 0.0;
    double net_send_us = 0.0;
    double net_recv_us = 0.0;

    // network aggregates
    std::size_t bytes_sent = 0;
    std::size_t bytes_recv = 0;
    std::size_t packets_sent = 0;
    std::size_t packets_lost = 0;

    // snapshot delivery
    std::size_t snapshots_sent = 0;
    std::size_t snapshots_acked = 0;
    std::size_t snapshots_lost = 0;

    // compression (per tick aggregate)
    std::size_t uncompressed_bytes = 0;
    std::size_t compressed_bytes = 0;

    // determinism
    std::uint64_t state_hash = 0;
    double pos_drift = 0.0;
    double vel_drift = 0.0;
    double anisotropy_x = 0.0;
    double anisotropy_y = 0.0;
    double anisotropy_z = 0.0;
};

export enum class LogSeverity {
    trace = 0,
    debug = 1,
    info = 2,
    warn = 3,
    error = 4,
    critical = 5,
};

// ---------------------------------------------------------------
//  ScopedTracyZone  –  RAII profiler marker (optional)
// ---------------------------------------------------------------
export class ScopedTracyZone {
public:
    explicit ScopedTracyZone(const char *name,
                             const char *file = nullptr,
                             int line = 0);

    ~ScopedTracyZone();

    ScopedTracyZone(const ScopedTracyZone &) = delete;

    ScopedTracyZone &operator=(const ScopedTracyZone &) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------
//  TelemetryHub
// ---------------------------------------------------------------
export class TelemetryHub {
public:
    static TelemetryHub &instance() noexcept;

    void begin_run();

    void end_run();

    void set_node_id(std::string_view id);

    void set_run_id(std::string_view id);

    void set_run_tag(std::string_view tag);

    void add_technique_parameter(std::string_view key, std::string_view value);

    TickId advance_tick();

    void log_event(LogSeverity severity, std::string_view message);

    void record_frame_wall_time(double us);

    void record_entity_count(std::size_t count);

    void record_simulation_time(double us);

    void record_serialization_time(double us);

    void record_compression_stats(double cpu_us,
                                  std::size_t uncompressed,
                                  std::size_t compressed);

    void record_cpu_network_send(double us);

    void record_cpu_network_recv(double us);

    void record_network_bytes(std::size_t sent, std::size_t recv);

    void record_packet_stats(std::size_t sent, std::size_t lost);

    void record_snapshot_delivery(std::size_t sent,
                                  std::size_t acked,
                                  std::size_t lost);

    void record_divergence(double pos_drift, double vel_drift);

    void record_anisotropy_drift(double x, double y, double z);

    void record_state_hash(std::uint64_t hash);

    void enable_csv_export(std::string_view filepath);

    void enable_json_export(std::string_view filepath);

    void disable_export();

    std::span<const TickRecord> snapshot() const noexcept;

private:
    TelemetryHub();

    ~TelemetryHub();

    TelemetryHub(const TelemetryHub &) = delete;

    TelemetryHub &operator=(const TelemetryHub &) = delete;

    class Impl;
    std::unique_ptr<Impl> impl_;
};
