module;

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

module simnet.telemetry;

import :csv;
import :metrics;
import simnet.snapshot;

namespace
{
    using namespace simnet;

    constexpr std::size_t replication_csv_buffer_capacity = 256;
    constexpr std::size_t replication_csv_drain_threshold = 128;

    [[nodiscard]] bool ascii_alphanumeric(char value) noexcept
    {
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
               (value >= '0' && value <= '9');
    }

    [[nodiscard]] bool valid_run_id(std::string_view value) noexcept
    {
        if (value.empty() || value.size() > 64U || !ascii_alphanumeric(value.front()))
        {
            return false;
        }
        for (auto const character : value)
        {
            if (!ascii_alphanumeric(character) && character != '.' && character != '_' &&
                character != '-')
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::string_view process_role_name(EvidenceProcessRole role) noexcept
    {
        switch (role)
        {
            case EvidenceProcessRole::Server:
                return "server";
            case EvidenceProcessRole::Client:
                return "client";
        }
        return "unknown";
    }

    [[nodiscard]] std::uint64_t system_time_unix_ns()
    {
        auto const count = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::system_clock::now().time_since_epoch()
        )
                               .count();
        if (count < 0)
        {
            throw std::runtime_error("system clock precedes the Unix epoch");
        }
        return static_cast<std::uint64_t>(count);
    }

    void append_csv_text(std::string& output, std::string_view value)
    {
        auto quote = false;
        for (auto const character : value)
        {
            if (character == ',' || character == '"' || character == '\r' || character == '\n')
            {
                quote = true;
                break;
            }
        }
        if (!quote)
        {
            output.append(value);
            return;
        }
        output.push_back('"');
        for (auto const character : value)
        {
            if (character == '"')
            {
                output.push_back('"');
            }
            output.push_back(character);
        }
        output.push_back('"');
    }

    void append_csv_integer(std::string& output, bool value)
    {
        output.push_back(value ? '1' : '0');
    }

    template <typename Value> void append_csv_integer(std::string& output, Value value)
    {
        auto buffer = std::array<char, 32>{};
        auto const result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        if (result.ec != std::errc{})
        {
            throw std::runtime_error("failed to format CSV integer");
        }
        output.append(buffer.data(), result.ptr);
    }

    void append_csv_real(std::string& output, double value)
    {
        auto buffer = std::array<char, 64>{};
        auto const result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        if (result.ec != std::errc{})
        {
            throw std::runtime_error("failed to format CSV real value");
        }
        output.append(buffer.data(), result.ptr);
    }

    class CsvRow
    {
      public:
        explicit CsvRow(std::string& output) : output_(output)
        {
            output_.clear();
        }

        void text(std::string_view value)
        {
            separator();
            append_csv_text(output_, value);
        }

        template <typename Value> void integer(Value value)
        {
            separator();
            append_csv_integer(output_, value);
        }

        void real(double value)
        {
            separator();
            append_csv_real(output_, value);
        }

      private:
        void separator()
        {
            if (!first_)
            {
                output_.push_back(',');
            }
            first_ = false;
        }

        std::string& output_;
        bool first_{true};
    };

    [[nodiscard]] std::string_view snapshot_kind_name(SnapshotKind kind) noexcept
    {
        switch (kind)
        {
            case SnapshotKind::FullReplace:
                return "full_replace";
            case SnapshotKind::Patch:
                return "patch";
        }
        return "unknown";
    }

    [[nodiscard]] std::string_view server_outcome_name(ServerReplicationOutcome outcome) noexcept
    {
        switch (outcome)
        {
            case ServerReplicationOutcome::SnapshotExtractionFailed:
                return "snapshot_extraction_failed";
            case ServerReplicationOutcome::Skipped:
                return "skipped";
            case ServerReplicationOutcome::Abandoned:
                return "abandoned";
            case ServerReplicationOutcome::TransportSendFailed:
                return "transport_send_failed";
            case ServerReplicationOutcome::Sent:
                return "sent";
        }
        return "unknown";
    }

    [[nodiscard]] std::string_view client_outcome_name(ClientReplicationOutcome outcome) noexcept
    {
        switch (outcome)
        {
            case ClientReplicationOutcome::PacketIncomplete:
                return "packet_incomplete";
            case ClientReplicationOutcome::PacketDuplicate:
                return "packet_duplicate";
            case ClientReplicationOutcome::PacketInvalid:
                return "packet_invalid";
            case ClientReplicationOutcome::PacketStale:
                return "packet_stale";
            case ClientReplicationOutcome::PacketGroupExpired:
                return "packet_group_expired";
            case ClientReplicationOutcome::DeliveryMismatch:
                return "delivery_mismatch";
            case ClientReplicationOutcome::DecompressionFailed:
                return "decompression_failed";
            case ClientReplicationOutcome::DecodeFailed:
                return "decode_failed";
            case ClientReplicationOutcome::StaleSequenceIgnored:
                return "stale_sequence_ignored";
            case ClientReplicationOutcome::BaselineUnavailable:
                return "baseline_unavailable";
            case ClientReplicationOutcome::ReconstructionFailed:
                return "reconstruction_failed";
            case ClientReplicationOutcome::SinkApplicationFailed:
                return "sink_application_failed";
            case ClientReplicationOutcome::Applied:
                return "applied";
        }
        return "unknown";
    }

    void own_text(std::string_view& view, std::vector<std::string>& storage)
    {
        storage.emplace_back(view);
        view = storage.back();
    }

    void own_measurement_text(
        ServerReplicationMeasurement& measurement,
        std::vector<std::string>& storage
    )
    {
        storage.reserve(12U);
        own_text(measurement.accepted_gameplay_role, storage);
        own_text(measurement.outcome_detail, storage);
        own_text(measurement.area_of_interest_mode, storage);
        own_text(measurement.area_of_interest_source_status, storage);
        own_text(measurement.level_of_detail_mode, storage);
        own_text(measurement.representation_layout, storage);
        own_text(measurement.compression_mode, storage);
        own_text(measurement.compression_encoding, storage);
        own_text(measurement.compression_dictionary, storage);
        own_text(measurement.delivery_mode, storage);
        own_text(measurement.recovery_reason, storage);
    }

    void own_measurement_text(
        ClientReplicationMeasurement& measurement,
        std::vector<std::string>& storage
    )
    {
        storage.reserve(5U);
        own_text(measurement.outcome_detail, storage);
        own_text(measurement.compression_mode, storage);
        own_text(measurement.decompression_encoding, storage);
        own_text(measurement.decompression_result, storage);
        own_text(measurement.compression_dictionary, storage);
    }

    template <typename Measurement> struct BufferedMeasurement
    {
        BufferedMeasurement(
            Measurement const& measurement,
            EvidenceRecordTimestamp record_timestamp,
            std::uint64_t order
        )
            : value(measurement), timestamp(record_timestamp), record_order(order)
        {
            own_measurement_text(value, owned_text);
        }

        BufferedMeasurement(BufferedMeasurement const&) = delete;
        BufferedMeasurement& operator=(BufferedMeasurement const&) = delete;
        BufferedMeasurement(BufferedMeasurement&&) noexcept = default;
        BufferedMeasurement& operator=(BufferedMeasurement&&) noexcept = default;

        Measurement value{};
        // Measurement contracts use string_view so producers can report vocabulary cheaply.
        // Buffered evidence records must own those bytes because they can outlive the producer.
        std::vector<std::string> owned_text{};
        EvidenceRecordTimestamp timestamp{};
        std::uint64_t record_order{};
    };

    template <typename Measurement> struct ReplicationWriterState
    {
        explicit ReplicationWriterState(
            ReplicationCsvWriterConfig writer_config,
            std::string_view filename_prefix,
            EvidenceProcessRole required_role,
            std::string_view header
        )
            : config(std::move(writer_config))
        {
            buffer.reserve(replication_csv_buffer_capacity);
            if (!config.enabled)
            {
                return;
            }
            validate_evidence_run_context(config.run);
            if (config.run.process_role != required_role)
            {
                throw std::invalid_argument("replication CSV process role does not match writer");
            }
            std::filesystem::create_directories(config.output_directory);
            path = config.output_directory /
                   (std::string{filename_prefix} +
                    std::to_string(config.run.process_started_unix_ns) + ".csv");
            file = std::make_unique<EvidenceCsvFile>(path, header);
        }

        [[nodiscard]] bool submit(Measurement const& measurement, EvidenceRecordTimestamp timestamp)
        {
            if (!config.enabled)
            {
                return true;
            }
            if (closed)
            {
                reject("replication CSV submission attempted after close");
                return false;
            }
            if (!failure.empty())
            {
                return false;
            }
            if (buffer.size() == replication_csv_buffer_capacity)
            {
                failure = "replication CSV typed buffer overflow";
                return false;
            }
            buffer.emplace_back(measurement, timestamp, submitted);
            ++submitted;
            return true;
        }

        void reject(std::string_view message)
        {
            if (failure.empty())
            {
                failure = message;
            }
        }

        void capture_file_failure()
        {
            if (file == nullptr || file->error().empty())
            {
                return;
            }
            if (failure.find(file->error()) != std::string::npos)
            {
                return;
            }
            if (!failure.empty())
            {
                failure += ". ";
            }
            failure += file->error();
        }

        ReplicationCsvWriterConfig config{};
        std::unique_ptr<EvidenceCsvFile> file{};
        std::vector<BufferedMeasurement<Measurement>> buffer{};
        std::filesystem::path path{};
        std::string failure{};
        std::uint64_t submitted{};
        bool closed{};
    };

    void add_envelope(
        CsvRow& row,
        std::uint32_t schema_version,
        EvidenceRunContext const& run,
        EvidenceRecordTimestamp timestamp,
        std::uint64_t record_order
    )
    {
        row.integer(schema_version);
        row.text(run.run_id);
        row.text(process_role_name(run.process_role));
        row.integer(run.process_started_unix_ns);
        row.integer(timestamp.recorded_at_unix_ns);
        row.integer(timestamp.elapsed_since_process_start_ns);
        row.integer(record_order);
    }

    void format_server_row(
        std::string& output,
        EvidenceRunContext const& run,
        BufferedMeasurement<ServerReplicationMeasurement> const& entry
    )
    {
        auto row = CsvRow{output};
        add_envelope(
            row,
            server_replication_csv_schema_version,
            run,
            entry.timestamp,
            entry.record_order
        );
        auto const& value = entry.value;
        row.integer(value.runtime_config_fingerprint);
        row.integer(value.network_compatibility_fingerprint);
        row.integer(value.application_wire_fingerprint);
        row.integer(value.peer_id);
        row.text(value.accepted_gameplay_role);
        row.integer(value.tick);
        row.integer(value.sequence);
        row.integer(value.baseline_sequence);
        row.integer(value.acknowledged_sequence);
        row.integer(value.latest_submitted_sequence);
        row.text(snapshot_kind_name(value.snapshot_kind));
        row.text(server_outcome_name(value.outcome));
        row.text(value.outcome_detail);
        row.integer(value.cadence_enabled);
        row.integer(value.incremental_enabled);
        row.integer(value.quantization_enabled);
        row.integer(value.oct_heading_enabled);
        row.integer(value.delta_enabled);
        row.integer(value.delta_field_mask_enabled);
        row.integer(value.bit_packing_enabled);
        row.integer(value.cadence_interval_ticks);
        row.integer(value.incremental_limit);
        row.integer(value.incremental_cursor_before);
        row.integer(value.incremental_cursor_after);
        row.integer(value.incremental_seeded_before);
        row.integer(value.incremental_seeded_after);
        row.text(value.area_of_interest_mode);
        row.text(value.area_of_interest_source_status);
        row.text(value.level_of_detail_mode);
        row.integer(value.source_entity_count);
        row.integer(value.selected_entity_count);
        row.integer(value.upsert_count);
        row.integer(value.delete_count);
        row.integer(value.area_of_interest_candidate_count);
        row.integer(value.area_of_interest_culled_count);
        row.integer(value.lod_near_population);
        row.integer(value.lod_medium_population);
        row.integer(value.lod_far_population);
        row.integer(value.lod_near_scheduled);
        row.integer(value.lod_medium_scheduled);
        row.integer(value.lod_far_scheduled);
        row.integer(value.lod_pending_due_count);
        row.integer(value.lod_transition_count);
        row.integer(value.lod_forced_immediate_count);
        row.integer(value.lod_recovery_forced_count);
        row.integer(value.lod_deletions_bypassing_count);
        row.integer(value.lod_full_replace_override_count);
        row.integer(value.delta_candidate_count);
        row.integer(value.delta_unchanged_count);
        row.integer(value.delta_changed_existing_count);
        row.integer(value.delta_spawned_count);
        row.integer(value.delta_whole_record_existing_count);
        row.integer(value.delta_masked_existing_count);
        row.integer(value.delta_classification_field_count);
        row.integer(value.delta_position_field_count);
        row.integer(value.delta_heading_field_count);
        row.integer(value.delta_hue_field_count);
        row.integer(value.complete_record_equivalent_bytes);
        row.integer(value.sparse_record_bytes);
        row.text(value.representation_layout);
        row.integer(value.complete_record_bytes);
        row.integer(value.representation_quality_sample_count);
        row.real(value.position_error_sum);
        row.real(value.position_error_maximum);
        row.real(value.heading_error_degrees_sum);
        row.real(value.heading_error_degrees_maximum);
        row.integer(value.encoded_update_bytes);
        row.integer(value.application_payload_bytes);
        row.integer(value.transport_payload_bytes);
        row.text(value.compression_mode);
        row.text(value.compression_encoding);
        row.text(value.compression_dictionary);
        row.integer(value.compression_dictionary_id);
        row.integer(value.compression_dictionary_fingerprint);
        row.integer(value.compression_raw_fallback);
        row.integer(value.compression_input_bytes);
        row.integer(value.compression_payload_bytes);
        row.integer(value.compression_envelope_bytes);
        row.integer(value.compression_output_bytes);
        row.integer(value.compression_elapsed_time.count());
        row.integer(value.packetization_enabled);
        row.integer(value.packet_group_id);
        row.integer(value.packet_group_bytes);
        row.integer(value.packet_payload_bytes);
        row.integer(value.packet_header_bytes);
        row.integer(value.packet_chunk_count);
        row.integer(value.attempted_submissions);
        row.integer(value.accepted_submissions);
        row.text(value.delivery_mode);
        row.integer(value.recovery_active);
        row.text(value.recovery_reason);
        row.integer(value.recovery_forced_upsert_count);
        row.integer(value.recovery_forced_delete_count);
        row.integer(value.repeated_without_ack_upsert_count);
        row.integer(value.repeated_without_ack_delete_count);
        row.integer(value.submissions_since_ack_progress);
        row.integer(value.canonical_entity_count);
        row.integer(value.canonical_fingerprint);
        row.integer(value.snapshot_extraction_elapsed_time.count());
        row.integer(value.baseline_resolution_elapsed_time.count());
        row.integer(value.encode_elapsed_time.count());
        row.integer(value.transport_send_elapsed_time.count());
        row.integer(value.snapshot_retention_elapsed_time.count());
        row.integer(value.total_replication_elapsed_time.count());
    }

    void format_client_row(
        std::string& output,
        EvidenceRunContext const& run,
        std::string_view accepted_gameplay_role,
        BufferedMeasurement<ClientReplicationMeasurement> const& entry
    )
    {
        auto row = CsvRow{output};
        add_envelope(
            row,
            client_replication_csv_schema_version,
            run,
            entry.timestamp,
            entry.record_order
        );
        auto const& value = entry.value;
        row.integer(value.runtime_config_fingerprint);
        row.integer(value.network_compatibility_fingerprint);
        row.integer(value.application_wire_fingerprint);
        row.integer(value.peer_id);
        row.text(accepted_gameplay_role);
        row.integer(value.tick);
        row.integer(value.sequence);
        row.integer(value.baseline_sequence);
        row.integer(value.acknowledged_sequence_before);
        row.integer(value.received_sequence_after);
        row.integer(value.acknowledged_sequence_after);
        row.text(snapshot_kind_name(value.snapshot_kind));
        row.text(client_outcome_name(value.outcome));
        row.text(value.outcome_detail);
        row.integer(value.encoded_update_bytes);
        row.integer(value.upsert_count);
        row.integer(value.delete_count);
        row.integer(value.reconstructed_entity_count);
        row.integer(value.final_sink_entity_count);
        row.integer(value.canonical_fingerprint);
        row.integer(value.packetization_enabled);
        row.integer(value.packet_group_id);
        row.integer(value.received_outer_bytes);
        row.integer(value.group_chunk_count);
        row.integer(value.received_packet_count);
        row.integer(value.duplicate_packet_count);
        row.integer(value.invalid_packet_count);
        row.integer(value.stale_packet_count);
        row.integer(value.incomplete_packet_count);
        row.integer(value.expired_group_count);
        row.integer(value.retained_incomplete_group_count);
        row.integer(value.retained_incomplete_bytes);
        row.integer(value.packet_group_wait_available);
        row.integer(value.packet_group_wait_time.count());
        row.text(value.compression_mode);
        row.text(value.decompression_encoding);
        row.text(value.decompression_result);
        row.text(value.compression_dictionary);
        row.integer(value.compression_dictionary_id);
        row.integer(value.compression_dictionary_fingerprint);
        row.integer(value.compressed_bytes);
        row.integer(value.compression_payload_bytes);
        row.integer(value.compression_envelope_bytes);
        row.integer(value.uncompressed_bytes);
        row.integer(value.decompression_elapsed_time.count());
        row.integer(value.decode_elapsed_time.count());
        row.integer(value.baseline_resolution_elapsed_time.count());
        row.integer(value.reconstruction_elapsed_time.count());
        row.integer(value.sink_preparation_elapsed_time.count());
        row.integer(value.sink_application_elapsed_time.count());
        row.integer(value.canonical_snapshot_commit_elapsed_time.count());
        row.integer(value.decode_to_applied_elapsed_time.count());
    }

    template <typename Measurement, typename FormatEntry>
    [[nodiscard]] bool persist_rows(
        ReplicationWriterState<Measurement>& state,
        std::size_t row_capacity,
        FormatEntry format_entry
    )
    {
        if (state.file == nullptr)
        {
            state.reject("replication CSV file is unavailable");
            return false;
        }
        auto row = std::string{};
        row.reserve(row_capacity);
        for (auto const& entry : state.buffer)
        {
            format_entry(row, entry);
            if (!state.file->write_row(row))
            {
                state.capture_file_failure();
                return false;
            }
        }
        if (!state.file->flush())
        {
            state.capture_file_failure();
            return false;
        }
        state.buffer.clear();
        return true;
    }

    [[nodiscard]] bool
    persist_server_rows(ReplicationWriterState<ServerReplicationMeasurement>& state)
    {
        return persist_rows(
            state,
            2048U,
            [&state](
                std::string& row,
                BufferedMeasurement<ServerReplicationMeasurement> const& entry
            )
            {
                format_server_row(row, state.config.run, entry);
            }
        );
    }

    [[nodiscard]] bool persist_client_rows(
        ReplicationWriterState<ClientReplicationMeasurement>& state,
        std::string_view accepted_gameplay_role
    )
    {
        return persist_rows(
            state,
            1536U,
            [&state, accepted_gameplay_role](
                std::string& row,
                BufferedMeasurement<ClientReplicationMeasurement> const& entry
            )
            {
                format_client_row(row, state.config.run, accepted_gameplay_role, entry);
            }
        );
    }
}

namespace simnet
{
    EvidenceRunContext make_evidence_run_context(
        EvidenceProcessRole process_role,
        std::optional<std::string_view> supplied_run_id
    )
    {
        auto context = EvidenceRunContext{
            .process_role = process_role,
            .process_started_unix_ns = system_time_unix_ns(),
            .monotonic_start = std::chrono::steady_clock::now(),
        };
        if (supplied_run_id.has_value())
        {
            context.run_id = *supplied_run_id;
        }
        else
        {
            context.run_id = std::string{process_role_name(process_role)} + '-' +
                             std::to_string(context.process_started_unix_ns);
        }
        validate_evidence_run_context(context);
        return context;
    }

