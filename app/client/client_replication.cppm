module;

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <flecs.h>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <simnet/telemetry_trace.hpp>

module simnet.client_runtime:replication;

import simnet.app_common;
import simnet.app_protocol;
import simnet.app_snapshot_delivery;
import simnet.compression;
import simnet.core;
import simnet.game_client;
import simnet.packetization;
import simnet.pipeline;
import simnet.runtime;
import simnet.snapshot;
import simnet.telemetry;
import simnet.transport;

namespace simnet::app::client_replication
{
    struct SnapshotAckTracker
    {
        simnet::app::SnapshotAck value{};
    };

    struct RetainedClientSnapshot
    {
        simnet::SequenceId sequence{};
        simnet::WorldSnapshot snapshot{};
        std::uint64_t capacity_bytes{};
    };

    struct PendingCompressionGroup
    {
        simnet::PacketGroupId group_id{};
        simnet::Nanoseconds first_received_time{};
        simnet::Nanoseconds evidence_first_received_time{};
        std::uint64_t transport_bytes{};
        std::uint64_t compression_input_bytes{};
        std::uint64_t compression_payload_bytes{};
        std::uint64_t compression_envelope_bytes{};
        std::uint64_t compression_output_bytes{};
        simnet::Nanoseconds decompression_elapsed_time{};
        std::uint32_t zstd_packet_count{};
        std::uint32_t raw_packet_count{};
    };

    struct PacketDecompressionObservation
    {
        simnet::CompressionEncoding encoding{simnet::CompressionEncoding::Raw};
        std::uint32_t input_bytes{};
        std::uint32_t payload_bytes{};
        std::uint32_t envelope_bytes{};
        std::uint32_t output_bytes{};
        simnet::Nanoseconds elapsed_time{};
    };

    struct ClientEvidenceIdentity
    {
        std::uint64_t runtime_config_fingerprint{};
        std::uint64_t network_compatibility_fingerprint{};
        std::uint64_t application_wire_fingerprint{};
        std::string_view compression_mode{"none"};
        bool packetization_enabled{};
    };

    struct ClientReceiveEvidence
    {
        simnet::PacketGroupId group_id{};
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
        bool group_wait_available{};
        simnet::Nanoseconds group_wait_time{};
        std::string_view decompression_encoding{"disabled"};
        std::string_view decompression_result{"not_required"};
        std::uint32_t compressed_bytes{};
        std::uint32_t compression_payload_bytes{};
        std::uint32_t compression_envelope_bytes{};
        std::uint32_t uncompressed_bytes{};
        simnet::Nanoseconds decompression_elapsed_time{};
    };

    struct ClientCompressionReport
    {
        simnet::app::CompressionMode mode{simnet::app::CompressionMode::None};
        simnet::CompressionEncoding latest_encoding{simnet::CompressionEncoding::Raw};
        std::uint64_t raw_packet_count{};
        std::uint64_t compressed_packet_count{};
        std::uint64_t invalid_payload_count{};
        std::uint64_t whole_update_raw_fallback_count{};
        std::uint32_t latest_input_bytes{};
        std::uint32_t latest_payload_bytes{};
        std::uint32_t latest_envelope_bytes{};
        std::uint32_t latest_output_bytes{};
        std::uint32_t latest_completed_representation_bytes{};
        std::uint32_t latest_completed_transport_bytes{};
        std::uint32_t canonical_entity_count{};
        simnet::Nanoseconds decompression_elapsed_time{};
    };

    constexpr std::size_t retained_snapshot_limit = 64;

    enum class ApplyPacketOutcome : std::uint8_t
    {
        Applied,
        Ignored,
        RecoveryRequested,
        Fatal
    };

    struct ClientReplicationReceiver
    {
        ClientReplicationReceiver(
            simnet::PipelineDefinition const& pipeline_definition,
            simnet::app::CompressionSettings const& compression_settings,
            simnet::PacketizationSettings const& packetization_settings,
            simnet::TransportDelivery configured_delivery,
            std::uint32_t maximum_transport_payload_bytes,
            ClientEvidenceIdentity evidence
        );

        void begin_session();
        void discard_incomplete();
        void expire(
            simnet::Nanoseconds now,
            bool joined,
            simnet::PeerId peer_id,
            simnet::ClientReplicationCsvWriter& csv
        );
        [[nodiscard]] bool receive_snapshot(
            simnet::ReceivedPacket const& packet,
            simnet::PeerId peer_id,
            simnet::Nanoseconds now,
            flecs::world& world,
            simnet::TransportClient& transport,
            simnet::RuntimeStats& stats,
            simnet::ClientReplicationCsvWriter& csv
        );
        void log_measurements() const;

        simnet::PipelineDefinition const& pipeline;
        simnet::app::CompressionSettings const& compression;
        simnet::PacketizationSettings const& packetization;
        simnet::TransportDelivery delivery{};
        std::uint32_t maximum_transport_payload_bytes{};
        ClientEvidenceIdentity evidence_identity{};
        simnet::ClientReplicationState decode_state{};
        simnet::SequenceId latest_applied_sequence{};
        std::uint32_t latest_applied_upserts{};
        std::uint32_t latest_applied_deletes{};
        SnapshotAckTracker ack_tracker{};
        simnet::app::ClientRecoveryRequestState recovery_request_state{};
        std::uint64_t sequence_gap_count{};
        std::uint64_t reliable_promotion_count{};
        std::optional<simnet::TransportDelivery> effective_snapshot_delivery{};
        simnet::ReassemblyState reassembly_state{};
        simnet::ZstdDecompressor decompressor{};
        std::vector<simnet::Byte> decompression_scratch{};
        std::vector<PendingCompressionGroup> pending_compression_groups{};
        ClientCompressionReport compression_report{};
        std::deque<RetainedClientSnapshot> snapshot_history{};
        bool logged_multi_packet_application{};
        bool logged_compression_application{};
        simnet::ClientReplicationMeasurements measurements{};
    };

