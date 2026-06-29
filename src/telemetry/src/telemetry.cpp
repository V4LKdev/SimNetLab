module;
#include <vector>
#include <string>
#include <span>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <utility>
#include <string_view>
#include <random>
#include <sstream>
#include <thread>
#include <spdlog/spdlog.h>
#include "telemetry/trace.hpp"

module simnet.telemetry;

namespace telemetry::detail {
    void write_csv(std::string_view filepath,
                   std::span<const TickRecord> records,
                   std::string_view run_tag,
                   std::string_view node_id,
                   std::string_view run_id,
                   const std::vector<std::pair<std::string, std::string> > &parameters);

    void write_json(std::string_view filepath,
                    std::span<const TickRecord> records,
                    std::string_view run_tag,
                    std::string_view node_id,
                    std::string_view run_id,
                    const std::vector<std::pair<std::string, std::string> > &parameters);
}

// Helper – generate a short unique id
static std::string make_run_id()
{
    thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << dist(rng) << '-' << dist(rng);
    return oss.str();
}

class TelemetryHub::Impl {
public:
    static constexpr std::size_t default_capacity = 1'000'000;

    std::vector<TickRecord> records;
    TickId current_tick;
    bool run_active = false;

    std::string csv_filepath;
    std::string json_filepath;

    std::string run_tag;
    std::string node_id;
    std::string run_id;
    bool run_id_pinned = false;
    std::thread::id owner_thread{};
    std::vector<std::pair<std::string, std::string> > parameters;

    explicit Impl()
    {
        records.reserve(default_capacity);
    }

    void assert_owner_thread() const noexcept
    {
        assert(owner_thread == std::this_thread::get_id() &&
            "TelemetryHub TickRecord mutation is main-thread only");
    }

    TickRecord *current_record() noexcept
    {
        if (!run_active) {
            return nullptr;
        }
        assert_owner_thread();
        if (records.empty()) {
            return nullptr;
        }
        return &records.back();
    }

    const TickRecord *current_record_for_owner_log() const noexcept
    {
        if (owner_thread != std::this_thread::get_id() || !run_active || records.empty()) {
            return nullptr;
        }
        return &records.back();
    }
};

// Singleton
TelemetryHub &TelemetryHub::instance() noexcept
{
    static TelemetryHub hub;
    return hub;
}

TelemetryHub::TelemetryHub() : impl_(std::make_unique<Impl>())
{
}

TelemetryHub::~TelemetryHub() = default;

void TelemetryHub::begin_run()
{
    auto &r = *impl_;
    r.records.clear();
    r.current_tick = TickId{};
    r.run_active = true;
    r.owner_thread = std::this_thread::get_id();
    r.run_tag.clear();
    r.parameters.clear();
    if (!r.run_id_pinned) {
        r.run_id = make_run_id();
    }
}

void TelemetryHub::end_run()
{
    auto &r = *impl_;
    if (r.run_active) {
        r.assert_owner_thread();
    }
    r.run_active = false;
    auto records = snapshot();
    if (!r.csv_filepath.empty())
        telemetry::detail::write_csv(r.csv_filepath, records, r.run_tag,
                                     r.node_id, r.run_id, r.parameters);
    if (!r.json_filepath.empty())
        telemetry::detail::write_json(r.json_filepath, records, r.run_tag,
                                      r.node_id, r.run_id, r.parameters);
}

void TelemetryHub::set_node_id(std::string_view id) { impl_->node_id = id; }

void TelemetryHub::set_run_id(std::string_view id)
{
    impl_->run_id = id;
    impl_->run_id_pinned = true;
}

void TelemetryHub::clear_run_id()
{
    impl_->run_id.clear();
    impl_->run_id_pinned = false;
}

void TelemetryHub::set_run_tag(std::string_view tag) { impl_->run_tag = tag; }

void TelemetryHub::add_technique_parameter(std::string_view key,
                                           std::string_view value)
{
    impl_->parameters.emplace_back(key, value);
}