    void validate_evidence_run_context(EvidenceRunContext const& context)
    {
        if (!valid_run_id(context.run_id))
        {
            throw std::invalid_argument(
                "run ID must match [A-Za-z0-9][A-Za-z0-9._-]* and contain 1 to 64 ASCII characters"
            );
        }
    }

    EvidenceRecordTimestamp capture_evidence_record_timestamp(EvidenceRunContext const& context)
    {
        auto const elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now() - context.monotonic_start
        )
                                 .count();
        if (elapsed < 0)
        {
            throw std::runtime_error("monotonic clock precedes process evidence start");
        }
        return {
            .recorded_at_unix_ns = system_time_unix_ns(),
            .elapsed_since_process_start_ns = static_cast<std::uint64_t>(elapsed),
        };
    }

    struct EvidenceCsvFile::Impl
    {
        std::ofstream stream{};
        std::filesystem::path path{};
        std::string failure{};
        bool closed{};
    };

    EvidenceCsvFile::EvidenceCsvFile(std::filesystem::path path, std::string_view header)
        : impl_(std::make_unique<Impl>())
    {
        impl_->path = std::move(path);
        impl_->stream.open(impl_->path, std::ios::out | std::ios::noreplace);
        if (!impl_->stream)
        {
            throw std::runtime_error(
                "failed to exclusively create evidence CSV: " + impl_->path.string()
            );
        }
        impl_->stream.imbue(std::locale::classic());
        impl_->stream.write(header.data(), static_cast<std::streamsize>(header.size()));
        impl_->stream.put('\n');
        if (!impl_->stream)
        {
            impl_->failure = "failed to write evidence CSV header: " + impl_->path.string();
            static_cast<void>(close());
            throw std::runtime_error(impl_->failure);
        }
    }