    void expire_pending_compression_groups(
        std::vector<PendingCompressionGroup>& groups,
        simnet::Nanoseconds now,
        simnet::Nanoseconds timeout
    )
    {
        std::erase_if(
            groups,
            [&](PendingCompressionGroup const& group)
            {
                return now >= group.first_received_time &&
                       now - group.first_received_time >= timeout;
            }
        );
    }

    void record_group_transport_bytes(
        std::vector<PendingCompressionGroup>& groups,
        simnet::PacketGroupId group_id,
        std::uint32_t bytes,
        simnet::Nanoseconds now,
        simnet::Nanoseconds evidence_now,
        PacketDecompressionObservation const& decompression,
        std::uint32_t maximum_groups
    )
    {
        if (group_id == 0U)
        {
            return;
        }
        auto const found = std::ranges::find(groups, group_id, &PendingCompressionGroup::group_id);
        if (found != groups.end())
        {
            found->transport_bytes += bytes;
            found->compression_input_bytes += decompression.input_bytes;
            found->compression_payload_bytes += decompression.payload_bytes;
            found->compression_envelope_bytes += decompression.envelope_bytes;
            found->compression_output_bytes += decompression.output_bytes;
            found->decompression_elapsed_time += decompression.elapsed_time;
            if (decompression.encoding == simnet::CompressionEncoding::Zstd)
            {
                ++found->zstd_packet_count;
            }
            else
            {
                ++found->raw_packet_count;
            }
            return;
        }
        if (groups.size() >= maximum_groups)
        {
            return;
        }
        groups.push_back({
            .group_id = group_id,
            .first_received_time = now,
            .evidence_first_received_time = evidence_now,
            .transport_bytes = bytes,
            .compression_input_bytes = decompression.input_bytes,
            .compression_payload_bytes = decompression.payload_bytes,
            .compression_envelope_bytes = decompression.envelope_bytes,
            .compression_output_bytes = decompression.output_bytes,
            .decompression_elapsed_time = decompression.elapsed_time,
            .zstd_packet_count =
                decompression.encoding == simnet::CompressionEncoding::Zstd ? 1U : 0U,
            .raw_packet_count =
                decompression.encoding == simnet::CompressionEncoding::Zstd ? 0U : 1U,
        });
    }

    [[nodiscard]] PendingCompressionGroup const* find_pending_group(
        std::vector<PendingCompressionGroup> const& groups,
        simnet::PacketGroupId group_id
    ) noexcept
    {
        auto const found = std::ranges::find(groups, group_id, &PendingCompressionGroup::group_id);
        return found == groups.end() ? nullptr : &*found;
    }

    [[nodiscard]] std::uint32_t group_transport_bytes(
        std::vector<PendingCompressionGroup> const& groups,
        simnet::PacketGroupId group_id,
        std::uint32_t fallback
    ) noexcept
    {
        auto const found = std::ranges::find(groups, group_id, &PendingCompressionGroup::group_id);
        return found == groups.end() ||
                       found->transport_bytes > std::numeric_limits<std::uint32_t>::max()
                   ? fallback
                   : static_cast<std::uint32_t>(found->transport_bytes);
    }

    [[nodiscard]] std::string_view
    client_replication_outcome_name(simnet::ClientReplicationOutcome outcome) noexcept
    {
        using enum simnet::ClientReplicationOutcome;
        switch (outcome)
        {
            case PacketIncomplete:
                return "packet_incomplete";
            case PacketDuplicate:
                return "packet_duplicate";
            case PacketInvalid:
                return "packet_invalid";
            case PacketStale:
                return "packet_stale";
            case PacketGroupExpired:
                return "packet_group_expired";
            case DeliveryMismatch:
                return "delivery_mismatch";
            case DecompressionFailed:
                return "decompression_failed";
            case DecodeFailed:
                return "decode_failed";
            case StaleSequenceIgnored:
                return "stale_sequence_ignored";
            case BaselineUnavailable:
                return "baseline_unavailable";
            case ReconstructionFailed:
                return "reconstruction_failed";
            case SinkApplicationFailed:
                return "sink_application_failed";
            case Applied:
                return "applied";
        }
        return "unknown";
    }

    void
    log_client_replication_measurements(simnet::ClientReplicationMeasurements const& measurements)
    {
        if (!measurements.latest_attempt.has_value())
        {
            simnet::log(
                simnet::LogCategory::Telemetry,
                simnet::LogLevel::Info,
                "client replication measurements attempts=0 applied=0"
            );
            return;
        }

        auto const& value = *measurements.latest_attempt;
        simnet::log(
            simnet::LogCategory::Telemetry,
            simnet::LogLevel::Info,
            "client replication measurements attempts=" +
                std::to_string(measurements.attempt_count) +
                " applied=" + std::to_string(measurements.applied_count) +
                " latest_outcome=" + std::string{client_replication_outcome_name(value.outcome)} +
                " tick=" + std::to_string(value.tick) +
                " sequence=" + std::to_string(value.sequence) +
                " baseline_sequence=" + std::to_string(value.baseline_sequence) +
                " snapshot_kind=" + std::to_string(static_cast<unsigned>(value.snapshot_kind)) +
                " encoded_update_bytes=" + std::to_string(value.encoded_update_bytes) +
                " received_outer_bytes=" + std::to_string(value.received_outer_bytes) +
                " upserts=" + std::to_string(value.upsert_count) +
                " deletes=" + std::to_string(value.delete_count) +
                " reconstructed_entities=" + std::to_string(value.reconstructed_entity_count) +
                " final_sink_entities=" + std::to_string(value.final_sink_entity_count) +
                " decode_elapsed_ns=" + std::to_string(value.decode_elapsed_time.count()) +
                " baseline_resolution_elapsed_ns=" +
                std::to_string(value.baseline_resolution_elapsed_time.count()) +
                " reconstruction_elapsed_ns=" +
                std::to_string(value.reconstruction_elapsed_time.count()) +
                " sink_preparation_elapsed_ns=" +
                std::to_string(value.sink_preparation_elapsed_time.count()) +
                " sink_application_elapsed_ns=" +
                std::to_string(value.sink_application_elapsed_time.count()) +
                " canonical_snapshot_commit_elapsed_ns=" +
                std::to_string(value.canonical_snapshot_commit_elapsed_time.count()) +
                " decode_to_applied_elapsed_ns=" +
                std::to_string(value.decode_to_applied_elapsed_time.count())
        );
    }


