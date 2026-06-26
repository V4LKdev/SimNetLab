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
#include <stdexcept>     // std::logic_error
#include <spdlog/spdlog.h>  // now used internally

module telemetry;

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
    std::vector<std::pair<std::string, std::string> > parameters;

    explicit Impl()
    {
        records.reserve(default_capacity);
    }

    // check pre‑condition and throw if violated
    void ensure_tick_ready()
    {
        if (records.empty()) {
            spdlog::error("telemetry: record called before advance_tick()");
            throw std::logic_error("telemetry: record called before advance_tick()");
        }
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

// Lifecycle
void TelemetryHub::begin_run()
{
    auto &r = *impl_;
    r.records.clear();
    r.current_tick = TickId{};
    r.run_active = true;
    r.run_tag.clear();
    r.parameters.clear();
    // auto‑generate a run ID if none set explicitly
    if (r.run_id.empty())
        r.run_id = make_run_id();
}

void TelemetryHub::end_run()
{
    auto &r = *impl_;
    r.run_active = false;
    auto records = snapshot();
    if (!r.csv_filepath.empty())
        telemetry::detail::write_csv(r.csv_filepath, records, r.run_tag,
                                     r.node_id, r.run_id, r.parameters);
    if (!r.json_filepath.empty())
        telemetry::detail::write_json(r.json_filepath, records, r.run_tag,
                                      r.node_id, r.run_id, r.parameters);
}

// Identifiers
void TelemetryHub::set_node_id(std::string_view id) { impl_->node_id = id; }
void TelemetryHub::set_run_id(std::string_view id) { impl_->run_id = id; }

// Metadata
void TelemetryHub::set_run_tag(std::string_view tag) { impl_->run_tag = tag; }

void TelemetryHub::add_technique_parameter(std::string_view key,
                                           std::string_view value)
{
    impl_->parameters.emplace_back(key, value);
}

// Tick flow
TickId TelemetryHub::advance_tick()
{
    auto &r = *impl_;
    assert(r.run_active && "advance_tick() called before begin_run()");
    r.records.push_back(TickRecord{});
    auto &rec = r.records.back();
    rec.tick = r.current_tick.value;
    return r.current_tick.advance();
}

// Log event (synchronised with tick)
void TelemetryHub::log_event(LogSeverity severity, std::string_view message)
{
    impl_->ensure_tick_ready();
    // format: [tick:tickvalue] message
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
    spdlog::log(lvl, "[tick:{}] {}", impl_->current_tick.value, message);
}

// Metric recording
void TelemetryHub::record_frame_wall_time(double us)
{
    impl_->ensure_tick_ready();
    impl_->records.back().frame_wall_us = us;
}

void TelemetryHub::record_entity_count(std::size_t count)
{
    impl_->ensure_tick_ready();
    impl_->records.back().entity_count = count;
}

void TelemetryHub::record_simulation_time(double us)
{
    impl_->ensure_tick_ready();
    impl_->records.back().sim_time_us = us;
}

void TelemetryHub::record_serialization_time(double us)
{
    impl_->ensure_tick_ready();
    impl_->records.back().serialization_us = us;
}

void TelemetryHub::record_compression_stats(double cpu_us,
                                            std::size_t uncompressed,
                                            std::size_t compressed)
{
    impl_->ensure_tick_ready();
    auto &rec = impl_->records.back();
    rec.compression_us = cpu_us;
    rec.uncompressed_bytes = uncompressed;
    rec.compressed_bytes = compressed;
}

void TelemetryHub::record_cpu_network_send(double us)
{
    impl_->ensure_tick_ready();
    impl_->records.back().net_send_us = us;
}

void TelemetryHub::record_cpu_network_recv(double us)
{
    impl_->ensure_tick_ready();
    impl_->records.back().net_recv_us = us;
}

void TelemetryHub::record_network_bytes(std::size_t sent, std::size_t recv)
{
    impl_->ensure_tick_ready();
    auto &rec = impl_->records.back();
    rec.bytes_sent = sent;
    rec.bytes_recv = recv;
}

void TelemetryHub::record_packet_stats(std::size_t sent, std::size_t lost)
{
    impl_->ensure_tick_ready();
    auto &rec = impl_->records.back();
    rec.packets_sent = sent;
    rec.packets_lost = lost;
}

void TelemetryHub::record_snapshot_delivery(std::size_t sent,
                                            std::size_t acked,
                                            std::size_t lost)
{
    impl_->ensure_tick_ready();
    auto &rec = impl_->records.back();
    rec.snapshots_sent = sent;
    rec.snapshots_acked = acked;
    rec.snapshots_lost = lost;
}

void TelemetryHub::record_divergence(double pos_drift, double vel_drift)
{
    impl_->ensure_tick_ready();
    auto &rec = impl_->records.back();
    rec.pos_drift = pos_drift;
    rec.vel_drift = vel_drift;
}

void TelemetryHub::record_anisotropy_drift(double x, double y, double z)
{
    impl_->ensure_tick_ready();
    auto &rec = impl_->records.back();
    rec.anisotropy_x = x;
    rec.anisotropy_y = y;
    rec.anisotropy_z = z;
}

void TelemetryHub::record_state_hash(std::uint64_t hash)
{
    impl_->ensure_tick_ready();
    impl_->records.back().state_hash = hash;
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

// Tracy dummy
#ifndef TELEMETRY_TRACY
struct ScopedTracyZone::Impl {
};
ScopedTracyZone::ScopedTracyZone(const char *, const char *, int)
    : impl_{std::make_unique<Impl>()}
{
}
ScopedTracyZone::~ScopedTracyZone() = default;
#endif
