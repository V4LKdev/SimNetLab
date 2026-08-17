module;

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <flecs.h>
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
    struct RetainedClientSnapshot
    {
        simnet::SequenceId sequence{};
        simnet::WorldSnapshot snapshot{};
        std::uint64_t capacity_bytes{};
    };

    struct PacketDecompressionObservation
    {
        simnet::CompressionEncoding encoding{simnet::CompressionEncoding::Raw};
        std::uint32_t input_bytes{};
        std::uint32_t output_bytes{};
        simnet::Nanoseconds elapsed_time{};
    };

    struct ClientEvidenceIdentity
    {
        std::uint64_t runtime_config_fingerprint{};
        std::uint64_t network_compatibility_fingerprint{};
        std::uint64_t application_wire_fingerprint{};
    };

    struct ClientReceiveEvidence
    {
        simnet::PacketGroupId group_id{};
        std::uint32_t received_packet_bytes{};
        std::string_view decompression_encoding{"disabled"};
        std::uint32_t decompression_input_bytes{};
        std::uint32_t decompression_output_bytes{};
        simnet::Nanoseconds decompression_elapsed_time{};
    };

    constexpr void flatten_packet_decompression(
        ClientReceiveEvidence& receive,
        simnet::app::CompressionMode mode,
        bool delivery_valid,
        PacketDecompressionObservation const& decompression
    ) noexcept
    {
        if (!delivery_valid || mode != simnet::app::CompressionMode::PerPacket)
        {
            if (mode != simnet::app::CompressionMode::None)
            {
                receive.decompression_encoding = "not_required";
            }
            return;
        }
        receive.decompression_encoding =
            decompression.encoding == simnet::CompressionEncoding::Raw ? "raw" : "zstd";
        receive.decompression_input_bytes = decompression.input_bytes;
        receive.decompression_output_bytes = decompression.output_bytes;
        receive.decompression_elapsed_time = decompression.elapsed_time;
    }

    [[nodiscard]] consteval bool packet_decompression_is_local(
        PacketDecompressionObservation decompression,
        std::string_view encoding,
        std::uint32_t received_packet_bytes,
        std::uint32_t input_bytes,
        std::uint32_t output_bytes
    )
    {
        auto receive = ClientReceiveEvidence{.received_packet_bytes = received_packet_bytes};
        flatten_packet_decompression(
            receive,
            simnet::app::CompressionMode::PerPacket,
            true,
            decompression
        );
        return receive.decompression_encoding == encoding &&
               receive.received_packet_bytes == received_packet_bytes &&
               receive.decompression_input_bytes == input_bytes &&
               receive.decompression_output_bytes == output_bytes &&
               receive.decompression_elapsed_time == decompression.elapsed_time;
    }

    static_assert(packet_decompression_is_local(
        {
            .encoding = simnet::CompressionEncoding::Zstd,
            .input_bytes = 101U,
            .output_bytes = 151U,
            .elapsed_time = simnet::Nanoseconds{11},
        },
        "zstd",
        113U,
        101U,
        151U
    ));
    static_assert(packet_decompression_is_local(
        {
            .encoding = simnet::CompressionEncoding::Raw,
            .input_bytes = 103U,
            .output_bytes = 103U,
            .elapsed_time = simnet::Nanoseconds{13},
        },
        "raw",
        103U,
        103U,
        103U
    ));
    static_assert(packet_decompression_is_local(
        {
            .encoding = simnet::CompressionEncoding::Zstd,
            .input_bytes = 107U,
            .output_bytes = 157U,
            .elapsed_time = simnet::Nanoseconds{17},
        },
        "zstd",
        119U,
        107U,
        157U
    ));

    [[nodiscard]] consteval bool decompression_is_not_fabricated(
        simnet::app::CompressionMode mode,
        std::string_view encoding
    )
    {
        auto receive = ClientReceiveEvidence{.received_packet_bytes = 127U};
        flatten_packet_decompression(receive, mode, true, {});
        return receive.received_packet_bytes == 127U &&
               receive.decompression_encoding == encoding &&
               receive.decompression_input_bytes == 0U &&
               receive.decompression_output_bytes == 0U &&
               receive.decompression_elapsed_time == simnet::Nanoseconds{};
    }

    static_assert(decompression_is_not_fabricated(simnet::app::CompressionMode::None, "disabled"));
    static_assert(
        decompression_is_not_fabricated(simnet::app::CompressionMode::WholeUpdate, "not_required")
    );

    struct ClientCompressionReport
    {
        simnet::app::CompressionMode mode{simnet::app::CompressionMode::None};
        simnet::CompressionEncoding latest_encoding{simnet::CompressionEncoding::Raw};
        std::uint64_t raw_packet_count{};
        std::uint64_t compressed_packet_count{};
        std::uint32_t latest_completed_representation_bytes{};
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
        void expire(simnet::Nanoseconds now);
        [[nodiscard]] bool receive_snapshot(
            simnet::ReceivedPacket const& packet,
            simnet::PeerId peer_id,
            simnet::Nanoseconds now,
            flecs::world& world,
            simnet::TransportClient& transport,
            simnet::RuntimeStats& stats,
            simnet::ClientReplicationCsvWriter& csv
        );
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
        simnet::app::ClientRecoveryRequestState recovery_request_state{};
        std::uint64_t sequence_gap_count{};
        std::uint64_t reliable_promotion_count{};
        std::optional<simnet::TransportDelivery> effective_snapshot_delivery{};
        simnet::ReassemblyState reassembly_state{};
        simnet::ZstdDecompressor decompressor{};
        std::vector<simnet::Byte> decompression_scratch{};
        ClientCompressionReport compression_report{};
        std::deque<RetainedClientSnapshot> snapshot_history{};
        bool logged_multi_packet_application{};
        bool logged_compression_application{};
        simnet::ClientReplicationMeasurements measurements{};
    };

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
        ClientReceiveEvidence const& receive
    ) noexcept
    {
        return {
            .runtime_config_fingerprint = identity.runtime_config_fingerprint,
            .network_compatibility_fingerprint = identity.network_compatibility_fingerprint,
            .application_wire_fingerprint = identity.application_wire_fingerprint,
            .peer_id = peer_id,
            .packet_group_id = receive.group_id,
            .received_packet_bytes = receive.received_packet_bytes,
            .decompression_encoding = receive.decompression_encoding,
            .decompression_input_bytes = receive.decompression_input_bytes,
            .decompression_output_bytes = receive.decompression_output_bytes,
            .decompression_elapsed_time = receive.decompression_elapsed_time,
        };
    }

    constexpr void flatten_update_identity(
        simnet::ClientReplicationMeasurement& measurement,
        simnet::EncodedUpdateHeaderInspection const& inspected,
        std::uint32_t encoded_update_bytes
    ) noexcept
    {
        if (inspected.error != simnet::EncodedUpdateHeaderError::None)
        {
            return;
        }
        measurement.tick = inspected.tick;
        measurement.sequence = inspected.sequence;
        measurement.baseline_sequence = inspected.baseline_sequence;
        measurement.snapshot_kind =
            inspected.snapshot_kind == simnet::SnapshotKind::FullReplace ? "full_replace" : "patch";
        measurement.encoded_update_bytes = encoded_update_bytes;
    }

    [[nodiscard]] consteval bool unavailable_update_identity_is_explicit()
    {
        auto measurement = simnet::ClientReplicationMeasurement{.packet_group_id = 41U};
        flatten_update_identity(
            measurement,
            {
                .error = simnet::EncodedUpdateHeaderError::Truncated,
                .tick = 7U,
                .sequence = 41U,
                .snapshot_kind = simnet::SnapshotKind::FullReplace,
            },
            73U
        );
        return measurement.packet_group_id == 41U && measurement.tick == 0U &&
               measurement.sequence == 0U && measurement.baseline_sequence == 0U &&
               measurement.snapshot_kind == "not_available" &&
               measurement.encoded_update_bytes == 0U;
    }

    [[nodiscard]] consteval bool available_update_identity_is_decoded()
    {
        auto measurement = simnet::ClientReplicationMeasurement{.packet_group_id = 43U};
        flatten_update_identity(
            measurement,
            {
                .tick = 9U,
                .sequence = 43U,
                .baseline_sequence = 42U,
                .snapshot_kind = simnet::SnapshotKind::Patch,
            },
            79U
        );
        return measurement.packet_group_id == 43U && measurement.tick == 9U &&
               measurement.sequence == 43U && measurement.baseline_sequence == 42U &&
               measurement.snapshot_kind == "patch" && measurement.encoded_update_bytes == 79U;
    }

    static_assert(unavailable_update_identity_is_explicit());
    static_assert(available_update_identity_is_decoded());

    void observe_client_receive_outcome(
        simnet::ClientReplicationMeasurements& measurements,
        simnet::ClientReplicationCsvWriter& csv,
        ClientEvidenceIdentity const& identity,
        simnet::PeerId peer_id,
        ClientReceiveEvidence const& receive,
        simnet::ClientReplicationOutcome outcome,
        std::string_view detail
    )
    {
        auto measurement = make_client_measurement(identity, peer_id, receive);
        measurement.outcome = outcome;
        measurement.outcome_detail = detail;
        observe_client_measurement(measurements, csv, measurement);
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
        auto measurement = make_client_measurement(evidence_identity, peer_id, receive_evidence);
        auto const decode_to_applied_start = simnet::steady_now_ns();
        auto const decode_start = simnet::steady_now_ns();
        auto const inspected =
            simnet::inspect_encoded_update_header(pipeline, decode_state, encoded_bytes);
        measurement.decode_elapsed_time = simnet::steady_now_ns() - decode_start;
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
        flatten_update_identity(
            measurement,
            inspected,
            static_cast<std::uint32_t>(encoded_bytes.size())
        );
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

        auto const* baseline = static_cast<simnet::WorldSnapshot const*>(nullptr);
        if (inspected.baseline_sequence != 0U)
        {
            baseline = find_retained_snapshot(snapshot_history, inspected.baseline_sequence);
            if (baseline == nullptr)
            {
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
        if (latest_applied_sequence != 0U && decoded.report.sequence > latest_applied_sequence + 1U)
        {
            ++sequence_gap_count;
        }

        auto reconstructed = simnet::WorldSnapshot{};
        // Decode validated the update. The baseline is locally empty or a retained reconstruction.
        auto const reconstruction =
            simnet::reconstruct_world_snapshot_unchecked(baseline, decoded.update, reconstructed);
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
        auto const baseline_is_current = baseline != nullptr && !snapshot_history.empty() &&
                                         baseline == &snapshot_history.back().snapshot;
        auto replacement = simnet::SnapshotUpdate{};
        auto const* patch_to_apply = &decoded.update;
        if (decoded.update.kind == simnet::SnapshotKind::Patch && !baseline_is_current)
        {
            replacement = make_full_replace_patch(reconstructed);
            patch_to_apply = &replacement;
        }

        auto applied = simnet::ApplyPatchReport{};
        {
            SIMNET_TRACE_SCOPE_CATEGORY("client.snapshot_apply", simnet::LogCategory::Simulation);
            // The update passed decode validation or was built from successful reconstruction.
            applied = simnet::apply_client_snapshot_patch_unchecked(world, *patch_to_apply);
        }
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

        decode_state = candidate_decode_state;
        latest_applied_sequence = decoded.report.sequence;
        latest_applied_upserts = static_cast<std::uint32_t>(decoded.update.upserts.size());
        latest_applied_deletes = static_cast<std::uint32_t>(decoded.update.deletes.size());
        retain_snapshot(snapshot_history, decoded.report.sequence, std::move(reconstructed));
        simnet::app::record_snapshot_progress(recovery_request_state);
        stats.ticks = applied.tick;
        simnet::commit_reassembled_group(reassembly_state, decoded.report.sequence);
        measurement.outcome = simnet::ClientReplicationOutcome::Applied;
        measurement.outcome_detail = "committed";
        measurement.decode_to_applied_elapsed_time =
            simnet::steady_now_ns() - decode_to_applied_start;
        measurement.canonical_entity_count =
            static_cast<std::uint32_t>(snapshot_history.back().snapshot.size());
        measurement.canonical_fingerprint =
            simnet::app::snapshot_diagnostic_fingerprint(snapshot_history.back().snapshot);
        observe_client_measurement(measurements, csv, measurement);
        auto sent = simnet::TransportResult{};
        {
            SIMNET_TRACE_SCOPE_CATEGORY("client.snapshot_ack", simnet::LogCategory::Transport);
            auto const bytes = simnet::app::encode_snapshot_ack({
                .newest_applied_snapshot = latest_applied_sequence,
            });
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
                    " ack_sequence=" + std::to_string(latest_applied_sequence)
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
    }

    void ClientReplicationReceiver::begin_session()
    {
        simnet::app::record_snapshot_progress(recovery_request_state);
        effective_snapshot_delivery.reset();
        simnet::clear_reassembly_state(reassembly_state);
    }

    void ClientReplicationReceiver::discard_incomplete()
    {
        simnet::clear_reassembly_state(reassembly_state);
    }

    void ClientReplicationReceiver::expire(simnet::Nanoseconds now)
    {
        simnet::expire_incomplete_groups(packetization, reassembly_state, now);
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
                compression_report.latest_encoding = decompressed.encoding;
                packet_decompression = {
                    .encoding = decompressed.encoding,
                    .input_bytes = decompressed.input_bytes,
                    .output_bytes = decompressed.output_bytes,
                    .elapsed_time = decompressed.decompression_elapsed_time,
                };
                if (!decompressed.valid ||
                    decompressed.encoding != simnet::CompressionEncoding::Zstd)
                {
                    observe_client_receive_outcome(
                        measurements,
                        csv,
                        evidence_identity,
                        peer_id,
                        {
                            .received_packet_bytes =
                                static_cast<std::uint32_t>(packet.payload.size()),
                            .decompression_encoding = "invalid",
                            .decompression_input_bytes = decompressed.input_bytes,
                            .decompression_output_bytes = decompressed.output_bytes,
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
        auto receive_evidence = ClientReceiveEvidence{
            .group_id = reassembled.group_id,
            .received_packet_bytes = static_cast<std::uint32_t>(packet.payload.size()),
        };
        flatten_packet_decompression(
            receive_evidence,
            compression.mode,
            valid,
            packet_decompression
        );
        if (reassembled.kind == simnet::ReassemblyResultKind::Complete)
        {
            receive_evidence.group_id = reassembled.completed.group_id;
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
                compression_report.latest_encoding = decompressed.encoding;
                receive_evidence.decompression_encoding =
                    decompressed.encoding == simnet::CompressionEncoding::Raw ? "raw" : "zstd";
                receive_evidence.decompression_input_bytes = decompressed.input_bytes;
                receive_evidence.decompression_output_bytes = decompressed.output_bytes;
                receive_evidence.decompression_elapsed_time =
                    decompressed.decompression_elapsed_time;
                if (!decompressed.valid)
                {
                    receive_evidence.decompression_encoding = "invalid";
                    observe_client_receive_outcome(
                        measurements,
                        csv,
                        evidence_identity,
                        peer_id,
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
                encoded_bytes = decompression_scratch;
            }
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
                            " compressed_packets=" +
                            std::to_string(compression_report.compressed_packet_count) +
                            " raw_packets=" +
                            std::to_string(compression_report.raw_packet_count) +
                            " canonical_entities=" +
                            std::to_string(snapshot_history.back().snapshot.size()) +
                            " ack_sequence=" + std::to_string(latest_applied_sequence)
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
                receive_evidence,
                outcome,
                detail
            );
        }

        if (reassembled.kind == simnet::ReassemblyResultKind::Invalid ||
            reassembled.kind == simnet::ReassemblyResultKind::LimitExceeded)
        {
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Warn,
                "client rejected snapshot chunk: " + reassembled.error
            );
        }
        return valid;
    }
}