    void observe_client_measurement(
        simnet::ClientReplicationMeasurements& measurements,
        simnet::ClientReplicationCsvWriter& csv,
        simnet::ClientReplicationMeasurement const& measurement
    )
    {
        measurements.observe(measurement);
        if (!csv.submit(measurement))
        {
            throw std::runtime_error(
                "client replication CSV submission failed: " + std::string{csv.error()}
            );
        }
    }

    [[nodiscard]] simnet::ClientReplicationMeasurement make_client_measurement(
        ClientEvidenceIdentity const& identity,
        simnet::PeerId peer_id,
        SnapshotAckTracker const& ack,
        ClientReceiveEvidence const& receive
    ) noexcept
    {
        return {
            .runtime_config_fingerprint = identity.runtime_config_fingerprint,
            .network_compatibility_fingerprint = identity.network_compatibility_fingerprint,
            .application_wire_fingerprint = identity.application_wire_fingerprint,
            .peer_id = peer_id,
            .sequence = receive.group_id,
            .acknowledged_sequence_before = ack.value.newest_applied_snapshot,
            .received_sequence_after = ack.value.newest_received_snapshot,
            .acknowledged_sequence_after = ack.value.newest_applied_snapshot,
            .packetization_enabled = identity.packetization_enabled,
            .packet_group_id = receive.group_id,
            .received_outer_bytes = receive.received_outer_bytes,
            .group_chunk_count = receive.group_chunk_count,
            .received_packet_count = receive.received_packet_count,
            .duplicate_packet_count = receive.duplicate_packet_count,
            .invalid_packet_count = receive.invalid_packet_count,
            .stale_packet_count = receive.stale_packet_count,
            .incomplete_packet_count = receive.incomplete_packet_count,
            .expired_group_count = receive.expired_group_count,
            .retained_incomplete_group_count = receive.retained_incomplete_group_count,
            .retained_incomplete_bytes = receive.retained_incomplete_bytes,
            .packet_group_wait_available = receive.group_wait_available,
            .packet_group_wait_time = receive.group_wait_time,
            .compression_mode = identity.compression_mode,
            .decompression_encoding = receive.decompression_encoding,
            .decompression_result = receive.decompression_result,
            .compressed_bytes = receive.compressed_bytes,
            .compression_payload_bytes = receive.compression_payload_bytes,
            .compression_envelope_bytes = receive.compression_envelope_bytes,
            .uncompressed_bytes = receive.uncompressed_bytes,
            .decompression_elapsed_time = receive.decompression_elapsed_time,
        };
    }

    void observe_client_receive_outcome(
        simnet::ClientReplicationMeasurements& measurements,
        simnet::ClientReplicationCsvWriter& csv,
        ClientEvidenceIdentity const& identity,
        simnet::PeerId peer_id,
        SnapshotAckTracker const& ack,
        ClientReceiveEvidence const& receive,
        simnet::ClientReplicationOutcome outcome,
        std::string_view detail
    )
    {
        auto measurement = make_client_measurement(identity, peer_id, ack, receive);
        measurement.outcome = outcome;
        measurement.outcome_detail = detail;
        observe_client_measurement(measurements, csv, measurement);
    }

    [[nodiscard]] bool
    record_received_snapshot(SnapshotAckTracker& tracker, simnet::SequenceId sequence) noexcept
    {
        auto const previous = tracker.value.newest_received_snapshot;
        if (sequence == 0 || sequence <= previous)
        {
            return false;
        }
        if (previous == 0)
        {
            tracker.value.newest_received_snapshot = sequence;
            tracker.value.received_mask = 0;
            return true;
        }

        auto const shift = sequence - previous;
        if (shift >= 33U)
        {
            tracker.value.received_mask = 0;
        }
        else
        {
            auto const shifted_history = shift == 32U ? 0U : tracker.value.received_mask << shift;
            tracker.value.received_mask = shifted_history | (1U << (shift - 1U));
        }
        tracker.value.newest_received_snapshot = sequence;
        return true;
    }

    [[nodiscard]] simnet::WorldSnapshot const* find_retained_snapshot(
        std::deque<RetainedClientSnapshot> const& history,
        simnet::SequenceId sequence
    ) noexcept
    {
        auto const found = std::ranges::find(history, sequence, &RetainedClientSnapshot::sequence);
        return found == history.end() ? nullptr : &found->snapshot;
    }

    void retain_snapshot(
        std::deque<RetainedClientSnapshot>& history,
        simnet::SequenceId sequence,
        simnet::WorldSnapshot snapshot
    )
    {
        auto const bytes = simnet::app::snapshot_capacity_bytes(snapshot);
        auto retained_bytes = std::uint64_t{};
        for (auto const& retained : history)
        {
            retained_bytes += retained.capacity_bytes;
        }
        while (!history.empty() &&
               (history.size() >= retained_snapshot_limit ||
                retained_bytes > simnet::app::maximum_retained_capacity_bytes - bytes))
        {
            retained_bytes -= history.front().capacity_bytes;
            history.pop_front();
        }
        history.push_back({
            .sequence = sequence,
            .snapshot = std::move(snapshot),
            .capacity_bytes = bytes,
        });
    }

    [[nodiscard]] simnet::SnapshotUpdate
    make_full_replace_patch(simnet::WorldSnapshot const& snapshot)
    {
        auto patch = simnet::SnapshotUpdate{
            .tick = snapshot.tick,
            .kind = simnet::SnapshotKind::FullReplace,
            .upserts = {},
            .deletes = {},
        };
        patch.upserts.reserve(snapshot.size());
        for (std::size_t index = 0; index < snapshot.size(); ++index)
        {
            patch.upserts.push_back({
                .id = snapshot.ids[index],
                .classification = snapshot.classifications[index],
                .position = snapshot.positions[index],
                .heading = snapshot.headings[index],
                .hue = snapshot.hues[index],
            });
        }
        return patch;
    }

