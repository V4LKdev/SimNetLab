module;

#include <cstdint>
#include <optional>
#include <string_view>

/// @brief Active typed replication measurement contracts.
export module simnet.telemetry:metrics;

import :types;
import simnet.core;
import simnet.snapshot;

export namespace simnet
{
    /// Terminal result of one active Server replication attempt.
    enum class ServerReplicationOutcome : std::uint8_t
    {
        SnapshotExtractionFailed,
        Skipped,
        Abandoned,
        TransportSendFailed,
        Sent
    };

    /// One Server replication attempt measured by the application runtime.
    struct ServerReplicationMeasurement
    {
        std::uint64_t runtime_config_fingerprint{};
        std::uint64_t network_compatibility_fingerprint{};
        std::uint64_t application_wire_fingerprint{};
        PeerId peer_id{};
        // The application supplies stable role vocabulary that outlives buffered CSV records.
        std::string_view accepted_gameplay_role{};
        Tick tick{};
        SequenceId sequence{};
        SequenceId baseline_sequence{};
        SequenceId acknowledged_sequence{};
        SnapshotKind snapshot_kind{SnapshotKind::FullReplace};
        ServerReplicationOutcome outcome{ServerReplicationOutcome::SnapshotExtractionFailed};
        /// Stable application-owned detail for skipped and failed terminal outcomes.
        std::string_view outcome_detail{"snapshot_extraction_failed"};

        std::uint32_t source_entity_count{};
        std::uint32_t selected_entity_count{};
        std::uint32_t upsert_count{};
        std::uint32_t delete_count{};
        std::uint32_t canonical_entity_count{};
        std::uint64_t canonical_fingerprint{};

        std::uint32_t aoi_candidate_entity_count{};
        std::uint32_t lod_near_population{};
        std::uint32_t lod_medium_population{};
        std::uint32_t lod_far_population{};
        std::uint32_t lod_near_scheduled{};
        std::uint32_t lod_medium_scheduled{};
        std::uint32_t lod_far_scheduled{};
        std::uint32_t lod_pending_due_count{};
        std::uint32_t lod_transition_count{};
        std::uint32_t lod_forced_immediate_count{};

        std::uint32_t delta_unchanged_entity_count{};
        std::uint32_t delta_spawned_entity_count{};
        std::uint32_t delta_whole_record_entity_count{};
        std::uint32_t delta_field_mask_entity_count{};
        std::uint32_t delta_classification_field_count{};
        std::uint32_t delta_position_field_count{};
        std::uint32_t delta_heading_field_count{};
        std::uint32_t delta_hue_field_count{};
        std::uint64_t delta_complete_record_equivalent_bytes{};
        std::uint64_t delta_encoded_record_bytes{};

        std::uint32_t representation_sample_count{};
        double position_error_world_units_sum{};
        double position_error_world_units_max{};
        double heading_error_degrees_sum{};
        double heading_error_degrees_max{};

        /// Complete pipeline update including its application header.
        std::uint32_t encoded_update_bytes{};

        std::string_view compression_encoding{"disabled"};
        bool compression_raw_fallback{};
        std::uint32_t compression_input_bytes{};
        std::uint32_t compression_output_bytes{};
        /// Steady-clock elapsed wall time around production compression.
        Nanoseconds compression_elapsed_time{};

        std::uint32_t packet_header_bytes{};
        /// Application payload bytes accepted by transport, excluding network overhead.
        std::uint32_t transport_accepted_bytes{};
        /// Application packet submissions accepted by transport.
        std::uint32_t transport_accepted_packet_count{};

        std::string_view recovery_reason{"none"};
        std::uint32_t recovery_forced_upsert_count{};
        std::uint32_t recovery_forced_delete_count{};
        std::uint32_t repeated_without_ack_upsert_count{};
        std::uint32_t repeated_without_ack_delete_count{};
        std::uint32_t submissions_since_ack_progress{};

        /// Steady-clock elapsed wall time around pipeline encoding.
        Nanoseconds encode_elapsed_time{};
        /// Steady-clock elapsed wall time around transport submissions.
        Nanoseconds transport_submission_elapsed_time{};
        /// Steady-clock elapsed wall time from per-peer baseline work through terminal outcome.
        Nanoseconds total_replication_elapsed_time{};
    };

