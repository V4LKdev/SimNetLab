module;

#include <charconv>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

module simnet.app_evidence;

namespace
{
    using namespace simnet;
    using namespace simnet::app;

    template <typename Value> void append_integer(std::string& output, Value value)
    {
        char buffer[32]{};
        auto const result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc{}) {
            throw std::runtime_error("failed to format boid CSV integer");
        }
        output.append(buffer, result.ptr);
    }

    template <typename Value> void append_floating_point(std::string& output, Value value)
    {
        char buffer[64]{};
        auto const result = std::to_chars(
            buffer,
            buffer + sizeof(buffer),
            value,
            std::chars_format::general,
            std::numeric_limits<Value>::max_digits10
        );
        if (result.ec != std::errc{}) {
            throw std::runtime_error("failed to format boid CSV floating-point value");
        }
        output.append(buffer, result.ptr);
    }

    class BoidCsvRow
    {
    public:
        explicit BoidCsvRow(std::string& output)
            : output_(output)
        {
            output_.clear();
        }

        void text(std::string_view value)
        {
            separator();
            output_.append(value);
        }

        template <typename Value> void integer(Value value)
        {
            separator();
            append_integer(output_, value);
        }

        template <typename Value> void floating_point(Value value)
        {
            separator();
            append_floating_point(output_, value);
        }

    private:
        void separator()
        {
            if (!first_) {
                output_.push_back(',');
            }
            first_ = false;
        }

        std::string& output_;
        bool first_{true};
    };

    struct ServerBoidCsvRecord
    {
        Tick tick{};
        std::uint32_t entity_count{};
        std::uint32_t worker_count{};
        std::uint32_t occupied_cell_count{};
        std::uint32_t max_cell_occupancy{};
        float average_occupied_cell_load{};
        double raw_candidates_mean{};
        std::uint32_t raw_candidates_max{};
        double retained_neighbors_mean{};
        std::uint32_t retained_neighbors_max{};
        std::uint32_t neighbor_cap_hit_count{};
        double separation_neighbors_mean{};
        double social_neighbors_mean{};
        std::uint32_t isolated_boid_count{};
        double nearest_neighbor_distance_mean{};
        double speed_mean{};
        float speed_min{};
        float speed_max{};
        double acceleration_mean{};
        float acceleration_max{};
        std::uint32_t acceleration_saturation_count{};
        std::uint32_t overlap_recovery_count{};
        std::uint32_t hard_wall_guard_count{};
        float polarization{};
        double capture_ms{};
        double grid_ms{};
        double compute_ms{};
        double validate_ms{};
        double commit_ms{};
        double progress_ms{};
        EvidenceRecordTimestamp timestamp{};
        std::uint64_t record_order{};
    };

    [[nodiscard]] ServerBoidCsvRecord make_record(
        Tick tick,
        std::uint32_t worker_count,
        ServerGameStepReport const& report,
        EvidenceRecordTimestamp timestamp,
        std::uint64_t record_order
    )
    {
        auto const& value = report.diagnostics;
        auto const& phase = report.phases;
        return {
            .tick = tick,
            .entity_count = report.entity_count,
            .worker_count = worker_count,
            .occupied_cell_count = value.grid.occupied_cell_count,
            .max_cell_occupancy = value.grid.max_cell_occupancy,
            .average_occupied_cell_load = value.grid.average_occupied_cell_load,
            .raw_candidates_mean = value.raw_candidates_mean,
            .raw_candidates_max = value.raw_candidates_max,
            .retained_neighbors_mean = value.retained_neighbors_mean,
            .retained_neighbors_max = value.retained_neighbors_max,
            .neighbor_cap_hit_count = value.neighbor_cap_hit_count,
            .separation_neighbors_mean = value.separation_neighbors_mean,
            .social_neighbors_mean = value.social_neighbors_mean,
            .isolated_boid_count = value.isolated_boid_count,
            .nearest_neighbor_distance_mean = value.nearest_neighbor_distance_mean,
            .speed_mean = value.speed_mean,
            .speed_min = value.speed_min,
            .speed_max = value.speed_max,
            .acceleration_mean = value.acceleration_mean,
            .acceleration_max = value.acceleration_max,
            .acceleration_saturation_count = value.acceleration_saturation_count,
            .overlap_recovery_count = value.overlap_recovery_count,
            .hard_wall_guard_count = value.hard_wall_guard_count,
            .polarization = value.polarization,
            .capture_ms = phase.capture_ms,
            .grid_ms = phase.grid_ms,
            .compute_ms = phase.compute_ms,
            .validate_ms = phase.validate_ms,
            .commit_ms = phase.commit_ms,
            .progress_ms = phase.progress_ms,
            .timestamp = timestamp,
            .record_order = record_order,
        };
    }

    void
    format_row(std::string& output, EvidenceRunContext const& run, ServerBoidCsvRecord const& value)
    {
        auto row = BoidCsvRow{output};
        row.integer(server_boids_csv_schema_version);
        row.text(run.run_id);
        row.text("server");
        row.integer(run.process_started_unix_ns);
        row.integer(value.timestamp.recorded_at_unix_ns);
        row.integer(value.timestamp.elapsed_since_process_start_ns);
        row.integer(value.record_order);
        row.integer(value.tick);
        row.integer(value.entity_count);
        row.integer(value.worker_count);
        row.integer(value.occupied_cell_count);
        row.integer(value.max_cell_occupancy);
        row.floating_point(value.average_occupied_cell_load);
        row.floating_point(value.raw_candidates_mean);
        row.integer(value.raw_candidates_max);
        row.floating_point(value.retained_neighbors_mean);
        row.integer(value.retained_neighbors_max);
        row.integer(value.neighbor_cap_hit_count);
        row.floating_point(value.separation_neighbors_mean);
        row.floating_point(value.social_neighbors_mean);
        row.integer(value.isolated_boid_count);
        row.floating_point(value.nearest_neighbor_distance_mean);
        row.floating_point(value.speed_mean);
        row.floating_point(value.speed_min);
        row.floating_point(value.speed_max);
        row.floating_point(value.acceleration_mean);
        row.floating_point(value.acceleration_max);
        row.integer(value.acceleration_saturation_count);
        row.integer(value.overlap_recovery_count);
        row.integer(value.hard_wall_guard_count);
        row.floating_point(value.polarization);
        row.floating_point(value.capture_ms);
        row.floating_point(value.grid_ms);
        row.floating_point(value.compute_ms);
        row.floating_point(value.validate_ms);
        row.floating_point(value.commit_ms);
        row.floating_point(value.progress_ms);
    }
}