TickId TelemetryHub::advance_tick()
{
    auto &r = *impl_;
    r.assert_owner_thread();
    assert(r.run_active && "advance_tick() called before begin_run()");
    r.records.push_back(TickRecord{});
    auto &rec = r.records.back();
    rec.tick = r.current_tick.value;
    SIMNET_TRACE_PLOT("tick", static_cast<double>(rec.tick));
    return r.current_tick.advance();
}

void TelemetryHub::log_event(LogSeverity severity, std::string_view message)
{
    spdlog::level::level_enum lvl{};
    switch (severity) {
        case LogSeverity::trace: lvl = spdlog::level::trace;
            break;
        case LogSeverity::debug: lvl = spdlog::level::debug;
            break;
        case LogSeverity::info: lvl = spdlog::level::info;
            break;
        case LogSeverity::warn: lvl = spdlog::level::warn;
            break;
        case LogSeverity::error: lvl = spdlog::level::err;
            break;
        case LogSeverity::critical: lvl = spdlog::level::critical;
            break;
    }
    if (const auto *record = impl_->current_record_for_owner_log()) {
        spdlog::log(lvl, "[tick:{}] {}", record->tick, message);
    } else {
        spdlog::log(lvl, "{}", message);
    }
}

// Metric recording
void TelemetryHub::record_frame_wall_time(double us)
{
    SIMNET_TRACE_PLOT("frame_wall_us", us);
    if (auto *record = impl_->current_record()) record->frame_wall_us = us;
}

void TelemetryHub::record_entity_count(std::size_t count)
{
    SIMNET_TRACE_PLOT("entity_count", static_cast<double>(count));
    if (auto *record = impl_->current_record()) record->entity_count = count;
}

void TelemetryHub::record_simulation_time(double us)
{
    SIMNET_TRACE_PLOT("sim_time_us", us);
    if (auto *record = impl_->current_record()) record->sim_time_us = us;
}

void TelemetryHub::record_serialization_time(double us)
{
    if (auto *record = impl_->current_record()) record->serialization_us = us;
}

void TelemetryHub::record_compression_stats(double cpu_us,
                                            std::size_t uncompressed,
                                            std::size_t compressed)
{
    auto *record = impl_->current_record();
    if (record == nullptr) return;
    auto &rec = *record;
    rec.compression_us = cpu_us;
    rec.uncompressed_bytes = uncompressed;
    rec.compressed_bytes = compressed;
}

void TelemetryHub::record_pipeline_rows(std::size_t input,
                                        std::size_t selected,
                                        std::size_t output)
{
    auto *record = impl_->current_record();
    if (record == nullptr) return;
    auto &rec = *record;
    rec.pipeline_input_rows = input;
    rec.pipeline_selected_rows = selected;
    rec.pipeline_output_rows = output;
}

void TelemetryHub::record_pipeline_bytes(std::size_t canonical,
                                         std::size_t encoded)
{
    auto *record = impl_->current_record();
    if (record == nullptr) return;
    auto &rec = *record;
    rec.pipeline_canonical_bytes = canonical;
    rec.pipeline_encoded_bytes = encoded;
}

void TelemetryHub::record_pipeline_failures(std::size_t stage_failures,
                                            std::size_t decode_failures)
{
    auto *record = impl_->current_record();
    if (record == nullptr) return;
    auto &rec = *record;
    rec.pipeline_stage_failures = stage_failures;
    rec.pipeline_decode_failures = decode_failures;
}

void TelemetryHub::record_pipeline_baselines(std::size_t hits,
                                             std::size_t misses,
                                             std::size_t stale)
{
    auto *record = impl_->current_record();
    if (record == nullptr) return;
    auto &rec = *record;
    rec.pipeline_baseline_hits = hits;
    rec.pipeline_baseline_misses = misses;
    rec.pipeline_stale_baselines = stale;
}

void TelemetryHub::record_cpu_network_send(double us)
{
    if (auto *record = impl_->current_record()) record->net_send_us = us;
}