    /// Terminal result of one Snapshot-lane packet handled by the Client runtime.
    enum class ClientReplicationOutcome : std::uint8_t
    {
        PacketIncomplete,
        PacketDuplicate,
        PacketInvalid,
        PacketStale,
        PacketGroupExpired,
        DeliveryMismatch,
        DecompressionFailed,
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
    /// Client Flecs workload. decode_to_applied_elapsed_time is populated only for Applied.
    struct ClientReplicationMeasurement
    {
        std::uint64_t runtime_config_fingerprint{};
        std::uint64_t network_compatibility_fingerprint{};
        std::uint64_t application_wire_fingerprint{};
        PeerId peer_id{};
        Tick tick{};
        SequenceId sequence{};
        SequenceId baseline_sequence{};
        SequenceId acknowledged_sequence_before{};
        SequenceId received_sequence_after{};
        SequenceId acknowledged_sequence_after{};
        SnapshotKind snapshot_kind{SnapshotKind::FullReplace};
        ClientReplicationOutcome outcome{ClientReplicationOutcome::DecodeFailed};
        std::string_view outcome_detail{"decode_failed"};

        /// Complete pipeline update reported by decode.
        std::uint32_t encoded_update_bytes{};
        std::uint32_t upsert_count{};
        std::uint32_t delete_count{};
        std::uint32_t reconstructed_entity_count{};
        std::uint32_t final_sink_entity_count{};
        std::uint64_t canonical_fingerprint{};

        bool packetization_enabled{};
        std::uint32_t packet_group_id{};
        std::uint32_t received_outer_bytes{};
        std::uint32_t group_chunk_count{};
        std::uint32_t received_packet_count{};
        std::uint32_t duplicate_packet_count{};
        std::uint32_t invalid_packet_count{};
        std::uint32_t stale_packet_count{};
        std::uint32_t incomplete_packet_count{};
        std::uint32_t expired_group_count{};
        std::uint32_t retained_incomplete_group_count{};
        std::uint32_t retained_incomplete_bytes{};
        bool packet_group_wait_available{};
        /// Steady-clock elapsed wall time from the first packet to group completion.
        Nanoseconds packet_group_wait_time{};

        std::string_view compression_mode{"none"};
        std::string_view decompression_encoding{"disabled"};
        std::string_view decompression_result{"not_required"};
        std::string_view compression_dictionary{"none"};
        std::uint32_t compression_dictionary_id{};
        std::uint64_t compression_dictionary_fingerprint{};
        std::uint32_t compressed_bytes{};
        std::uint32_t compression_payload_bytes{};
        std::uint32_t compression_envelope_bytes{};
        std::uint32_t uncompressed_bytes{};
        /// Steady-clock elapsed wall time around production decompression.
        Nanoseconds decompression_elapsed_time{};

        /// Steady-clock elapsed wall time around pipeline decoding.
        Nanoseconds decode_elapsed_time{};
        /// Steady-clock elapsed wall time around retained-baseline lookup.
        Nanoseconds baseline_resolution_elapsed_time{};
        /// Steady-clock elapsed wall time around canonical snapshot reconstruction.
        Nanoseconds reconstruction_elapsed_time{};
        /// Steady-clock elapsed wall time around Flecs sink preparation.
        Nanoseconds sink_preparation_elapsed_time{};
        /// Steady-clock elapsed wall time around Flecs sink application.
        Nanoseconds sink_application_elapsed_time{};
        /// Steady-clock elapsed wall time around canonical state and ACK commit.
        Nanoseconds canonical_snapshot_commit_elapsed_time{};
        /// Steady-clock elapsed wall time from decode start through canonical commit.
        Nanoseconds decode_to_applied_elapsed_time{};
    };

    /// Allocation-free current observer state for Server replication attempts.
    struct ServerReplicationMeasurements
    {
        std::uint64_t attempt_count{};
        std::uint64_t sent_count{};
        std::uint64_t recovery_forced_upsert_count{};
        std::uint64_t recovery_forced_delete_count{};
        std::uint64_t repeated_without_ack_upsert_count{};
        std::uint64_t repeated_without_ack_delete_count{};
        std::optional<ServerReplicationMeasurement> latest_attempt{};
        std::optional<ServerReplicationMeasurement> latest_sent{};

        void observe(ServerReplicationMeasurement const& measurement) noexcept
        {
            ++attempt_count;
            recovery_forced_upsert_count += measurement.recovery_forced_upsert_count;
            recovery_forced_delete_count += measurement.recovery_forced_delete_count;
            repeated_without_ack_upsert_count += measurement.repeated_without_ack_upsert_count;
            repeated_without_ack_delete_count += measurement.repeated_without_ack_delete_count;
            latest_attempt = measurement;
            if (measurement.outcome == ServerReplicationOutcome::Sent)
            {
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
            if (measurement.outcome == ClientReplicationOutcome::Applied)
            {
                ++applied_count;
                latest_applied = measurement;
            }
        }
    };

}