namespace simnet::app
{
    struct ServerBoidCsvWriter::Impl
    {
        explicit Impl(ServerBoidCsvWriterConfig writer_config)
            : config(std::move(writer_config))
            , interval(std::max<Tick>(1U, static_cast<Tick>(std::llround(config.tick_rate_hz))))
            , last_tick(std::numeric_limits<Tick>::max())
        {
            buffer.reserve(server_boids_csv_buffer_capacity);
            if (!config.enabled) {
                return;
            }
            validate_evidence_run_context(config.run);
            if (config.run.process_role != EvidenceProcessRole::Server) {
                throw std::invalid_argument("boid CSV process role must be Server");
            }
            std::filesystem::create_directories(config.output_directory);
            path = config.output_directory
                / ("server_boids_v1_" + std::to_string(config.run.process_started_unix_ns)
                   + ".csv");
            file.emplace(path, server_boids_csv_header_v1);
        }

        void reject(std::string_view message)
        {
            if (failure.empty()) {
                failure = message;
            }
        }

        void capture_file_failure()
        {
            if (!file.has_value() || file->error().empty()) {
                return;
            }
            if (failure.find(file->error()) != std::string::npos) {
                return;
            }
            if (!failure.empty()) {
                failure += ". ";
            }
            failure += file->error();
        }