    [[nodiscard]] ApplyPacketOutcome apply_packet(
        ClientEvidenceIdentity const& evidence_identity,
        simnet::PeerId peer_id,
        ClientReceiveEvidence const& receive_evidence,
        simnet::CompletedByteGroup const& group,
        simnet::ByteSpan encoded_bytes,
        bool packetization_enabled,
        simnet::ReassemblyState& reassembly_state,
        simnet::PipelineDefinition const& pipeline,
        simnet::ClientReplicationState& decode_state,
        simnet::SequenceId& latest_applied_sequence,
        SnapshotAckTracker& ack_tracker,
        std::deque<RetainedClientSnapshot>& snapshot_history,
        flecs::world& world,
        simnet::TransportClient& transport,
        simnet::RuntimeStats& stats,
        simnet::ClientReplicationMeasurements& measurements,
        simnet::ClientReplicationCsvWriter& csv,
        bool& logged_multi_packet_application,
        simnet::app::ClientRecoveryRequestState& recovery_request_state,
        std::uint64_t& sequence_gap_count,
        std::uint32_t& latest_applied_upserts,
        std::uint32_t& latest_applied_deletes
    )
    {
        auto measurement =
            make_client_measurement(evidence_identity, peer_id, ack_tracker, receive_evidence);
        auto const decode_to_applied_start = simnet::steady_now_ns();
        auto const decode_start = simnet::steady_now_ns();
        auto const inspected =
            simnet::inspect_encoded_update_header(pipeline, decode_state, encoded_bytes);
        measurement.decode_elapsed_time = simnet::steady_now_ns() - decode_start;
        measurement.tick = inspected.tick;
        measurement.sequence = inspected.sequence;
        measurement.baseline_sequence = inspected.baseline_sequence;
        measurement.snapshot_kind = inspected.snapshot_kind;
        measurement.encoded_update_bytes = static_cast<std::uint32_t>(encoded_bytes.size());
        if (!inspected.valid())
        {
            if (inspected.error == simnet::EncodedUpdateHeaderError::StaleSequence)
            {
                measurement.outcome = simnet::ClientReplicationOutcome::StaleSequenceIgnored;
                measurement.outcome_detail = "stale_encoded_sequence";
                observe_client_measurement(measurements, csv, measurement);
                return ApplyPacketOutcome::Ignored;
            }
            measurement.outcome = simnet::ClientReplicationOutcome::DecodeFailed;
            measurement.outcome_detail = "header_inspection_failed";
            observe_client_measurement(measurements, csv, measurement);
            simnet::log(
                simnet::LogCategory::Pipeline,
                simnet::LogLevel::Error,
                "client snapshot header inspection failed error=" +
                    std::to_string(static_cast<unsigned>(inspected.error))
            );
            return ApplyPacketOutcome::Fatal;
        }
        if (packetization_enabled && inspected.sequence != group.group_id)
        {
            measurement.outcome = simnet::ClientReplicationOutcome::DecodeFailed;
            measurement.outcome_detail = "packet_group_sequence_mismatch";
            observe_client_measurement(measurements, csv, measurement);
            simnet::log(
                simnet::LogCategory::Pipeline,
                simnet::LogLevel::Error,
                "client packet group id does not match inspected sequence"
            );
            return ApplyPacketOutcome::Fatal;
        }

        auto const baseline_start = simnet::steady_now_ns();
        auto const* baseline = static_cast<simnet::WorldSnapshot const*>(nullptr);
        if (inspected.baseline_sequence != 0U)
        {
            baseline = find_retained_snapshot(snapshot_history, inspected.baseline_sequence);
            if (baseline == nullptr)
            {
                measurement.baseline_resolution_elapsed_time =
                    simnet::steady_now_ns() - baseline_start;
                measurement.outcome = simnet::ClientReplicationOutcome::BaselineUnavailable;
                measurement.outcome_detail = "baseline_not_retained";
                observe_client_measurement(measurements, csv, measurement);
                simnet::log(
                    simnet::LogCategory::Snapshot,
                    simnet::LogLevel::Warn,
                    "client Patch baseline is not retained sequence=" +
                        std::to_string(inspected.baseline_sequence)
                );
                simnet::app::record_missing_baseline_rejection(recovery_request_state);
                if (simnet::app::recovery_request_needed(
                        recovery_request_state,
                        inspected.baseline_sequence
                    ))
                {
                    auto const request = simnet::app::encode_snapshot_recovery_request({
                        .rejected_update_sequence = inspected.sequence,
                        .missing_baseline_sequence = inspected.baseline_sequence,
                    });
                    auto const sent = transport.send(
                        simnet::app::input_lane,
                        simnet::TransportDelivery::ReliableSequenced,
                        request
                    );
                    if (!sent.ok)
                    {
                        simnet::log(
                            simnet::LogCategory::Transport,
                            simnet::LogLevel::Error,
                            "client recovery request send failed: " + sent.error.message
                        );
                        return ApplyPacketOutcome::Fatal;
                    }
                    simnet::app::record_recovery_request(
                        recovery_request_state,
                        inspected.baseline_sequence
                    );
                }
                return ApplyPacketOutcome::RecoveryRequested;
            }
        }
        measurement.baseline_resolution_elapsed_time = simnet::steady_now_ns() - baseline_start;

        auto decoded = simnet::DecodeOutput{};
        auto candidate_decode_state = decode_state;
        auto const full_decode_start = simnet::steady_now_ns();
        {
            SIMNET_TRACE_SCOPE_CATEGORY("client.snapshot_decode", simnet::LogCategory::Pipeline);
            decoded = simnet::decode_update_unchecked(
                pipeline,
                candidate_decode_state,
                {
                    .bytes = encoded_bytes,
                    .baseline_snapshot = baseline,
                    .baseline_sequence = inspected.baseline_sequence,
                }
            );
        }
        measurement.decode_elapsed_time += simnet::steady_now_ns() - full_decode_start;
        measurement.upsert_count = static_cast<std::uint32_t>(decoded.update.upserts.size());
        measurement.delete_count = static_cast<std::uint32_t>(decoded.update.deletes.size());
        if (!decoded.report.valid)
        {
            measurement.outcome = simnet::ClientReplicationOutcome::DecodeFailed;
            measurement.outcome_detail = "update_decode_failed";
            observe_client_measurement(measurements, csv, measurement);
            simnet::log(
                simnet::LogCategory::Pipeline,
                simnet::LogLevel::Error,
                "client snapshot decode failed: " + decoded.report.error
            );
            return ApplyPacketOutcome::Fatal;
        }
        auto candidate_ack_tracker = ack_tracker;
        if (!record_received_snapshot(candidate_ack_tracker, decoded.report.sequence))
        {
            measurement.outcome = simnet::ClientReplicationOutcome::StaleSequenceIgnored;
            measurement.outcome_detail = "ack_tracker_rejected_sequence";
            observe_client_measurement(measurements, csv, measurement);
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Warn,
                "client ignored stale snapshot sequence=" + std::to_string(decoded.report.sequence)
            );
            return ApplyPacketOutcome::Ignored;
        }
        measurement.received_sequence_after = candidate_ack_tracker.value.newest_received_snapshot;
        if (latest_applied_sequence != 0U && decoded.report.sequence > latest_applied_sequence + 1U)
        {
            ++sequence_gap_count;
        }