    EvidenceCsvFile::~EvidenceCsvFile() noexcept
    {
        try
        {
            if (impl_)
            {
                static_cast<void>(close());
            }
        }
        catch (...)
        {
            return;
        }
    }

    bool EvidenceCsvFile::write_row(std::string_view row)
    {
        if (impl_->closed)
        {
            if (impl_->failure.empty())
            {
                impl_->failure =
                    "evidence CSV row write attempted after close: " + impl_->path.string();
            }
            return false;
        }
        if (!healthy())
        {
            return false;
        }
        impl_->stream.write(row.data(), static_cast<std::streamsize>(row.size()));
        impl_->stream.put('\n');
        if (!impl_->stream)
        {
            impl_->failure = "failed to write evidence CSV row: " + impl_->path.string();
            return false;
        }
        return true;
    }

    bool EvidenceCsvFile::flush()
    {
        if (impl_->closed)
        {
            if (impl_->failure.empty())
            {
                impl_->failure =
                    "evidence CSV flush attempted after close: " + impl_->path.string();
            }
            return false;
        }
        if (!healthy())
        {
            return false;
        }
        impl_->stream.flush();
        if (!impl_->stream)
        {
            impl_->failure = "failed to flush evidence CSV: " + impl_->path.string();
            return false;
        }
        return true;
    }

