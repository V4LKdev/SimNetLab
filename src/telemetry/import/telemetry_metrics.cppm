module;

#include <vector>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

/// @brief Typed measurement and legacy metric-submission contracts.
export module simnet.telemetry:metrics;

import :types;
import simnet.snapshot;

export namespace simnet
{
    /// Terminal result of one active Server replication attempt.
    enum class ServerReplicationOutcome : std::uint8_t
    {
        SnapshotExtractionFailed,
        Skipped,
        TransportSendFailed,
        Sent
    };

    /// One Server replication attempt measured by the application runtime.
    struct ServerReplicationMeasurement
    {
        Tick tick{};
        SequenceId sequence{};
        SequenceId baseline_sequence{};
        SnapshotKind snapshot_kind{SnapshotKind::FullReplace};
        ServerReplicationOutcome outcome{ServerReplicationOutcome::SnapshotExtractionFailed};

        std::uint32_t source_entity_count{};
        std::uint32_t selected_entity_count{};
        std::uint32_t upsert_count{};
        std::uint32_t delete_count{};

        /// Complete pipeline update including its application header.
        std::uint32_t encoded_update_bytes{};
        /// Encoded update bytes offered by the Server application to transport.
        std::uint32_t application_payload_bytes{};
        /// Application payload bytes accepted by transport, excluding network overhead.
        std::uint32_t transport_payload_bytes{};

        /// ensure_current_snapshot for the current authoritative tick.
        Nanoseconds snapshot_extraction_cpu_time{};
        /// Retained acknowledged-baseline lookup and fallback selection.
        Nanoseconds baseline_resolution_cpu_time{};
        /// encode_snapshot_unchecked for the selected current and baseline snapshots.
        Nanoseconds encode_cpu_time{};
        /// TransportServer::send for the encoded update.
        Nanoseconds transport_send_cpu_time{};
        /// Retained-baseline copy and newest-emitted sequence commit after send succeeds.
        Nanoseconds snapshot_retention_cpu_time{};
        /// Extraction start through the terminal outcome of this attempt.
        Nanoseconds total_replication_cpu_time{};
    };

    /// Terminal result of one Snapshot-lane packet handled by the Client runtime.
    enum class ClientReplicationOutcome : std::uint8_t
    {
        DecodeFailed,
        StaleSequenceIgnored,
        BaselineUnavailable,
        ReconstructionFailed,
        SinkApplicationFailed,
        Applied
    };

    /// One Client Snapshot-lane packet measured by the application runtime.
    ///
    /// Retained reconstructed snapshots are canonical Client state. Sink preparation and sink
    /// application are separate so pipeline-only treatments can exclude the nonauthoritative
    /// Client Flecs workload. total_receive_to_applied_cpu_time is populated only for Applied.
    struct ClientReplicationMeasurement
    {
        Tick tick{};
        SequenceId sequence{};
        SequenceId baseline_sequence{};
        SnapshotKind snapshot_kind{SnapshotKind::FullReplace};
        ClientReplicationOutcome outcome{ClientReplicationOutcome::DecodeFailed};

        /// Complete pipeline update reported by decode.
        std::uint32_t encoded_update_bytes{};
        /// Encoded update bytes submitted by the Client application to decode.
        std::uint32_t application_payload_bytes{};
        /// Application payload bytes delivered by transport, excluding network overhead.
        std::uint32_t transport_payload_bytes{};
        std::uint32_t upsert_count{};
        std::uint32_t delete_count{};
        std::uint32_t reconstructed_entity_count{};
        std::uint32_t final_sink_entity_count{};

        /// decode_update for the delivered Snapshot-lane application payload.
        Nanoseconds decode_cpu_time{};
        /// Exact retained-baseline lookup or current baseline selection.
        Nanoseconds baseline_resolution_cpu_time{};
        /// reconstruct_world_snapshot_unchecked into a new canonical snapshot.
        Nanoseconds reconstruction_cpu_time{};
        /// FullReplace construction when the sink cannot apply the decoded patch directly.
        Nanoseconds sink_preparation_cpu_time{};
        /// Client Flecs patch application including entity, component, and sink-index work.
        Nanoseconds sink_application_cpu_time{};
        /// Applied sequence, ACK tracker, retained snapshot, and runtime tick commit.
        Nanoseconds canonical_snapshot_commit_cpu_time{};
        /// Decode start through canonical commit after successful sink application.
        Nanoseconds total_receive_to_applied_cpu_time{};
    };

    /// Allocation-free current observer state for Server replication attempts.
    struct ServerReplicationMeasurements
    {
        std::uint64_t attempt_count{};
        std::uint64_t sent_count{};
        std::optional<ServerReplicationMeasurement> latest_attempt{};
        std::optional<ServerReplicationMeasurement> latest_sent{};

        void observe(ServerReplicationMeasurement const& measurement) noexcept
        {
            ++attempt_count;
            latest_attempt = measurement;
            if (measurement.outcome == ServerReplicationOutcome::Sent) {
                ++sent_count;
                latest_sent = measurement;
            }
        }
    };

    /// Allocation-free current observer state for Client replication attempts.
    struct ClientReplicationMeasurements
    {
        std::uint64_t attempt_count{};
        std::uint64_t applied_count{};
        std::optional<ClientReplicationMeasurement> latest_attempt{};
        std::optional<ClientReplicationMeasurement> latest_applied{};

        void observe(ClientReplicationMeasurement const& measurement) noexcept
        {
            ++attempt_count;
            latest_attempt = measurement;
            if (measurement.outcome == ClientReplicationOutcome::Applied) {
                ++applied_count;
                latest_applied = measurement;
            }
        }
    };

    /// A single metric value. Can be integral, floating, boolean, or a string.
    using MetricValue = std::variant<std::int64_t, std::uint64_t, double, bool, std::string>;

    /// Named value in a structured metric record.
    struct MetricField
    {
        std::string name; /// Name of the field.
        MetricValue value; /// Value of the field.
    };

    /// Generic semantic metric record for experiments and runtime events.
    struct MetricRecord
    {
        std::string stream; /// Logical metric stream name.
        Tick tick{}; /// Associated tick.
        std::vector<MetricField> fields; /// Key-value pairs for this record.
    };

    /// Submits per-tick raw metrics.
    void submit_tick_metrics(TickMetrics const& metrics);

    /// Returns buffered tick metrics and clears the buffer.
    [[nodiscard]] std::vector<TickMetrics> take_tick_metrics();

    /// Clears buffered tick metrics without returning them.
    void clear_tick_metrics();

    /// Submits a generic structured metric record.
    void submit_metric_record(MetricRecord record);

    /// Returns buffered generic metric records and clears the buffer.
    [[nodiscard]] std::vector<MetricRecord> take_metric_records();

    /// Clears buffered generic metric records without returning them.
    void clear_metric_records();

    /// Formats a metric record as 'stream tick=... field=value...'.
    [[nodiscard]] std::string format_metric_record_key_value(MetricRecord const& record);
}