        auto reconstructed = simnet::WorldSnapshot{};
        // Decode validated the update. The baseline is locally empty or a retained reconstruction.
        auto const reconstruction_start = simnet::steady_now_ns();
        auto const reconstruction =
            simnet::reconstruct_world_snapshot_unchecked(baseline, decoded.update, reconstructed);
        measurement.reconstruction_elapsed_time = simnet::steady_now_ns() - reconstruction_start;
        if (!reconstruction.valid)
        {
            measurement.outcome = simnet::ClientReplicationOutcome::ReconstructionFailed;
            measurement.outcome_detail = "snapshot_reconstruction_failed";
            observe_client_measurement(measurements, csv, measurement);
            simnet::log(
                simnet::LogCategory::Snapshot,
                simnet::LogLevel::Error,
                "client snapshot reconstruction failed: " + reconstruction.message
            );
            return ApplyPacketOutcome::Fatal;
        }
        if (simnet::app::snapshot_capacity_bytes(reconstructed) >
            simnet::app::maximum_retained_capacity_bytes)
        {
            measurement.outcome = simnet::ClientReplicationOutcome::ReconstructionFailed;
            measurement.outcome_detail = "snapshot_retention_capacity_exceeded";
            observe_client_measurement(measurements, csv, measurement);
            simnet::log(
                simnet::LogCategory::Snapshot,
                simnet::LogLevel::Error,
                "client reconstructed snapshot exceeds the retained capacity limit"
            );
            return ApplyPacketOutcome::Fatal;
        }
        measurement.reconstructed_entity_count = static_cast<std::uint32_t>(reconstructed.size());

        auto const sink_preparation_start = simnet::steady_now_ns();
        auto const baseline_is_current = baseline != nullptr && !snapshot_history.empty() &&
                                         baseline == &snapshot_history.back().snapshot;
        auto replacement = simnet::SnapshotUpdate{};
        auto const* patch_to_apply = &decoded.update;
        if (decoded.update.kind == simnet::SnapshotKind::Patch && !baseline_is_current)
        {
            replacement = make_full_replace_patch(reconstructed);
            patch_to_apply = &replacement;
        }
        measurement.sink_preparation_elapsed_time =
            simnet::steady_now_ns() - sink_preparation_start;