    bool EvidenceCsvFile::close()
    {
        if (impl_->closed)
        {
            return healthy();
        }
        auto const flushed = flush();
        impl_->stream.close();
        if (impl_->stream.fail() && impl_->failure.empty())
        {
            impl_->failure = "failed to close evidence CSV: " + impl_->path.string();
        }
        impl_->closed = true;
        return flushed && healthy();
    }

    bool EvidenceCsvFile::healthy() const noexcept
    {
        return impl_ != nullptr && impl_->failure.empty();
    }

    bool EvidenceCsvFile::closed() const noexcept
    {
        return impl_ == nullptr || impl_->closed;
    }

    std::string_view EvidenceCsvFile::error() const noexcept
    {
        return impl_ == nullptr ? std::string_view{} : std::string_view{impl_->failure};
    }

    std::filesystem::path const& EvidenceCsvFile::path() const noexcept
    {
        return impl_->path;
    }

    struct ServerReplicationCsvWriter::Impl
    {
        explicit Impl(ReplicationCsvWriterConfig config)
            : state(
                  std::move(config),
                  "server_replication_v3_",
                  EvidenceProcessRole::Server,
                  server_replication_csv_header_v3
              )
        {
        }

        ReplicationWriterState<ServerReplicationMeasurement> state;
    };