        [[nodiscard]] bool persist_buffer()
        {
            auto row = std::string{};
            row.reserve(768);
            for (auto const& entry : buffer) {
                format_row(row, config.run, entry);
                if (!file->write_row(row)) {
                    capture_file_failure();
                    return false;
                }
            }
            if (!file->flush()) {
                capture_file_failure();
                return false;
            }
            buffer.clear();
            return true;
        }

        ServerBoidCsvWriterConfig config{};
        std::optional<EvidenceCsvFile> file{};
        std::vector<ServerBoidCsvRecord> buffer{};
        std::filesystem::path path{};
        std::string failure{};
        Tick interval{1U};
        Tick last_tick{};
        std::uint64_t submitted{};
        bool closed{};
    };

    ServerBoidCsvWriter::ServerBoidCsvWriter(ServerBoidCsvWriterConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    ServerBoidCsvWriter::~ServerBoidCsvWriter()
    {
        if (impl_) {
            static_cast<void>(close());
        }
    }

    bool ServerBoidCsvWriter::sample(Tick tick, ServerGameStepReport const& report, bool force)
    {
        if (!enabled()) {
            return true;
        }
        if (impl_->closed) {
            impl_->reject("Server boid CSV submission attempted after close");
            return false;
        }
        if (!impl_->failure.empty()) {
            return false;
        }
        if ((!force && tick % impl_->interval != 0U) || impl_->last_tick == tick) {
            return true;
        }
        return sample(tick, report, capture_evidence_record_timestamp(impl_->config.run), force);
    }

    bool ServerBoidCsvWriter::sample(
        Tick tick,
        ServerGameStepReport const& report,
        EvidenceRecordTimestamp timestamp,
        bool force
    )
    {
        if (!impl_->config.enabled) {
            return true;
        }
        if (impl_->closed) {
            impl_->reject("Server boid CSV submission attempted after close");
            return false;
        }
        if (!impl_->failure.empty()) {
            return false;
        }
        if ((!force && tick % impl_->interval != 0U) || impl_->last_tick == tick) {
            return true;
        }
        if (impl_->buffer.size() == server_boids_csv_buffer_capacity) {
            impl_->failure = "Server boid CSV typed buffer overflow";
            return false;
        }
        impl_->buffer.push_back(
            make_record(tick, impl_->config.worker_count, report, timestamp, impl_->submitted)
        );
        impl_->last_tick = tick;
        ++impl_->submitted;
        return true;
    }

    bool ServerBoidCsvWriter::needs_drain() const noexcept
    {
        return impl_->buffer.size() >= server_boids_csv_drain_threshold;
    }

    bool ServerBoidCsvWriter::drain()
    {
        if (!impl_->config.enabled) {
            return true;
        }
        if (impl_->closed) {
            impl_->reject("Server boid CSV drain attempted after close");
            return false;
        }
        if (!impl_->failure.empty()) {
            return false;
        }
        if (impl_->buffer.empty()) {
            return true;
        }
        return impl_->persist_buffer();
    }

    bool ServerBoidCsvWriter::close()
    {
        if (impl_->closed) {
            return impl_->failure.empty();
        }
        auto success = true;
        if (impl_->config.enabled && !impl_->buffer.empty() && !impl_->persist_buffer()) {
            success = false;
        }
        if (impl_->file.has_value() && !impl_->file->close()) {
            impl_->capture_file_failure();
            success = false;
        }
        impl_->closed = true;
        return success && impl_->failure.empty();
    }

    bool ServerBoidCsvWriter::enabled() const noexcept
    {
        return impl_->config.enabled;
    }

    bool ServerBoidCsvWriter::healthy() const noexcept
    {
        return impl_->failure.empty();
    }

    std::size_t ServerBoidCsvWriter::buffered_count() const noexcept
    {
        return impl_->buffer.size();
    }

    std::uint64_t ServerBoidCsvWriter::submitted_count() const noexcept
    {
        return impl_->submitted;
    }

    std::string_view ServerBoidCsvWriter::error() const noexcept
    {
        return impl_->failure;
    }

    std::filesystem::path const& ServerBoidCsvWriter::path() const noexcept
    {
        return impl_->path;
    }
}
