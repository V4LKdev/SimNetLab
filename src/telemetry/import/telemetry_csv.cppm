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
    inline constexpr std::uint32_t server_replication_csv_schema_version = 3;
    inline constexpr std::uint32_t client_replication_csv_schema_version = 2;
    inline constexpr std::size_t replication_csv_buffer_capacity = 256;
    inline constexpr std::size_t replication_csv_drain_threshold = 128;

    inline constexpr std::string_view server_replication_csv_header_v3 =
        "schema_version,run_id,process_role,process_started_unix_ns,recorded_at_unix_ns,"
        "elapsed_since_process_start_ns,record_order,runtime_config_fingerprint,"
        "network_compatibility_fingerprint,application_wire_fingerprint,peer_id,"
        "accepted_gameplay_role,tick,sequence,baseline_sequence,acknowledged_sequence,"
        "latest_submitted_sequence,snapshot_kind,outcome,outcome_detail,"
        "cadence_enabled,incremental_enabled,quantization_enabled,oct_heading_enabled,"
        "delta_enabled,delta_field_mask_enabled,bit_packing_enabled,cadence_interval_ticks,"
        "incremental_limit,"
        "incremental_cursor_before,incremental_cursor_after,incremental_seeded_before,"
        "incremental_seeded_after,area_of_interest_mode,area_of_interest_source_status,"
        "level_of_detail_mode,source_entity_count,selected_entity_count,upsert_count,"
        "delete_count,area_of_interest_candidate_count,area_of_interest_culled_count,"
        "lod_near_population,lod_medium_population,lod_far_population,lod_near_scheduled,"
        "lod_medium_scheduled,lod_far_scheduled,lod_pending_due_count,lod_transition_count,"
        "lod_forced_immediate_count,lod_recovery_forced_count,"
        "lod_deletions_bypassing_count,lod_full_replace_override_count,"
        "delta_candidate_count,delta_unchanged_count,delta_changed_existing_count,"
        "delta_spawned_count,delta_whole_record_existing_count,"
        "delta_masked_existing_count,delta_classification_field_count,"
        "delta_position_field_count,delta_heading_field_count,delta_hue_field_count,"
        "complete_record_equivalent_bytes,sparse_record_bytes,representation_layout,"
        "complete_record_bytes,representation_quality_sample_count,position_error_sum,"
        "position_error_maximum,heading_error_degrees_sum,"
        "heading_error_degrees_maximum,encoded_update_bytes,application_payload_bytes,"
        "transport_payload_bytes,compression_mode,compression_encoding,"
        "compression_dictionary,compression_dictionary_id,"
        "compression_dictionary_fingerprint,compression_raw_fallback,"
        "compression_input_bytes,compression_payload_bytes,compression_envelope_bytes,"
        "compression_output_bytes,compression_elapsed_ns,packetization_enabled,packet_group_id,"
        "packet_group_bytes,packet_payload_bytes,packet_header_bytes,packet_chunk_count,"
        "attempted_submissions,accepted_submissions,delivery_mode,recovery_active,"
        "recovery_reason,recovery_forced_upsert_count,recovery_forced_delete_count,"
        "repeated_without_ack_upsert_count,repeated_without_ack_delete_count,"
        "submissions_since_ack_progress,canonical_entity_count,canonical_fingerprint,"
        "snapshot_extraction_elapsed_ns,baseline_resolution_elapsed_ns,encode_elapsed_ns,"
        "transport_send_elapsed_ns,snapshot_retention_elapsed_ns,total_replication_elapsed_ns";

    inline constexpr std::string_view client_replication_csv_header_v2 =
        "schema_version,run_id,process_role,process_started_unix_ns,recorded_at_unix_ns,"
        "elapsed_since_process_start_ns,record_order,runtime_config_fingerprint,"
        "network_compatibility_fingerprint,application_wire_fingerprint,peer_id,"
        "accepted_gameplay_role,tick,sequence,baseline_sequence,"
        "acknowledged_sequence_before,received_sequence_after,"
        "acknowledged_sequence_after,snapshot_kind,outcome,outcome_detail,"
        "encoded_update_bytes,upsert_count,delete_count,reconstructed_entity_count,"
        "final_sink_entity_count,canonical_fingerprint,packetization_enabled,"
        "packet_group_id,received_outer_bytes,group_chunk_count,received_packet_count,"
        "duplicate_packet_count,invalid_packet_count,stale_packet_count,"
        "incomplete_packet_count,expired_group_count,retained_incomplete_group_count,"
        "retained_incomplete_bytes,packet_group_wait_available,packet_group_wait_ns,"
        "compression_mode,"
        "decompression_encoding,decompression_result,compression_dictionary,"
        "compression_dictionary_id,compression_dictionary_fingerprint,compressed_bytes,"
        "compression_payload_bytes,compression_envelope_bytes,uncompressed_bytes,"
        "decompression_elapsed_ns,decode_elapsed_ns,baseline_resolution_elapsed_ns,"
        "reconstruction_elapsed_ns,sink_preparation_elapsed_ns,sink_application_elapsed_ns,"
        "canonical_snapshot_commit_elapsed_ns,total_receive_to_applied_elapsed_ns";

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
        ~EvidenceCsvFile() noexcept;

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
        ~ServerReplicationCsvWriter() noexcept;

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
        ~ClientReplicationCsvWriter() noexcept;

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