    ServerReplicationCsvWriter::ServerReplicationCsvWriter(ReplicationCsvWriterConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    ServerReplicationCsvWriter::~ServerReplicationCsvWriter() noexcept
    {
        try
        {
            if (impl_)
            {
                static_cast<void>(close());
            }
        }
        catch (...)
        {
            return;
        }
    }

    bool ServerReplicationCsvWriter::submit(ServerReplicationMeasurement const& measurement)
    {
        if (!enabled())
        {
            return true;
        }
        return submit(measurement, capture_evidence_record_timestamp(impl_->state.config.run));
    }

    bool ServerReplicationCsvWriter::submit(
        ServerReplicationMeasurement const& measurement,
        EvidenceRecordTimestamp timestamp
    )
    {
        if (needs_drain() && !drain())
        {
            return false;
        }
        return impl_->state.submit(measurement, timestamp);
    }

    bool ServerReplicationCsvWriter::needs_drain() const noexcept
    {
        return impl_->state.buffer.size() >= replication_csv_drain_threshold;
    }

    bool ServerReplicationCsvWriter::drain()
    {
        auto& state = impl_->state;
        if (!state.config.enabled)
        {
            return true;
        }
        if (state.closed)
        {
            state.reject("server replication CSV drain attempted after close");
            return false;
        }
        if (!state.failure.empty())
        {
            return false;
        }
        if (state.buffer.empty())
        {
            return true;
        }
        return persist_server_rows(state);
    }

    bool ServerReplicationCsvWriter::close()
    {
        auto& state = impl_->state;
        if (state.closed)
        {
            return state.failure.empty();
        }
        auto success = true;
        if (state.config.enabled && !state.buffer.empty() && !persist_server_rows(state))
        {
            success = false;
        }
        if (state.file != nullptr && !state.file->close())
        {
            state.capture_file_failure();
            success = false;
        }
        state.closed = true;
        return success && state.failure.empty();
    }

    bool ServerReplicationCsvWriter::enabled() const noexcept
    {
        return impl_->state.config.enabled;
    }

    bool ServerReplicationCsvWriter::healthy() const noexcept
    {
        return impl_->state.failure.empty();
    }

    std::size_t ServerReplicationCsvWriter::buffered_count() const noexcept
    {
        return impl_->state.buffer.size();
    }

    std::uint64_t ServerReplicationCsvWriter::submitted_count() const noexcept
    {
        return impl_->state.submitted;
    }

    std::string_view ServerReplicationCsvWriter::error() const noexcept
    {
        return impl_->state.failure;
    }

    std::filesystem::path const& ServerReplicationCsvWriter::path() const noexcept
    {
        return impl_->state.path;
    }

    struct ClientReplicationCsvWriter::Impl
    {
        explicit Impl(ReplicationCsvWriterConfig config)
            : state(
                  std::move(config),
                  "client_replication_v3_",
                  EvidenceProcessRole::Client,
                  client_replication_csv_header_v3
              )
        {
        }

        ReplicationWriterState<ClientReplicationMeasurement> state;
        std::string accepted_gameplay_role{};
    };

    ClientReplicationCsvWriter::ClientReplicationCsvWriter(ReplicationCsvWriterConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    ClientReplicationCsvWriter::~ClientReplicationCsvWriter() noexcept
    {
        try
        {
            if (impl_)
            {
                static_cast<void>(close());
            }
        }
        catch (...)
        {
            return;
        }
    }

    bool ClientReplicationCsvWriter::set_accepted_gameplay_role(std::string_view role)
    {
        if (!enabled())
        {
            return true;
        }
        auto& state = impl_->state;
        if (state.closed)
        {
            state.reject("client replication CSV accepted role set after close");
            return false;
        }
        if (!state.failure.empty())
        {
            return false;
        }
        if (role != "player" && role != "stationary_observer")
        {
            state.reject("client replication CSV received an invalid accepted role");
            return false;
        }
        if (!impl_->accepted_gameplay_role.empty() && impl_->accepted_gameplay_role != role)
        {
            state.reject("client replication CSV accepted role changed during the run");
            return false;
        }
        impl_->accepted_gameplay_role = role;
        return true;
    }

    bool ClientReplicationCsvWriter::submit(ClientReplicationMeasurement const& measurement)
    {
        if (!enabled())
        {
            return true;
        }
        return submit(measurement, capture_evidence_record_timestamp(impl_->state.config.run));
    }

    bool ClientReplicationCsvWriter::submit(
        ClientReplicationMeasurement const& measurement,
        EvidenceRecordTimestamp timestamp
    )
    {
        if (needs_drain() && !drain())
        {
            return false;
        }
        return impl_->state.submit(measurement, timestamp);
    }

    bool ClientReplicationCsvWriter::needs_drain() const noexcept
    {
        return impl_->state.buffer.size() >= replication_csv_drain_threshold;
    }

    bool ClientReplicationCsvWriter::drain()
    {
        auto& state = impl_->state;
        if (!state.config.enabled)
        {
            return true;
        }
        if (state.closed)
        {
            state.reject("client replication CSV drain attempted after close");
            return false;
        }
        if (!state.failure.empty())
        {
            return false;
        }
        if (state.buffer.empty())
        {
            return true;
        }
        if (impl_->accepted_gameplay_role.empty())
        {
            state.reject("client replication CSV has records without an accepted gameplay role");
            return false;
        }
        return persist_client_rows(state, impl_->accepted_gameplay_role);
    }

    bool ClientReplicationCsvWriter::close()
    {
        auto& state = impl_->state;
        if (state.closed)
        {
            return state.failure.empty();
        }
        auto success = true;
        if (state.config.enabled && !state.buffer.empty())
        {
            if (impl_->accepted_gameplay_role.empty())
            {
                state.reject(
                    "client replication CSV has records without an accepted gameplay role"
                );
                success = false;
            }
            else if (!persist_client_rows(state, impl_->accepted_gameplay_role))
            {
                success = false;
            }
        }
        if (state.file != nullptr && !state.file->close())
        {
            state.capture_file_failure();
            success = false;
        }
        state.closed = true;
        return success && state.failure.empty();
    }

    bool ClientReplicationCsvWriter::enabled() const noexcept
    {
        return impl_->state.config.enabled;
    }

    bool ClientReplicationCsvWriter::healthy() const noexcept
    {
        return impl_->state.failure.empty();
    }

    std::size_t ClientReplicationCsvWriter::buffered_count() const noexcept
    {
        return impl_->state.buffer.size();
    }

    std::uint64_t ClientReplicationCsvWriter::submitted_count() const noexcept
    {
        return impl_->state.submitted;
    }

    std::string_view ClientReplicationCsvWriter::error() const noexcept
    {
        return impl_->state.failure;
    }

    std::filesystem::path const& ClientReplicationCsvWriter::path() const noexcept
    {
        return impl_->state.path;
    }
}