        auto applied = simnet::ApplyPatchReport{};
        auto const sink_application_start = simnet::steady_now_ns();
        {
            SIMNET_TRACE_SCOPE_CATEGORY("client.snapshot_apply", simnet::LogCategory::Simulation);
            // The update passed decode validation or was built from successful reconstruction.
            applied = simnet::apply_client_snapshot_patch_unchecked(world, *patch_to_apply);
        }
        measurement.sink_application_elapsed_time =
            simnet::steady_now_ns() - sink_application_start;
        measurement.final_sink_entity_count = applied.final_entities;
        if (!applied.valid)
        {
            measurement.outcome = simnet::ClientReplicationOutcome::SinkApplicationFailed;
            measurement.outcome_detail = "flecs_application_failed";
            observe_client_measurement(measurements, csv, measurement);
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Error,
                "client snapshot apply failed: " + applied.error
            );
            return ApplyPacketOutcome::Fatal;
        }

        auto const commit_start = simnet::steady_now_ns();
        decode_state = candidate_decode_state;
        candidate_ack_tracker.value.newest_applied_snapshot = decoded.report.sequence;
        ack_tracker = candidate_ack_tracker;
        latest_applied_sequence = decoded.report.sequence;
        latest_applied_upserts = static_cast<std::uint32_t>(decoded.update.upserts.size());
        latest_applied_deletes = static_cast<std::uint32_t>(decoded.update.deletes.size());
        retain_snapshot(snapshot_history, decoded.report.sequence, std::move(reconstructed));
        simnet::app::record_snapshot_progress(recovery_request_state);
        stats.ticks = applied.tick;
        simnet::commit_reassembled_group(reassembly_state, decoded.report.sequence);
        measurement.canonical_snapshot_commit_elapsed_time = simnet::steady_now_ns() - commit_start;
        measurement.outcome = simnet::ClientReplicationOutcome::Applied;
        measurement.outcome_detail = "committed";
        measurement.acknowledged_sequence_after = ack_tracker.value.newest_applied_snapshot;
        measurement.decode_to_applied_elapsed_time =
            simnet::steady_now_ns() - decode_to_applied_start;
        measurement.canonical_fingerprint =
            simnet::app::snapshot_diagnostic_fingerprint(snapshot_history.back().snapshot);
        observe_client_measurement(measurements, csv, measurement);
        auto sent = simnet::TransportResult{};
        {
            SIMNET_TRACE_SCOPE_CATEGORY("client.snapshot_ack", simnet::LogCategory::Transport);
            auto const bytes = simnet::app::encode_snapshot_ack(ack_tracker.value);
            sent = transport.send(
                simnet::app::input_lane,
                simnet::TransportDelivery::ReliableSequenced,
                bytes
            );
        }
        if (!sent.ok)
        {
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Error,
                "client snapshot ACK send failed: " + sent.error.message
            );
            return ApplyPacketOutcome::Fatal;
        }
        if (group.chunk_count > 1U && !logged_multi_packet_application)
        {
            logged_multi_packet_application = true;
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Info,
                "client packet group applied group_id=" + std::to_string(group.group_id) +
                    " encoded_bytes=" + std::to_string(encoded_bytes.size()) +
                    " chunks=" + std::to_string(group.chunk_count) +
                    " application_packet_bytes=" + std::to_string(group.total_packet_bytes) +
                    " canonical_entities=" + std::to_string(applied.final_entities) +
                    " ack_sequence=" + std::to_string(ack_tracker.value.newest_applied_snapshot)
            );
        }

        if (simnet::log_enabled(simnet::LogLevel::Debug))
        {
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Debug,
                "client snapshot applied tick=" + std::to_string(applied.tick) +
                    " sequence=" + std::to_string(decoded.report.sequence) +
                    " entities=" + std::to_string(applied.final_entities) +
                    " fingerprint=" + std::to_string(measurement.canonical_fingerprint)
            );
        }
        return ApplyPacketOutcome::Applied;
    }

    ClientReplicationReceiver::ClientReplicationReceiver(
        simnet::PipelineDefinition const& pipeline_definition,
        simnet::app::CompressionSettings const& compression_settings,
        simnet::PacketizationSettings const& packetization_settings,
        simnet::TransportDelivery configured_delivery,
        std::uint32_t maximum_payload_bytes,
        ClientEvidenceIdentity evidence
    )
        : pipeline{pipeline_definition},
          compression{compression_settings},
          packetization{packetization_settings},
          delivery{configured_delivery},
          maximum_transport_payload_bytes{maximum_payload_bytes},
          evidence_identity{evidence},
          compression_report{.mode = compression_settings.mode}
    {
        pending_compression_groups.reserve(packetization.max_in_flight_groups);
    }

    void ClientReplicationReceiver::begin_session()
    {
        simnet::app::record_snapshot_progress(recovery_request_state);
        effective_snapshot_delivery.reset();
        simnet::clear_reassembly_state(reassembly_state);
        pending_compression_groups.clear();
    }

    void ClientReplicationReceiver::discard_incomplete()
    {
        simnet::clear_reassembly_state(reassembly_state);
        pending_compression_groups.clear();
    }

    void ClientReplicationReceiver::expire(
        simnet::Nanoseconds now,
        bool joined,
        simnet::PeerId peer_id,
        simnet::ClientReplicationCsvWriter& csv
    )
    {
        auto const expired_groups_before = reassembly_state.report.expired_groups;
        simnet::expire_incomplete_groups(packetization, reassembly_state, now);
        auto const newly_expired_groups =
            reassembly_state.report.expired_groups - expired_groups_before;
        if (newly_expired_groups != 0U && joined)
        {
            observe_client_receive_outcome(
                measurements,
                csv,
                evidence_identity,
                peer_id,
                ack_tracker,
                {
                    .expired_group_count = static_cast<std::uint32_t>(std::min(
                        newly_expired_groups,
                        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())
                    )),
                    .retained_incomplete_group_count = reassembly_state.report.incomplete_groups,
                    .retained_incomplete_bytes = reassembly_state.report.retained_incomplete_bytes,
                },
                simnet::ClientReplicationOutcome::PacketGroupExpired,
                "reassembly_timeout"
            );
        }
        expire_pending_compression_groups(
            pending_compression_groups,
            now,
            packetization.reassembly_timeout
        );
    }

    [[nodiscard]] bool ClientReplicationReceiver::receive_snapshot(
        simnet::ReceivedPacket const& packet,
        simnet::PeerId peer_id,
        simnet::Nanoseconds now,
        flecs::world& world,
        simnet::TransportClient& transport,
        simnet::RuntimeStats& stats,
        simnet::ClientReplicationCsvWriter& csv
    )
    {
        effective_snapshot_delivery = packet.delivery;
        auto valid = true;
        if (delivery == simnet::TransportDelivery::ReliableSequenced)
        {
            valid = packet.delivery == simnet::TransportDelivery::ReliableSequenced;
        }
        else if (packet.delivery == simnet::TransportDelivery::ReliableSequenced)
        {
            ++reliable_promotion_count;
        }

        auto application_packet = simnet::ByteSpan{packet.payload};
        auto packet_decompression = PacketDecompressionObservation{};
        if (valid && compression.mode == simnet::app::CompressionMode::PerPacket)
        {
            packet_decompression = {
                .encoding = simnet::CompressionEncoding::Raw,
                .input_bytes = static_cast<std::uint32_t>(application_packet.size()),
                .payload_bytes = static_cast<std::uint32_t>(application_packet.size()),
                .output_bytes = static_cast<std::uint32_t>(application_packet.size()),
            };
            if (simnet::has_compression_envelope(application_packet))
            {
                auto const decompressed = simnet::decompress_bytes(
                    decompressor,
                    application_packet,
                    {
                        .max_uncompressed_bytes = packetization.max_payload_bytes,
                        .max_output_bytes = maximum_transport_payload_bytes,
                    },
                    decompression_scratch
                );
                compression_report.latest_input_bytes = decompressed.input_bytes;
                compression_report.latest_encoding = decompressed.encoding;
                compression_report.latest_payload_bytes = decompressed.encoded_payload_bytes;
                compression_report.latest_envelope_bytes = decompressed.envelope_bytes;
                compression_report.latest_output_bytes = decompressed.output_bytes;
                compression_report.decompression_elapsed_time +=
                    decompressed.decompression_elapsed_time;
                packet_decompression = {
                    .encoding = decompressed.encoding,
                    .input_bytes = decompressed.input_bytes,
                    .payload_bytes = decompressed.encoded_payload_bytes,
                    .envelope_bytes = decompressed.envelope_bytes,
                    .output_bytes = decompressed.output_bytes,
                    .elapsed_time = decompressed.decompression_elapsed_time,
                };
                if (!decompressed.valid ||
                    decompressed.encoding != simnet::CompressionEncoding::Zstd)
                {
                    ++compression_report.invalid_payload_count;
                    observe_client_receive_outcome(
                        measurements,
                        csv,
                        evidence_identity,
                        peer_id,
                        ack_tracker,
                        {
                            .received_outer_bytes =
                                static_cast<std::uint32_t>(packet.payload.size()),
                            .received_packet_count = 1U,
                            .invalid_packet_count = 1U,
                            .decompression_encoding = "invalid",
                            .decompression_result = "failed",
                            .compressed_bytes = decompressed.input_bytes,
                            .compression_payload_bytes = decompressed.encoded_payload_bytes,
                            .compression_envelope_bytes = decompressed.envelope_bytes,
                            .uncompressed_bytes = decompressed.output_bytes,
                            .decompression_elapsed_time = decompressed.decompression_elapsed_time,
                        },
                        simnet::ClientReplicationOutcome::DecompressionFailed,
                        "per_packet_decompression_failed"
                    );
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Warn,
                        "client rejected compressed snapshot packet: " + decompressed.error
                    );
                    return true;
                }
                ++compression_report.compressed_packet_count;
                application_packet = decompression_scratch;
            }
            else
            {
                ++compression_report.raw_packet_count;
            }
        }

        auto reassembled = valid
                               ? simnet::accept_group_packet(
                                     packetization,
                                     reassembly_state,
                                     application_packet,
                                     now
                                 )
                               : simnet::ReassemblyResult{
                                     .kind = simnet::ReassemblyResultKind::Invalid,
                                     .error = "snapshot delivery does not match configuration",
                                 };
        if (reassembled.group_id != 0U &&
            reassembled.kind != simnet::ReassemblyResultKind::Invalid &&
            reassembled.kind != simnet::ReassemblyResultKind::LimitExceeded &&
            reassembled.kind != simnet::ReassemblyResultKind::Stale)
        {
            record_group_transport_bytes(
                pending_compression_groups,
                reassembled.group_id,
                static_cast<std::uint32_t>(packet.payload.size()),
                now,
                simnet::steady_now_ns(),
                packet_decompression,
                packetization.max_in_flight_groups
            );
        }

        auto receive_evidence = ClientReceiveEvidence{
            .group_id = reassembled.group_id,
            .received_outer_bytes = static_cast<std::uint32_t>(packet.payload.size()),
            .received_packet_count = 1U,
            .retained_incomplete_group_count = reassembly_state.report.incomplete_groups,
            .retained_incomplete_bytes = reassembly_state.report.retained_incomplete_bytes,
        };
        if (reassembled.kind == simnet::ReassemblyResultKind::Incomplete)
        {
            receive_evidence.incomplete_packet_count = 1U;
        }
        else if (reassembled.kind == simnet::ReassemblyResultKind::Duplicate)
        {
            receive_evidence.duplicate_packet_count = 1U;
        }
        else if (reassembled.kind == simnet::ReassemblyResultKind::Stale)
        {
            receive_evidence.stale_packet_count = 1U;
        }
        else if (reassembled.kind == simnet::ReassemblyResultKind::Invalid ||
                 reassembled.kind == simnet::ReassemblyResultKind::LimitExceeded)
        {
            receive_evidence.invalid_packet_count = 1U;
        }

        auto const* pending_group =
            find_pending_group(pending_compression_groups, reassembled.group_id);
        if (pending_group != nullptr)
        {
            receive_evidence.group_wait_available = true;
            receive_evidence.group_wait_time =
                simnet::steady_now_ns() - pending_group->evidence_first_received_time;
            if (compression.mode == simnet::app::CompressionMode::PerPacket)
            {
                auto const narrow = [](std::uint64_t value)
                {
                    return static_cast<std::uint32_t>(std::min(
                        value,
                        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())
                    ));
                };
                receive_evidence.decompression_encoding =
                    pending_group->zstd_packet_count != 0U &&
                            pending_group->raw_packet_count != 0U
                        ? "mixed"
                    : pending_group->zstd_packet_count != 0U ? "zstd"
                                                             : "raw";
                receive_evidence.decompression_result = "success";
                receive_evidence.compressed_bytes =
                    narrow(pending_group->compression_input_bytes);
                receive_evidence.compression_payload_bytes =
                    narrow(pending_group->compression_payload_bytes);
                receive_evidence.compression_envelope_bytes =
                    narrow(pending_group->compression_envelope_bytes);
                receive_evidence.uncompressed_bytes =
                    narrow(pending_group->compression_output_bytes);
                receive_evidence.decompression_elapsed_time =
                    pending_group->decompression_elapsed_time;
            }
        }

        if (reassembled.kind == simnet::ReassemblyResultKind::Complete)
        {
            receive_evidence.group_id = reassembled.completed.group_id;
            receive_evidence.group_chunk_count = reassembled.completed.chunk_count;
            auto encoded_bytes = simnet::ByteSpan{reassembled.completed.bytes};
            if (compression.mode == simnet::app::CompressionMode::WholeUpdate)
            {
                auto const limits = simnet::CompressionLimits{
                    .max_uncompressed_bytes = packetization.max_group_bytes,
                    .max_output_bytes = packetization.max_group_bytes,
                };
                auto const decompressed = simnet::decompress_bytes(
                    decompressor,
                    encoded_bytes,
                    limits,
                    decompression_scratch
                );
                compression_report.latest_input_bytes = decompressed.input_bytes;
                compression_report.latest_encoding = decompressed.encoding;
                compression_report.latest_payload_bytes = decompressed.encoded_payload_bytes;
                compression_report.latest_envelope_bytes = decompressed.envelope_bytes;
                compression_report.latest_output_bytes = decompressed.output_bytes;
                compression_report.decompression_elapsed_time +=
                    decompressed.decompression_elapsed_time;
                receive_evidence.decompression_encoding =
                    decompressed.encoding == simnet::CompressionEncoding::Raw ? "raw" : "zstd";
                receive_evidence.decompression_result = decompressed.valid ? "success" : "failed";
                receive_evidence.compressed_bytes = decompressed.input_bytes;
                receive_evidence.compression_payload_bytes = decompressed.encoded_payload_bytes;
                receive_evidence.compression_envelope_bytes = decompressed.envelope_bytes;
                receive_evidence.uncompressed_bytes = decompressed.output_bytes;
                receive_evidence.decompression_elapsed_time =
                    decompressed.decompression_elapsed_time;
                if (!decompressed.valid)
                {
                    ++compression_report.invalid_payload_count;
                    observe_client_receive_outcome(
                        measurements,
                        csv,
                        evidence_identity,
                        peer_id,
                        ack_tracker,
                        receive_evidence,
                        simnet::ClientReplicationOutcome::DecompressionFailed,
                        "whole_update_decompression_failed"
                    );
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Warn,
                        "client rejected compressed snapshot group: " + decompressed.error
                    );
                    return true;
                }
                if (decompressed.encoding == simnet::CompressionEncoding::Raw)
                {
                    ++compression_report.whole_update_raw_fallback_count;
                }
                encoded_bytes = decompression_scratch;
            }
            if (compression.mode == simnet::app::CompressionMode::None)
            {
                receive_evidence.uncompressed_bytes =
                    static_cast<std::uint32_t>(encoded_bytes.size());
            }
            auto const outer_bytes =
                reassembled.completed.group_id == 0U
                    ? static_cast<std::uint32_t>(packet.payload.size())
                    : group_transport_bytes(
                          pending_compression_groups,
                          reassembled.completed.group_id,
                          reassembled.completed.total_packet_bytes
                      );
            auto const apply_outcome = apply_packet(
                evidence_identity,
                peer_id,
                receive_evidence,
                reassembled.completed,
                encoded_bytes,
                packetization.enabled,
                reassembly_state,
                pipeline,
                decode_state,
                latest_applied_sequence,
                ack_tracker,
                snapshot_history,
                world,
                transport,
                stats,
                measurements,
                csv,
                logged_multi_packet_application,
                recovery_request_state,
                sequence_gap_count,
                latest_applied_upserts,
                latest_applied_deletes
            );
            valid = apply_outcome != ApplyPacketOutcome::Fatal;
            if (apply_outcome == ApplyPacketOutcome::Applied)
            {
                compression_report.latest_completed_representation_bytes =
                    static_cast<std::uint32_t>(encoded_bytes.size());
                compression_report.latest_completed_transport_bytes = outer_bytes;
                compression_report.canonical_entity_count =
                    snapshot_history.empty()
                        ? 0U
                        : static_cast<std::uint32_t>(snapshot_history.back().snapshot.size());
                std::erase_if(
                    pending_compression_groups,
                    [&](PendingCompressionGroup const& group)
                    {
                        return group.group_id <= reassembled.completed.group_id;
                    }
                );
                if (compression.mode != simnet::app::CompressionMode::None &&
                    !logged_compression_application)
                {
                    logged_compression_application = true;
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Info,
                        "client compressed group applied group_id=" +
                            std::to_string(reassembled.completed.group_id) +
                            " compression_mode=" +
                            std::string{simnet::app::compression_mode_name(compression.mode)} +
                            " representation_bytes=" + std::to_string(encoded_bytes.size()) +
                            " transport_bytes=" + std::to_string(outer_bytes) +
                            " compressed_packets=" +
                            std::to_string(compression_report.compressed_packet_count) +
                            " raw_packets=" +
                            std::to_string(compression_report.raw_packet_count) +
                            " canonical_entities=" +
                            std::to_string(compression_report.canonical_entity_count) +
                            " ack_sequence=" +
                            std::to_string(ack_tracker.value.newest_applied_snapshot)
                    );
                }
            }
        }
        else
        {
            auto outcome = simnet::ClientReplicationOutcome::PacketInvalid;
            auto detail = std::string_view{"reassembly_invalid"};
            if (!valid)
            {
                outcome = simnet::ClientReplicationOutcome::DeliveryMismatch;
                detail = "snapshot_delivery_mismatch";
            }
            else if (reassembled.kind == simnet::ReassemblyResultKind::Incomplete)
            {
                outcome = simnet::ClientReplicationOutcome::PacketIncomplete;
                detail = "group_incomplete";
            }
            else if (reassembled.kind == simnet::ReassemblyResultKind::Duplicate)
            {
                outcome = simnet::ClientReplicationOutcome::PacketDuplicate;
                detail = "duplicate_chunk";
            }
            else if (reassembled.kind == simnet::ReassemblyResultKind::Stale)
            {
                outcome = simnet::ClientReplicationOutcome::PacketStale;
                detail = "stale_group";
            }
            else if (reassembled.kind == simnet::ReassemblyResultKind::LimitExceeded)
            {
                detail = "reassembly_limit_exceeded";
            }
            observe_client_receive_outcome(
                measurements,
                csv,
                evidence_identity,
                peer_id,
                ack_tracker,
                receive_evidence,
                outcome,
                detail
            );
        }

        if (reassembled.kind == simnet::ReassemblyResultKind::Invalid ||
            reassembled.kind == simnet::ReassemblyResultKind::LimitExceeded)
        {
            if (compression.mode == simnet::app::CompressionMode::PerPacket &&
                reassembled.group_id != 0U)
            {
                std::erase_if(
                    pending_compression_groups,
                    [&](PendingCompressionGroup const& group)
                    {
                        return group.group_id == reassembled.group_id;
                    }
                );
            }
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Warn,
                "client rejected snapshot chunk: " + reassembled.error
            );
        }
        return valid;
    }

    void ClientReplicationReceiver::log_measurements() const
    {
        log_client_replication_measurements(measurements);
    }
}