void TelemetryHub::record_cpu_network_recv(double us)
{
    if (auto *record = impl_->current_record()) record->net_recv_us = us;
}

void TelemetryHub::record_network_bytes(std::size_t sent, std::size_t recv)
{
    auto *record = impl_->current_record();
    if (record == nullptr) return;
    auto &rec = *record;
    rec.bytes_sent = sent;
    rec.bytes_recv = recv;
}

void TelemetryHub::record_packet_stats(std::size_t sent, std::size_t lost)
{
    auto *record = impl_->current_record();
    if (record == nullptr) return;
    auto &rec = *record;
    rec.packets_sent = sent;
    rec.packets_lost = lost;
}

void TelemetryHub::record_snapshot_delivery(std::size_t sent,
                                            std::size_t acked,
                                            std::size_t lost)
{
    auto *record = impl_->current_record();
    if (record == nullptr) return;
    auto &rec = *record;
    rec.snapshots_sent = sent;
    rec.snapshots_acked = acked;
    rec.snapshots_lost = lost;
}

void TelemetryHub::record_divergence(double pos_drift, double vel_drift)
{
    auto *record = impl_->current_record();
    if (record == nullptr) return;
    auto &rec = *record;
    rec.pos_drift = pos_drift;
    rec.vel_drift = vel_drift;
}

void TelemetryHub::record_anisotropy_drift(double x, double y, double z)
{
    auto *record = impl_->current_record();
    if (record == nullptr) return;
    auto &rec = *record;
    rec.anisotropy_x = x;
    rec.anisotropy_y = y;
    rec.anisotropy_z = z;
}

void TelemetryHub::record_state_hash(std::uint64_t hash)
{
    if (auto *record = impl_->current_record()) record->state_hash = hash;
}

void TelemetryHub::record_neighbor_cache(std::size_t edges,
                                         std::size_t distance_checks,
                                         std::size_t grid_cells,
                                         std::size_t occupied_grid_cells)
{
    auto *record = impl_->current_record();
    if (record == nullptr) return;

    record->neighbor_cache_edges = edges;
    record->neighbor_cache_checks = distance_checks;
    record->spatial_grid_cells = grid_cells;
    record->spatial_grid_occupied_cells = occupied_grid_cells;

    SIMNET_TRACE_PLOT("neighbor_cache_edges", static_cast<double>(edges));
    SIMNET_TRACE_PLOT("neighbor_cache_checks", static_cast<double>(distance_checks));
    SIMNET_TRACE_PLOT("spatial_grid_cells", static_cast<double>(grid_cells));
    SIMNET_TRACE_PLOT("spatial_grid_occupied_cells", static_cast<double>(occupied_grid_cells));
    SIMNET_TRACE_PLOT(
        "spatial_grid_occupancy_pct",
        grid_cells == 0 ? 0.0 : static_cast<double>(occupied_grid_cells) * 100.0 / static_cast<double>(grid_cells));
}

void TelemetryHub::record_flock_stats(
    double avg_speed,
    double avg_steer,
    double polarization)
{
    auto *record = impl_->current_record();
    if (record != nullptr) {
        record->flock_avg_speed = avg_speed;
        record->flock_avg_steer = avg_steer;
        record->flock_polarization = polarization;
    }

    SIMNET_TRACE_PLOT("flock_avg_speed", avg_speed);
    SIMNET_TRACE_PLOT("flock_avg_steer", avg_steer);
    SIMNET_TRACE_PLOT("flock_polarization", polarization);
}

// Export control
void TelemetryHub::enable_csv_export(std::string_view filepath)
{
    impl_->csv_filepath = filepath;
}

void TelemetryHub::enable_json_export(std::string_view filepath)
{
    impl_->json_filepath = filepath;
}

void TelemetryHub::disable_export()
{
    impl_->csv_filepath.clear();
    impl_->json_filepath.clear();
}

// Snapshot
std::span<const TickRecord> TelemetryHub::snapshot() const noexcept
{
    return {impl_->records.data(), impl_->records.size()};
}
