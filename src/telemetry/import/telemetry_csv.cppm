module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

/// @brief Versioned CSV evidence lifecycle and replication writers.
export module simnet.telemetry:csv;

import :metrics;

export namespace simnet
{
    inline constexpr std::uint32_t replication_csv_schema_version = 1;
    inline constexpr std::size_t replication_csv_buffer_capacity = 256;
    inline constexpr std::size_t replication_csv_drain_threshold = 128;

    inline constexpr std::string_view server_replication_csv_header_v1
        = "schema_version,run_id,process_role,process_started_unix_ns,recorded_at_unix_ns,"
          "elapsed_since_process_start_ns,record_order,tick,sequence,baseline_sequence,"
          "snapshot_kind,outcome,source_entity_count,selected_entity_count,upsert_count,"
          "delete_count,encoded_update_bytes,application_payload_bytes,transport_payload_bytes,"
          "snapshot_extraction_cpu_ns,baseline_resolution_cpu_ns,encode_cpu_ns,"
          "transport_send_cpu_ns,snapshot_retention_cpu_ns,total_replication_cpu_ns";

    inline constexpr std::string_view client_replication_csv_header_v1
        = "schema_version,run_id,process_role,process_started_unix_ns,recorded_at_unix_ns,"
          "elapsed_since_process_start_ns,record_order,accepted_gameplay_role,tick,sequence,"
          "baseline_sequence,snapshot_kind,outcome,encoded_update_bytes,"
          "application_payload_bytes,transport_payload_bytes,upsert_count,delete_count,"
          "reconstructed_entity_count,final_sink_entity_count,decode_cpu_ns,"
          "baseline_resolution_cpu_ns,reconstruction_cpu_ns,sink_preparation_cpu_ns,"
          "sink_application_cpu_ns,canonical_snapshot_commit_cpu_ns,"
          "total_receive_to_applied_cpu_ns";

    enum class EvidenceProcessRole : std::uint8_t
    {
        Server,
        Client
    };

    struct EvidenceRunContext
    {
        std::string run_id{};
        EvidenceProcessRole process_role{EvidenceProcessRole::Server};
        std::uint64_t process_started_unix_ns{};
        std::chrono::steady_clock::time_point monotonic_start{};
    };

    struct EvidenceRecordTimestamp
    {
        std::uint64_t recorded_at_unix_ns{};
        std::uint64_t elapsed_since_process_start_ns{};
    };

    /// A supplied run ID is validated and preserved. An omitted ID is process-local and cannot
    /// establish that independently started processes belong to the same experiment.
    [[nodiscard]] EvidenceRunContext make_evidence_run_context(
        EvidenceProcessRole process_role,
        std::optional<std::string_view> supplied_run_id = std::nullopt
    );

    /// Rejects aggregate contexts whose run ID is not a safe 1 to 64 character token.
    void validate_evidence_run_context(EvidenceRunContext const& context);

    /// Captures wall and monotonic time after the caller's measured stage has ended.
    [[nodiscard]] EvidenceRecordTimestamp
    capture_evidence_record_timestamp(EvidenceRunContext const& context);

    /// Exclusively creates one CSV file and owns its checked write and close lifecycle.
    class EvidenceCsvFile
    {
    public:
        EvidenceCsvFile(std::filesystem::path path, std::string_view header);
        ~EvidenceCsvFile();

        EvidenceCsvFile(EvidenceCsvFile const&) = delete;
        EvidenceCsvFile& operator=(EvidenceCsvFile const&) = delete;
        EvidenceCsvFile(EvidenceCsvFile&&) = delete;
        EvidenceCsvFile& operator=(EvidenceCsvFile&&) = delete;

        [[nodiscard]] bool write_row(std::string_view row);
        [[nodiscard]] bool flush();
        [[nodiscard]] bool close();
        [[nodiscard]] bool healthy() const noexcept;
        [[nodiscard]] bool closed() const noexcept;
        [[nodiscard]] std::string_view error() const noexcept;
        [[nodiscard]] std::filesystem::path const& path() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    struct ReplicationCsvWriterConfig
    {
        bool enabled{};
        std::filesystem::path output_directory{};
        EvidenceRunContext run{};
    };

    /// Application-owned bounded persistence for every Server replication measurement.
    class ServerReplicationCsvWriter
    {
    public:
        explicit ServerReplicationCsvWriter(ReplicationCsvWriterConfig config);
        ~ServerReplicationCsvWriter();

        ServerReplicationCsvWriter(ServerReplicationCsvWriter const&) = delete;
        ServerReplicationCsvWriter& operator=(ServerReplicationCsvWriter const&) = delete;
        ServerReplicationCsvWriter(ServerReplicationCsvWriter&&) = delete;
        ServerReplicationCsvWriter& operator=(ServerReplicationCsvWriter&&) = delete;

        [[nodiscard]] bool submit(ServerReplicationMeasurement const& measurement);
        [[nodiscard]] bool
        submit(ServerReplicationMeasurement const& measurement, EvidenceRecordTimestamp timestamp);
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

    /// Application-owned bounded persistence for every Client replication measurement.
    class ClientReplicationCsvWriter
    {
    public:
        explicit ClientReplicationCsvWriter(ReplicationCsvWriterConfig config);
        ~ClientReplicationCsvWriter();

        ClientReplicationCsvWriter(ClientReplicationCsvWriter const&) = delete;
        ClientReplicationCsvWriter& operator=(ClientReplicationCsvWriter const&) = delete;
        ClientReplicationCsvWriter(ClientReplicationCsvWriter&&) = delete;
        ClientReplicationCsvWriter& operator=(ClientReplicationCsvWriter&&) = delete;

        /// Accepts only the authoritative role carried by JoinAccepted.
        [[nodiscard]] bool set_accepted_gameplay_role(std::string_view role);
        [[nodiscard]] bool submit(ClientReplicationMeasurement const& measurement);
        [[nodiscard]] bool
        submit(ClientReplicationMeasurement const& measurement, EvidenceRecordTimestamp timestamp);
        [[nodiscard]] bool needs_drain() const noexcept;
        /// Rows without an accepted gameplay role cannot be claimed and remain buffered on failure.
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
