module;

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <flecs.h>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <simnet/telemetry_trace.hpp>

#include "server_peer_iteration.hpp"

module simnet.server_runtime:replication;

import simnet.app_common;
import simnet.app_evidence;
import simnet.app_protocol;
import simnet.app_snapshot_delivery;
import simnet.compression;
import simnet.config;
import simnet.core;
import simnet.game_server;
import simnet.game_shared;
import simnet.packetization;
import simnet.pipeline;
import simnet.runtime;
import simnet.snapshot;
import simnet.spatial;
import simnet.telemetry;
import simnet.transport;
#if defined(SIMNET_ENABLE_SYNTHETIC)
import simnet.synthetic;
#endif

namespace simnet::app::server_replication
{
    struct PreparedTransportPayload
    {
        std::uint32_t offset{};
        std::uint32_t size{};
        simnet::CompressionEncoding encoding{simnet::CompressionEncoding::Raw};
    };

    struct PreparedTransportGroup
    {
        std::vector<simnet::Byte> bytes{};
        std::vector<PreparedTransportPayload> payloads{};
    };

    struct ServerCompressionReport
    {
        simnet::app::CompressionMode mode{simnet::app::CompressionMode::None};
        simnet::PacketGroupId group_id{};
        std::uint32_t representation_bytes{};
        std::uint32_t compression_input_bytes{};
        std::uint32_t compression_payload_bytes{};
        std::uint32_t compression_envelope_bytes{};
        std::uint32_t compression_output_bytes{};
        std::uint32_t bytes_after_packetization{};
        std::uint32_t final_transport_bytes{};
        std::uint32_t zstd_packet_count{};
        std::uint32_t raw_packet_count{};
        double ratio{1.0};
        simnet::CompressionEncoding whole_encoding{simnet::CompressionEncoding::Raw};
        simnet::Nanoseconds compression_elapsed_time{};
        bool valid{};
        std::string error{};
    };

    struct CurrentSnapshotState
    {
        simnet::WorldSnapshot snapshot{};
        simnet::Tick extracted_tick{};
        bool valid{};
        bool dirty{true};
    };

    struct AreaOfInterestGridState
    {
        simnet::SpatialGrid grid{};
        simnet::SpatialGridScratch scratch{};
        simnet::Tick snapshot_tick{};
        bool area_of_interest_grid_valid{};
    };

    enum class PacketSubmissionOutcome : std::uint8_t
    {
        None,
        Prepared,
        Committed,
        Abandoned
    };

    [[nodiscard]] constexpr std::string_view
    level_of_detail_mode_name(simnet::LevelOfDetailMode mode) noexcept
    {
        return mode == simnet::LevelOfDetailMode::DistanceBands ? "distance_bands" : "none";
    }

    [[nodiscard]] constexpr std::string_view
    area_of_interest_mode_name(simnet::AreaOfInterestMode mode) noexcept
    {
        switch (mode)
        {
            case simnet::AreaOfInterestMode::None:
                return "none";
            case simnet::AreaOfInterestMode::Radius:
                return "radius";
            case simnet::AreaOfInterestMode::Fov:
                return "fov";
        }
        return "unknown";
    }

    [[nodiscard]] constexpr std::string_view
    entity_record_layout_name(simnet::EntityRecordLayout layout) noexcept
    {
        switch (layout)
        {
            case simnet::EntityRecordLayout::Raw:
                return "raw";
            case simnet::EntityRecordLayout::Quantized:
                return "quantized";
            case simnet::EntityRecordLayout::QuantizedOctHeading:
                return "quantized_oct_heading";
            case simnet::EntityRecordLayout::BitPackedQuantizedOctHeading:
                return "bit_packed_quantized_oct_heading";
        }
        return "unknown";
    }

    struct PeerRuntimeState
    {
        simnet::PeerId peer{};
        std::optional<simnet::app::ClientRole> role{};
        simnet::EntityNetId player_id{};
        simnet::ClientReplicationState pipeline_state{};
        simnet::PipelineScratch pipeline_scratch{};
        std::vector<std::uint32_t> area_of_interest_candidates{};
        simnet::app::StationaryObserverInterestState stationary_observer_interest{};
        simnet::AreaOfInterestReport latest_area_of_interest{};
        simnet::LevelOfDetailReport latest_level_of_detail{};
        simnet::RepresentationReport latest_representation{};
        bool has_area_of_interest_report{};
        bool has_representation_report{};
        std::uint32_t latest_upsert_count{};
        std::uint32_t latest_delete_count{};
        simnet::app::SnapshotDeliveryState snapshot_delivery{};
        std::vector<simnet::EntityNetId> recovery_upsert_ids{};
        simnet::ZstdCompressor compressor{};
        std::vector<simnet::Byte> compression_scratch{};
        std::vector<simnet::Byte> packet_serialization_scratch{};
        PreparedTransportGroup prepared_transport_group{};
        ServerCompressionReport latest_compression{};
        std::uint64_t whole_update_raw_fallback_count{};
        simnet::Nanoseconds whole_update_compression_elapsed_time{};
        simnet::PacketizationReport latest_packetization{};
        std::uint32_t latest_attempted_submissions{};
        std::uint32_t latest_accepted_submissions{};
        PacketSubmissionOutcome latest_submission_outcome{PacketSubmissionOutcome::None};
        std::string latest_submission_error{};
        bool logged_multi_packet_commit{};
        std::uint64_t reliable_group_count{};
        std::uint64_t unreliable_group_count{};
        std::uint64_t reliable_packet_count{};
        std::uint64_t unreliable_packet_count{};
        std::uint64_t repeated_recovery_upserts{};
        std::uint64_t repeated_recovery_deletes{};
        std::uint64_t level_of_detail_full_replace_override_count{};
        std::uint64_t committed_emission_count{};
        std::uint64_t cadence_skip_count{};
    };

    using PeerRuntimeStates = std::vector<PeerRuntimeState>;

    [[nodiscard]] auto find_peer(PeerRuntimeStates& peers, simnet::PeerId peer)
    {
        return std::ranges::lower_bound(peers, peer, {}, &PeerRuntimeState::peer);
    }

    [[nodiscard]] constexpr std::string_view
    peer_role_name(std::optional<simnet::app::ClientRole> role) noexcept
    {
        if (role == simnet::app::ClientRole::Player)
        {
            return "player";
        }
        if (role == simnet::app::ClientRole::StationaryObserver)
        {
            return "stationary_observer";
        }
        return "unjoined";
    }

    void log_snapshot_delivery_state(PeerRuntimeState const& peer, std::string_view configured_mode)
    {
        auto const& delivery_state = peer.snapshot_delivery;
        auto const acknowledged_entities = delivery_state.acknowledged.has_value()
                                               ? delivery_state.acknowledged->snapshot.size()
                                               : 0U;
        auto const acknowledged_fingerprint = delivery_state.acknowledged.has_value()
                                                  ? simnet::app::snapshot_diagnostic_fingerprint(
                                                        delivery_state.acknowledged->snapshot
                                                    )
                                                  : 0U;
        auto const quality_samples =
            static_cast<double>(peer.latest_representation.quality_sample_count);
        auto const mean_position_error =
            quality_samples == 0.0
                ? 0.0
                : peer.latest_representation.position_error_sum / quality_samples;
        auto const mean_heading_error_degrees =
            quality_samples == 0.0
                ? 0.0
                : peer.latest_representation.heading_angular_error_degrees_sum / quality_samples;
        simnet::log(
            simnet::LogCategory::Transport,
            simnet::LogLevel::Info,
            "server snapshot delivery peer_id=" + std::to_string(peer.peer) +
                " role=" + std::string{peer_role_name(peer.role)} + " player_id=" +
                std::to_string(peer.player_id) + " mode=" + std::string{configured_mode} +
                " submitted_sequence=" + std::to_string(delivery_state.latest_submitted_sequence) +
                " acknowledged_sequence=" +
                std::to_string(delivery_state.latest_acknowledged_sequence) +
                " acknowledged_entities=" + std::to_string(acknowledged_entities) +
                " acknowledged_fingerprint=" + std::to_string(acknowledged_fingerprint) +
                " reliable_groups=" + std::to_string(peer.reliable_group_count) +
                " unreliable_groups=" + std::to_string(peer.unreliable_group_count) +
                " reliable_packets=" + std::to_string(peer.reliable_packet_count) +
                " unreliable_packets=" + std::to_string(peer.unreliable_packet_count) +
                " recovery_reason=" +
                std::string{
                    simnet::app::snapshot_recovery_reason_name(delivery_state.recovery_reason)
                } +
                " lod_mode=" +
                std::string{level_of_detail_mode_name(peer.latest_level_of_detail.mode)} +
                " lod_near_population=" +
                std::to_string(peer.latest_level_of_detail.population.near) +
                " lod_medium_population=" +
                std::to_string(peer.latest_level_of_detail.population.medium) +
                " lod_far_population=" +
                std::to_string(peer.latest_level_of_detail.population.far) + " lod_pending_due=" +
                std::to_string(peer.latest_level_of_detail.pending_due_count) +
                " lod_full_replace_overrides=" +
                std::to_string(peer.level_of_detail_full_replace_override_count) +
                " record_layout=" +
                std::string{entity_record_layout_name(peer.latest_representation.layout)} +
                " record_bytes=" + std::to_string(peer.latest_representation.record_bytes) +
                " quality_samples=" +
                std::to_string(peer.latest_representation.quality_sample_count) +
                " mean_position_error=" + std::to_string(mean_position_error) +
                " maximum_position_error=" +
                std::to_string(peer.latest_representation.position_error_maximum) +
                " mean_heading_error_degrees=" + std::to_string(mean_heading_error_degrees) +
                " maximum_heading_error_degrees=" +
                std::to_string(peer.latest_representation.heading_angular_error_degrees_maximum) +
                " committed_emissions=" + std::to_string(peer.committed_emission_count) +
                " cadence_skips=" + std::to_string(peer.cadence_skip_count)
        );
        if (peer.latest_compression.mode == simnet::app::CompressionMode::WholeUpdate)
        {
            simnet::log(
                simnet::LogCategory::Pipeline,
                simnet::LogLevel::Info,
                "server whole-update compression peer_id=" + std::to_string(peer.peer) +
                    " sequence=" + std::to_string(peer.latest_compression.group_id) +
                    " source_bytes=" +
                    std::to_string(peer.latest_compression.compression_input_bytes) +
                    " payload_bytes=" +
                    std::to_string(peer.latest_compression.compression_payload_bytes) +
                    " envelope_bytes=" +
                    std::to_string(peer.latest_compression.compression_envelope_bytes) +
                    " output_bytes=" +
                    std::to_string(peer.latest_compression.compression_output_bytes) +
                    " packet_bytes=" +
                    std::to_string(peer.latest_compression.bytes_after_packetization) +
                    " transport_bytes=" +
                    std::to_string(peer.latest_compression.final_transport_bytes) +
                    " latest_compression_elapsed_ns=" +
                    std::to_string(
                        std::max(
                            peer.latest_compression.compression_elapsed_time,
                            simnet::Nanoseconds{}
                        )
                            .count()
                    ) +
                    " raw_fallbacks=" + std::to_string(peer.whole_update_raw_fallback_count) +
                    " compression_elapsed_ns=" +
                    std::to_string(
                        std::max(peer.whole_update_compression_elapsed_time, simnet::Nanoseconds{})
                            .count()
                    )
            );
        }
        for (auto const& retained : delivery_state.submitted)
        {
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Info,
                "server retained result peer_id=" + std::to_string(peer.peer) +
                    " role=" + std::string{peer_role_name(peer.role)} +
                    " sequence=" + std::to_string(retained.sequence) +
                    " entities=" + std::to_string(retained.snapshot.size()) + " fingerprint=" +
                    std::to_string(simnet::app::snapshot_diagnostic_fingerprint(retained.snapshot))
            );
        }
    }

    [[nodiscard]] bool prepare_transport_group(
        simnet::app::CompressionSettings const& compression,
        simnet::PacketizationSettings const& packetization,
        std::uint32_t transport_payload_limit,
        simnet::PreparedByteGroup const& prepared,
        PeerRuntimeState& peer,
        ServerCompressionReport& report
    )
    {
        auto const maximum_storage =
            compression.mode == simnet::app::CompressionMode::PerPacket
                ? static_cast<std::uint64_t>(prepared.chunk_count) * transport_payload_limit
                : prepared.report.total_packet_bytes;
        if (maximum_storage > std::numeric_limits<std::uint32_t>::max() ||
            maximum_storage > peer.prepared_transport_group.bytes.max_size())
        {
            report.error = "transport group storage bound is invalid";
            return false;
        }

        auto& transport_group = peer.prepared_transport_group;
        transport_group.bytes.clear();
        transport_group.payloads.clear();
        transport_group.bytes.reserve(static_cast<std::size_t>(maximum_storage));
        transport_group.payloads.reserve(prepared.chunk_count);

        for (auto index = std::uint16_t{}; index < prepared.chunk_count; ++index)
        {
            auto const packet = simnet::serialize_group_chunk(
                packetization,
                prepared,
                index,
                peer.packet_serialization_scratch
            );
            auto payload = packet;
            auto encoding = simnet::CompressionEncoding::Raw;
            if (compression.mode == simnet::app::CompressionMode::PerPacket)
            {
                auto const compressed = simnet::compress_bytes(
                    peer.compressor,
                    packet,
                    compression.level,
                    {
                        .max_uncompressed_bytes = packetization.max_payload_bytes,
                        .max_output_bytes = transport_payload_limit,
                    },
                    simnet::CompressionEnvelopePolicy::OnlyWhenSmaller,
                    peer.compression_scratch
                );
                if (!compressed.valid)
                {
                    report.error = compressed.error;
                    return false;
                }
                payload = simnet::ByteSpan{peer.compression_scratch};
                encoding = compressed.encoding;
                report.compression_input_bytes += compressed.input_bytes;
                report.compression_payload_bytes += compressed.encoded_payload_bytes;
                report.compression_envelope_bytes += compressed.envelope_bytes;
                report.compression_output_bytes += compressed.output_bytes;
                report.compression_elapsed_time += compressed.compression_elapsed_time;
                if (compressed.encoding == simnet::CompressionEncoding::Zstd)
                {
                    ++report.zstd_packet_count;
                }
                else
                {
                    ++report.raw_packet_count;
                }
            }
            std::uint64_t next_size = transport_group.bytes.size();
            next_size += payload.size();
            if (next_size > maximum_storage ||
                next_size > std::numeric_limits<std::uint32_t>::max())
            {
                report.error = "transport group exceeds its validated storage bound";
                return false;
            }

            auto const offset = static_cast<std::uint32_t>(transport_group.bytes.size());
            transport_group.bytes.insert(
                transport_group.bytes.end(),
                payload.begin(),
                payload.end()
            );
            transport_group.payloads.push_back({
                .offset = offset,
                .size = static_cast<std::uint32_t>(payload.size()),
                .encoding = encoding,
            });
        }
        report.final_transport_bytes = static_cast<std::uint32_t>(transport_group.bytes.size());
        report.valid = true;
        return true;
    }

    [[nodiscard]] std::optional<simnet::InterestSource>
    resolve_interest_source(PeerRuntimeState const& peer, simnet::WorldSnapshot const& snapshot);

    [[nodiscard]] bool advance_world(
        flecs::world& world,
        simnet::ServerGameRuntime& game,
        simnet::Nanoseconds fixed_dt
    )
    {
        SIMNET_TRACE_SCOPE_CATEGORY(
            "server.fixed_step.world_advance",
            simnet::LogCategory::Simulation
        );
        if (!simnet::prepare_server_game_runtime(world, game))
        {
            return false;
        }
        auto const seconds = std::chrono::duration<float>(fixed_dt).count();
        return world.progress(seconds) && game.last_step_report().valid;
    }

    [[nodiscard]] bool valid_ack(PeerRuntimeState const& peer, simnet::app::SnapshotAck const& ack)
    {
        return ack.newest_applied_snapshot != 0U &&
               ack.newest_applied_snapshot <= peer.snapshot_delivery.latest_submitted_sequence;
    }

    [[nodiscard]] simnet::ServerSnapshotExtractionReport ensure_current_snapshot(
        flecs::world const& world,
        simnet::Tick tick,
        CurrentSnapshotState& state
    )
    {
        if (state.valid && !state.dirty)
        {
            return {
                .tick = state.extracted_tick,
                .entity_count = static_cast<std::uint32_t>(state.snapshot.size()),
            };
        }

        SIMNET_TRACE_SCOPE_CATEGORY(
            "server.fixed_step.snapshot_demand",
            simnet::LogCategory::Snapshot
        );
        auto const report = simnet::extract_world_snapshot(world, tick, state.snapshot);
        if (!report.valid)
        {
            state.valid = false;
            state.dirty = true;
            return report;
        }
        state.extracted_tick = tick;
        state.valid = true;
        state.dirty = false;
        return report;
    }

    void ensure_area_of_interest_grid(
        simnet::WorldSnapshot const& snapshot,
        AreaOfInterestGridState& state,
        simnet::SpatialGridSettings const& settings
    )
    {
        if (state.area_of_interest_grid_valid && state.snapshot_tick == snapshot.tick)
        {
            return;
        }

        auto& grid = state.grid;
        if (grid.dim_x == 0U || grid.settings.cell_size != settings.cell_size ||
            grid.settings.bounds.min.x != settings.bounds.min.x ||
            grid.settings.bounds.max.x != settings.bounds.max.x)
        {
            simnet::resize_spatial_grid(grid, settings);
        }
        simnet::prepare_spatial_grid_scratch(state.scratch, snapshot.size(), 1U);
        simnet::build_spatial_grid_serial(grid, state.scratch, snapshot.positions, snapshot.ids);
        state.snapshot_tick = snapshot.tick;
        state.area_of_interest_grid_valid = true;
    }

    [[nodiscard]] std::optional<simnet::InterestSource>
    resolve_interest_source(PeerRuntimeState const& peer, simnet::WorldSnapshot const& snapshot)
    {
        if (peer.role == simnet::app::ClientRole::StationaryObserver)
        {
            if (!peer.stationary_observer_interest.initialized)
            {
                return std::nullopt;
            }
            return simnet::InterestSource{
                .position = peer.stationary_observer_interest.position,
                .forward = peer.stationary_observer_interest.forward,
            };
        }
        if (peer.role != simnet::app::ClientRole::Player || peer.player_id == 0U)
        {
            return std::nullopt;
        }

        auto const found = std::ranges::lower_bound(snapshot.ids, peer.player_id);
        if (found == snapshot.ids.end() || *found != peer.player_id)
        {
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Error,
                "authoritative Player interest source is absent peer_id=" +
                    std::to_string(peer.peer) + " player_id=" + std::to_string(peer.player_id)
            );
            return std::nullopt;
        }
        auto const index = static_cast<std::size_t>(std::distance(snapshot.ids.begin(), found));
        if (snapshot.classifications[index] != simnet::player_entity_classification)
        {
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Error,
                "authoritative Player interest source has wrong classification peer_id=" +
                    std::to_string(peer.peer) + " player_id=" + std::to_string(peer.player_id)
            );
            return std::nullopt;
        }
        return simnet::InterestSource{
            .position = snapshot.positions[index],
            .forward = snapshot.headings[index],
            .source_entity_id = peer.player_id,
        };
    }

    void query_area_of_interest_candidates(
        PeerRuntimeState& peer,
        simnet::WorldSnapshot const& snapshot,
        AreaOfInterestGridState& grid_state,
        simnet::SpatialGridSettings const& grid_settings,
        simnet::InterestSource const& source,
        float radius
    )
    {
        ensure_area_of_interest_grid(snapshot, grid_state, grid_settings);
        peer.area_of_interest_candidates.clear();
        static_cast<void>(simnet::query_radius(
            grid_state.grid,
            snapshot.positions,
            source.position,
            radius,
            [&](std::uint32_t source_index)
            {
                peer.area_of_interest_candidates.push_back(source_index);
            }
        ));
        std::ranges::sort(peer.area_of_interest_candidates);
    }

    void apply_ack(PeerRuntimeState& peer, simnet::app::SnapshotAck const& ack)
    {
        auto const outcome = simnet::app::promote_snapshot_ack(
            peer.snapshot_delivery,
            ack.newest_applied_snapshot,
            simnet::steady_now_ns()
        );
        using enum simnet::app::AckPromotionOutcome;
        if (outcome == Promoted)
        {
            simnet::log(
                simnet::LogCategory::Pipeline,
                simnet::LogLevel::Info,
                "acknowledged replica promoted peer_id=" + std::to_string(peer.peer) +
                    " role=" + std::string{peer_role_name(peer.role)} +
                    " sequence=" + std::to_string(ack.newest_applied_snapshot)
            );
        }
        else if (outcome == Missing)
        {
            simnet::log(
                simnet::LogCategory::Pipeline,
                simnet::LogLevel::Warn,
                "ACK result is unavailable, entering FullReplace recovery peer_id=" +
                    std::to_string(peer.peer) + " role=" + std::string{peer_role_name(peer.role)}
            );
        }
    }

    void remove_session_player(
        flecs::world* world,
        PeerRuntimeState const& peer,
        CurrentSnapshotState* snapshot_state
    )
    {
        if (world != nullptr && snapshot_state != nullptr && peer.player_id != 0U &&
            simnet::delete_authoritative_player(*world, peer.player_id))
        {
            snapshot_state->dirty = true;
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Info,
                "server deleted disconnected player_id=" + std::to_string(peer.player_id)
            );
        }
    }

    void remove_failed_peer_state(
        flecs::world* world,
        PeerRuntimeState const& peer,
        CurrentSnapshotState* snapshot_state,
        simnet::TransportDelivery snapshot_delivery
    )
    {
        log_snapshot_delivery_state(peer, simnet::app::transport_delivery_name(snapshot_delivery));
        remove_session_player(world, peer, snapshot_state);
    }

    [[nodiscard]] bool erase_peer_state(
        flecs::world* world,
        PeerRuntimeStates& peers,
        simnet::PeerId peer_id,
        CurrentSnapshotState* snapshot_state,
        simnet::TransportDelivery snapshot_delivery
    )
    {
        auto const found = find_peer(peers, peer_id);
        if (found == peers.end() || found->peer != peer_id)
        {
            return false;
        }
        remove_failed_peer_state(world, *found, snapshot_state, snapshot_delivery);
        peers.erase(found);
        return true;
    }

    void observe_server_measurement(
        simnet::ServerReplicationMeasurements& measurements,
        simnet::ServerReplicationCsvWriter& csv,
        simnet::ServerReplicationMeasurement const& measurement
    )
    {
        measurements.observe(measurement);
        if (!csv.submit(measurement))
        {
            throw std::runtime_error(
                "server replication CSV submission failed: " + std::string{csv.error()}
            );
        }
    }

    struct ServerEvidenceIdentity
    {
        std::uint64_t runtime_config_fingerprint{};
        std::uint64_t network_compatibility_fingerprint{};
        std::uint64_t application_wire_fingerprint{};
    };

    [[nodiscard]] simnet::ServerReplicationMeasurement make_server_measurement(
        ServerEvidenceIdentity const& identity,
        PeerRuntimeState const& peer,
        simnet::Tick tick
    ) noexcept
    {
        return {
            .runtime_config_fingerprint = identity.runtime_config_fingerprint,
            .network_compatibility_fingerprint = identity.network_compatibility_fingerprint,
            .application_wire_fingerprint = identity.application_wire_fingerprint,
            .peer_id = peer.peer,
            .accepted_gameplay_role = peer_role_name(peer.role),
            .tick = tick,
            .acknowledged_sequence = peer.snapshot_delivery.latest_acknowledged_sequence,
            .recovery_reason =
                simnet::app::snapshot_recovery_reason_name(peer.snapshot_delivery.recovery_reason),
            .submissions_since_ack_progress = peer.snapshot_delivery.submissions_since_ack_progress,
        };
    }

    [[nodiscard]] constexpr std::string_view
    compression_result_name(ServerCompressionReport const& report) noexcept
    {
        if (report.mode == simnet::app::CompressionMode::None)
        {
            return "disabled";
        }
        if (report.mode == simnet::app::CompressionMode::WholeUpdate)
        {
            switch (report.whole_encoding)
            {
                case simnet::CompressionEncoding::Raw:
                    return "raw";
                case simnet::CompressionEncoding::Zstd:
                    return "zstd";
            }
        }
        if (report.zstd_packet_count != 0U && report.raw_packet_count != 0U)
        {
            return "mixed";
        }
        return report.zstd_packet_count != 0U ? "zstd" : "raw";
    }

    constexpr void flatten_transport_reports(
        simnet::ServerReplicationMeasurement& measurement,
        ServerCompressionReport const& compression,
        simnet::PacketizationReport const& packetization
    ) noexcept
    {
        measurement.compression_encoding = compression_result_name(compression);
        switch (compression.mode)
        {
            case simnet::app::CompressionMode::None:
                measurement.compression_raw_fallback = false;
                break;
            case simnet::app::CompressionMode::WholeUpdate:
                measurement.compression_raw_fallback =
                    compression.whole_encoding == simnet::CompressionEncoding::Raw;
                break;
            case simnet::app::CompressionMode::PerPacket:
                measurement.compression_raw_fallback = compression.raw_packet_count != 0U;
                break;
        }
        measurement.compression_input_bytes = compression.compression_input_bytes;
        measurement.compression_output_bytes = compression.compression_output_bytes;
        measurement.compression_elapsed_time = compression.compression_elapsed_time;
        measurement.packet_header_bytes = packetization.total_header_bytes;
    }

    [[nodiscard]] consteval bool compression_evidence_matches(
        simnet::app::CompressionMode mode,
        simnet::CompressionEncoding whole_encoding,
        std::uint32_t zstd_packet_count,
        std::uint32_t raw_packet_count,
        std::string_view encoding,
        bool raw_fallback
    )
    {
        auto measurement = simnet::ServerReplicationMeasurement{};
        flatten_transport_reports(
            measurement,
            {
                .mode = mode,
                .zstd_packet_count = zstd_packet_count,
                .raw_packet_count = raw_packet_count,
                .whole_encoding = whole_encoding,
            },
            {.total_header_bytes = 7U}
        );
        return measurement.compression_encoding == encoding &&
               measurement.compression_raw_fallback == raw_fallback &&
               measurement.packet_header_bytes == 7U;
    }

    static_assert(compression_evidence_matches(
        simnet::app::CompressionMode::None,
        simnet::CompressionEncoding::Raw,
        0U,
        0U,
        "disabled",
        false
    ));
    static_assert(compression_evidence_matches(
        simnet::app::CompressionMode::WholeUpdate,
        simnet::CompressionEncoding::Zstd,
        0U,
        0U,
        "zstd",
        false
    ));
    static_assert(compression_evidence_matches(
        simnet::app::CompressionMode::WholeUpdate,
        simnet::CompressionEncoding::Raw,
        0U,
        0U,
        "raw",
        true
    ));
    static_assert(compression_evidence_matches(
        simnet::app::CompressionMode::PerPacket,
        simnet::CompressionEncoding::Raw,
        3U,
        0U,
        "zstd",
        false
    ));
    static_assert(compression_evidence_matches(
        simnet::app::CompressionMode::PerPacket,
        simnet::CompressionEncoding::Raw,
        2U,
        1U,
        "mixed",
        true
    ));
    static_assert(compression_evidence_matches(
        simnet::app::CompressionMode::PerPacket,
        simnet::CompressionEncoding::Raw,
        0U,
        3U,
        "raw",
        true
    ));

    [[nodiscard]] bool run_tick(
        flecs::world* world,
        simnet::ServerGameRuntime* game,
#if defined(SIMNET_ENABLE_SYNTHETIC)
        simnet::SyntheticSnapshotState* synthetic_state,
        simnet::SyntheticSnapshotSettings const* synthetic_snapshot_settings,
        simnet::SyntheticChangeSettings const* synthetic_change_settings,
#endif
        simnet::Tick tick,
        simnet::Nanoseconds fixed_dt,
        simnet::PipelineDefinition const& pipeline,
        bool collect_representation_quality,
        simnet::app::CompressionSettings const& compression,
        simnet::PacketizationSettings const& packetization,
        simnet::SnapshotDeliveryConfig const& delivery_config,
        simnet::SpatialGridSettings const& area_of_interest_grid_settings,
        simnet::TransportDelivery delivery,
        ServerEvidenceIdentity const& evidence_identity,
        simnet::TransportServer& transport,
        PeerRuntimeStates& peers,
        CurrentSnapshotState* snapshot_state,
        AreaOfInterestGridState& area_of_interest_grid_state,
        simnet::ServerReplicationMeasurements& measurements,
        simnet::ServerReplicationCsvWriter& csv
    )
    {
        SIMNET_TRACE_SCOPE_CATEGORY("server.fixed_tick", simnet::LogCategory::Simulation);
        auto const* source_snapshot = static_cast<simnet::WorldSnapshot const*>(nullptr);
        auto extraction = simnet::ServerSnapshotExtractionReport{};
        if (world != nullptr)
        {
            if (game == nullptr || snapshot_state == nullptr)
            {
                simnet::log(
                    simnet::LogCategory::Simulation,
                    simnet::LogLevel::Error,
                    "game producer state is incomplete"
                );
                return false;
            }
            if (!advance_world(*world, *game, fixed_dt))
            {
                simnet::log(
                    simnet::LogCategory::Simulation,
                    simnet::LogLevel::Error,
                    "authoritative boid step failed: " + game->last_step_report().error
                );
                return false;
            }
            snapshot_state->dirty = true;
#if defined(SIMNET_ENABLE_SYNTHETIC)
        }
        else if (
            synthetic_state != nullptr && synthetic_snapshot_settings != nullptr &&
            synthetic_change_settings != nullptr
        )
        {
            source_snapshot = &simnet::update_synthetic_world_snapshot(
                *synthetic_snapshot_settings,
                *synthetic_change_settings,
                tick,
                *synthetic_state
            );
            extraction = {
                .tick = source_snapshot->tick,
                .entity_count = static_cast<std::uint32_t>(source_snapshot->size()),
            };
#endif
        }
        else
        {
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Error,
                "authoritative producer state is incomplete"
            );
            return false;
        }
        auto const have_joined_peer = std::ranges::any_of(
            peers,
            [](PeerRuntimeState const& peer)
            {
                return peer.role.has_value();
            }
        );
        if (!have_joined_peer)
        {
            return true;
        }

        if (world != nullptr)
        {
            extraction = ensure_current_snapshot(*world, tick, *snapshot_state);
            source_snapshot = &snapshot_state->snapshot;
        }
        if (!extraction.valid)
        {
            for (auto const& failed_peer : peers)
            {
                if (!failed_peer.role.has_value())
                {
                    continue;
                }
                observe_server_measurement(
                    measurements,
                    csv,
                    make_server_measurement(evidence_identity, failed_peer, tick)
                );
            }
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Error,
                "snapshot extraction failed: " + extraction.error
            );
            return false;
        }

        if (pipeline.area_of_interest.mode != simnet::AreaOfInterestMode::None)
        {
            ensure_area_of_interest_grid(
                *source_snapshot,
                area_of_interest_grid_state,
                area_of_interest_grid_settings
            );
        }

        auto replicate_peer = [&](PeerRuntimeState& peer_state)
        {
            auto* peer = &peer_state;
            auto measurement = make_server_measurement(evidence_identity, *peer, tick);
            auto const total_start = simnet::steady_now_ns();
            auto excluded_evidence_time = simnet::Nanoseconds{};
            auto const finish_total_time = [&]
            {
                return simnet::steady_now_ns() - total_start - excluded_evidence_time;
            };
            auto const delta_enabled =
                simnet::has_all_flags(pipeline.techniques, simnet::PipelineTechniqueFlags::Delta);
            bool const cadence_emits =
                simnet::should_emit_snapshot(pipeline, source_snapshot->tick);
            auto const incremental_enabled = simnet::has_all_flags(
                pipeline.techniques,
                simnet::PipelineTechniqueFlags::Incremental
            );
            auto const level_of_detail_enabled =
                pipeline.level_of_detail.mode == simnet::LevelOfDetailMode::DistanceBands;
            auto const partial_selection_enabled = incremental_enabled || level_of_detail_enabled;
            auto interest_source = std::optional<simnet::InterestSource>{};
            if (pipeline.area_of_interest.mode != simnet::AreaOfInterestMode::None)
            {
                interest_source = resolve_interest_source(*peer, *source_snapshot);
                if (!interest_source.has_value())
                {
                    measurement.source_entity_count = extraction.entity_count;
                    measurement.outcome = simnet::ServerReplicationOutcome::Skipped;
                    measurement.outcome_detail = "area_of_interest_source_unavailable";
                    measurement.total_replication_elapsed_time = finish_total_time();
                    observe_server_measurement(measurements, csv, measurement);
                    return peer->role != simnet::app::ClientRole::Player;
                }
                if (cadence_emits)
                {
                    query_area_of_interest_candidates(
                        *peer,
                        *source_snapshot,
                        area_of_interest_grid_state,
                        area_of_interest_grid_settings,
                        *interest_source,
                        pipeline.area_of_interest.radius
                    );
                }
            }
            if (cadence_emits)
            {
                simnet::app::expire_retained_snapshots(
                    peer->snapshot_delivery,
                    simnet::steady_now_ns()
                );
                if (!peer->snapshot_delivery.acknowledged.has_value() &&
                    !peer->snapshot_delivery.recovery_active)
                {
                    simnet::app::enter_snapshot_recovery(
                        peer->snapshot_delivery,
                        simnet::app::SnapshotRecoveryReason::NoAcknowledgedBaseline
                    );
                }
                else if (
                    !peer->snapshot_delivery.recovery_active &&
                    simnet::app::ack_progress_stalled(
                        peer->snapshot_delivery,
                        delivery_config.full_replace_after_unacknowledged_updates
                    )
                )
                {
                    simnet::app::enter_snapshot_recovery(
                        peer->snapshot_delivery,
                        simnet::app::SnapshotRecoveryReason::AckStalled
                    );
                }
            }
            auto const* acknowledged = peer->snapshot_delivery.acknowledged.has_value()
                                           ? &*peer->snapshot_delivery.acknowledged
                                           : nullptr;
            auto const* level_of_detail_baseline = acknowledged;
            if (cadence_emits && level_of_detail_enabled &&
                delivery == simnet::TransportDelivery::ReliableSequenced &&
                !peer->snapshot_delivery.submitted.empty())
            {
                level_of_detail_baseline = &peer->snapshot_delivery.submitted.back();
            }
            if (cadence_emits && level_of_detail_enabled &&
                peer->pipeline_state.level_of_detail_seeded &&
                !peer->snapshot_delivery.recovery_active && level_of_detail_baseline == nullptr)
            {
                simnet::app::enter_snapshot_recovery(
                    peer->snapshot_delivery,
                    simnet::app::SnapshotRecoveryReason::MissingRetainedResult
                );
            }
            auto const force_full_replace =
                cadence_emits && peer->snapshot_delivery.recovery_active;
            auto const* delta_baseline =
                cadence_emits && !force_full_replace && delta_enabled
                    ? (level_of_detail_enabled ? level_of_detail_baseline : acknowledged)
                    : nullptr;
            auto const* explicit_level_of_detail_baseline =
                cadence_emits && !force_full_replace && level_of_detail_enabled
                    ? level_of_detail_baseline
                    : nullptr;
            auto const* replica_snapshot = static_cast<simnet::WorldSnapshot const*>(nullptr);
            auto replica_sequence = simnet::SequenceId{};
            if (cadence_emits && incremental_enabled && !level_of_detail_enabled &&
                !delta_enabled && peer->pipeline_state.incremental_seeded && !force_full_replace)
            {
                auto const* replica = acknowledged;
                if (delivery == simnet::TransportDelivery::ReliableSequenced &&
                    !peer->snapshot_delivery.submitted.empty())
                {
                    replica = &peer->snapshot_delivery.submitted.back();
                }
                if (replica == nullptr)
                {
                    simnet::app::enter_snapshot_recovery(
                        peer->snapshot_delivery,
                        simnet::app::SnapshotRecoveryReason::MissingRetainedResult
                    );
                }
                else
                {
                    replica_snapshot = &replica->snapshot;
                    replica_sequence = replica->sequence;
                }
            }

            peer->recovery_upsert_ids.clear();
            if (cadence_emits && partial_selection_enabled &&
                !peer->snapshot_delivery.recovery_active &&
                delivery == simnet::TransportDelivery::UnreliableSequenced)
            {
                peer->recovery_upsert_ids.reserve(peer->snapshot_delivery.recovery_upserts.size());
                for (auto const& upsert : peer->snapshot_delivery.recovery_upserts)
                {
                    peer->recovery_upsert_ids.push_back(upsert.id);
                }
            }

            auto encoded = simnet::EncodeOutput{};
            auto pending_pipeline_state = peer->pipeline_state;
            auto const encode_start = simnet::steady_now_ns();
            {
                SIMNET_TRACE_SCOPE_CATEGORY(
                    "server.snapshot_encode",
                    simnet::LogCategory::Pipeline
                );
                // Current and retained baseline snapshots come only from successful extraction.
                encoded = simnet::encode_snapshot_unchecked(
                    pipeline,
                    pending_pipeline_state,
                    peer->pipeline_scratch,
                    {
                        .snapshot = source_snapshot,
                        .baseline_snapshot = explicit_level_of_detail_baseline != nullptr
                                                 ? &explicit_level_of_detail_baseline->snapshot
                                             : delta_baseline != nullptr ? &delta_baseline->snapshot
                                                                         : nullptr,
                        .baseline_sequence = explicit_level_of_detail_baseline != nullptr
                                                 ? explicit_level_of_detail_baseline->sequence
                                             : delta_baseline != nullptr ? delta_baseline->sequence
                                                                         : 0U,
                        .replica_snapshot = replica_snapshot,
                        .replica_sequence = replica_sequence,
                        .recovery_upsert_ids = peer->recovery_upsert_ids,
                        .force_full_replace = peer->snapshot_delivery.recovery_active,
                        .interest_source =
                            interest_source.has_value() ? &*interest_source : nullptr,
                        .candidate_indices = peer->area_of_interest_candidates,
                    }
                );
            }
            measurement.encode_elapsed_time = simnet::steady_now_ns() - encode_start;
            measurement.source_entity_count = extraction.entity_count;
            measurement.recovery_reason =
                simnet::app::snapshot_recovery_reason_name(peer->snapshot_delivery.recovery_reason);
            measurement.encoded_update_bytes =
                static_cast<std::uint32_t>(encoded.update.bytes.size());
            auto const observe_encoded_measurement = [&]
            {
                simnet::app::flatten_server_encode_report(measurement, encoded.report);
                if (force_full_replace)
                {
                    measurement.recovery_forced_upsert_count = encoded.report.upsert_count;
                    measurement.recovery_forced_delete_count = encoded.report.delete_count;
                }
                observe_server_measurement(measurements, csv, measurement);
            };
            if (encoded.kind == simnet::EncodeResultKind::Skipped)
            {
                ++peer->cadence_skip_count;
                measurement.outcome = simnet::ServerReplicationOutcome::Skipped;
                measurement.outcome_detail = "cadence_skip";
                measurement.total_replication_elapsed_time = finish_total_time();
                observe_encoded_measurement();
                return true;
            }
            if (collect_representation_quality)
            {
                auto const quality_start = simnet::steady_now_ns();
                encoded.report.representation = simnet::measure_representation_quality(
                    pipeline,
                    *source_snapshot,
                    peer->pipeline_scratch.logical_update
                );
                excluded_evidence_time += simnet::steady_now_ns() - quality_start;
            }

            auto const retention_plan = simnet::app::plan_snapshot_retention(
                peer->snapshot_delivery,
                encoded.resulting_snapshot
            );
            if (!retention_plan.valid ||
                (delivery == simnet::TransportDelivery::UnreliableSequenced &&
                 partial_selection_enabled &&
                 encoded.report.snapshot_kind == simnet::SnapshotKind::Patch &&
                 peer->snapshot_delivery.recovery_upserts.size() +
                         peer->pipeline_scratch.logical_update.upserts.size() >
                     simnet::app::maximum_recovery_upserts))
            {
                if (!retention_plan.valid &&
                    simnet::app::snapshot_capacity_bytes(encoded.resulting_snapshot) >
                        simnet::app::maximum_retained_capacity_bytes)
                {
                    simnet::log(
                        simnet::LogCategory::Snapshot,
                        simnet::LogLevel::Error,
                        "exact snapshot result exceeds the retained capacity limit"
                    );
                    return false;
                }
                simnet::app::discard_acknowledged_replica(
                    peer->snapshot_delivery,
                    simnet::app::SnapshotRecoveryReason::RetentionPressure
                );
                measurement.outcome = simnet::ServerReplicationOutcome::Abandoned;
                measurement.outcome_detail = "retention_pressure";
                measurement.recovery_reason = "retention_pressure";
                measurement.total_replication_elapsed_time = finish_total_time();
                observe_encoded_measurement();
                return true;
            }
            if (delivery == simnet::TransportDelivery::UnreliableSequenced &&
                partial_selection_enabled &&
                encoded.report.snapshot_kind == simnet::SnapshotKind::Patch)
            {
                peer->snapshot_delivery.recovery_upserts.reserve(
                    peer->snapshot_delivery.recovery_upserts.size() +
                    peer->pipeline_scratch.logical_update.upserts.size()
                );
            }

            auto compression_report = ServerCompressionReport{
                .mode = compression.mode,
                .group_id = encoded.update.sequence,
                .representation_bytes = measurement.encoded_update_bytes,
            };
            if (compression.mode == simnet::app::CompressionMode::None)
            {
                compression_report.compression_input_bytes = measurement.encoded_update_bytes;
                compression_report.compression_payload_bytes = measurement.encoded_update_bytes;
                compression_report.compression_output_bytes = measurement.encoded_update_bytes;
            }
            peer->latest_attempted_submissions = 0U;
            peer->latest_accepted_submissions = 0U;
            peer->latest_submission_error.clear();
            auto group_bytes = std::move(encoded.update.bytes);
            if (compression.mode == simnet::app::CompressionMode::WholeUpdate)
            {
                auto const limits = simnet::CompressionLimits{
                    .max_uncompressed_bytes = packetization.max_group_bytes,
                    .max_output_bytes = packetization.max_group_bytes,
                };
                auto const compressed = simnet::compress_bytes(
                    peer->compressor,
                    group_bytes,
                    compression.level,
                    limits,
                    simnet::CompressionEnvelopePolicy::Always,
                    peer->compression_scratch
                );
                compression_report.compression_input_bytes = compressed.input_bytes;
                compression_report.compression_payload_bytes = compressed.encoded_payload_bytes;
                compression_report.compression_envelope_bytes = compressed.envelope_bytes;
                compression_report.compression_output_bytes = compressed.output_bytes;
                compression_report.compression_elapsed_time = compressed.compression_elapsed_time;
                compression_report.whole_encoding = compressed.encoding;
                if (!compressed.valid)
                {
                    compression_report.error = compressed.error;
                    peer->latest_compression = compression_report;
                    peer->latest_submission_outcome = PacketSubmissionOutcome::Abandoned;
                    peer->latest_submission_error = compressed.error;
                    flatten_transport_reports(
                        measurement,
                        compression_report,
                        {}
                    );
                    measurement.outcome = simnet::ServerReplicationOutcome::Abandoned;
                    measurement.outcome_detail = "whole_update_compression_failed";
                    measurement.total_replication_elapsed_time = finish_total_time();
                    observe_encoded_measurement();
                    simnet::log(
                        simnet::LogCategory::Pipeline,
                        simnet::LogLevel::Error,
                        "snapshot compression preparation failed: " + compressed.error
                    );
                    return false;
                }
                peer->whole_update_compression_elapsed_time += compressed.compression_elapsed_time;
                if (compressed.encoding == simnet::CompressionEncoding::Raw)
                {
                    ++peer->whole_update_raw_fallback_count;
                }
                group_bytes = std::move(peer->compression_scratch);
            }
            auto prepared = simnet::PreparedByteGroup{};
            auto const preparation = simnet::prepare_byte_group(
                packetization,
                encoded.update.sequence,
                std::move(group_bytes),
                prepared
            );
            peer->latest_packetization = preparation;
            if (preparation.outcome != simnet::GroupPreparationOutcome::Prepared)
            {
                compression_report.error = preparation.error;
                peer->latest_compression = compression_report;
                peer->latest_submission_outcome = PacketSubmissionOutcome::Abandoned;
                peer->latest_submission_error = preparation.error;
                flatten_transport_reports(
                    measurement,
                    compression_report,
                    preparation
                );
                measurement.outcome = simnet::ServerReplicationOutcome::Abandoned;
                measurement.outcome_detail = "packetization_failed";
                measurement.total_replication_elapsed_time = finish_total_time();
                observe_encoded_measurement();
                simnet::log(
                    simnet::LogCategory::Pipeline,
                    simnet::LogLevel::Error,
                    "snapshot packet preparation failed: " + preparation.error
                );
                return false;
            }
            compression_report.bytes_after_packetization = preparation.total_packet_bytes;
            if (!prepare_transport_group(
                    compression,
                    packetization,
                    packetization.max_payload_bytes,
                    prepared,
                    *peer,
                    compression_report
                ))
            {
                peer->latest_compression = compression_report;
                peer->latest_submission_outcome = PacketSubmissionOutcome::Abandoned;
                peer->latest_submission_error = compression_report.error;
                flatten_transport_reports(
                    measurement,
                    compression_report,
                    preparation
                );
                measurement.outcome = simnet::ServerReplicationOutcome::Abandoned;
                measurement.outcome_detail =
                    compression.mode == simnet::app::CompressionMode::PerPacket
                        ? "per_packet_compression_failed"
                        : "packet_serialization_failed";
                measurement.total_replication_elapsed_time = finish_total_time();
                observe_encoded_measurement();
                simnet::log(
                    simnet::LogCategory::Pipeline,
                    simnet::LogLevel::Error,
                    "snapshot transport payload preparation failed: " + compression_report.error
                );
                return false;
            }
            if (compression_report.compression_input_bytes != 0U)
            {
                compression_report.ratio =
                    static_cast<double>(compression_report.compression_output_bytes) /
                    static_cast<double>(compression_report.compression_input_bytes);
            }
            peer->latest_compression = compression_report;
            peer->latest_submission_outcome = PacketSubmissionOutcome::Prepared;
            flatten_transport_reports(measurement, compression_report, preparation);
            auto accepted_packet_bytes = std::uint32_t{};
            auto const send_start = simnet::steady_now_ns();
            {
                SIMNET_TRACE_SCOPE_CATEGORY("server.snapshot_send", simnet::LogCategory::Transport);
                for (auto index = std::uint16_t{}; index < prepared.chunk_count; ++index)
                {
                    auto const& payload = peer->prepared_transport_group.payloads[index];
                    auto const packet_bytes = simnet::ByteSpan{peer->prepared_transport_group.bytes}
                                                  .subspan(payload.offset, payload.size);
                    ++peer->latest_attempted_submissions;
                    auto const sent = transport.send({
                        .peer = peer->peer,
                        .lane = simnet::app::snapshot_lane,
                        .delivery = delivery,
                        .payload = packet_bytes,
                    });
                    if (!sent.ok)
                    {
                        peer->latest_submission_outcome = PacketSubmissionOutcome::Abandoned;
                        peer->latest_submission_error = sent.error.message;
                        measurement.transport_submission_elapsed_time =
                            simnet::steady_now_ns() - send_start;
                        measurement.transport_accepted_bytes = accepted_packet_bytes;
                        measurement.transport_accepted_packet_count =
                            peer->latest_accepted_submissions;
                        measurement.outcome = simnet::ServerReplicationOutcome::TransportSendFailed;
                        measurement.outcome_detail = "transport_submission_failed";
                        measurement.total_replication_elapsed_time = finish_total_time();
                        observe_encoded_measurement();
                        simnet::log(
                            simnet::LogCategory::Transport,
                            simnet::LogLevel::Error,
                            "snapshot chunk submission failed: " + sent.error.message
                        );
                        transport.disconnect(peer->peer, simnet::DisconnectCode::TransportError);
                        return false;
                    }
                    ++peer->latest_accepted_submissions;
                    accepted_packet_bytes += static_cast<std::uint32_t>(packet_bytes.size());
                }
            }
            measurement.transport_submission_elapsed_time = simnet::steady_now_ns() - send_start;
            measurement.transport_accepted_bytes = accepted_packet_bytes;
            measurement.transport_accepted_packet_count = peer->latest_accepted_submissions;

            peer->pipeline_state = pending_pipeline_state;
            auto const unchanged_ack = peer->snapshot_delivery.submissions_since_ack_progress != 0U;
            if (delivery == simnet::TransportDelivery::ReliableSequenced)
            {
                ++peer->reliable_group_count;
                peer->reliable_packet_count += prepared.chunk_count;
            }
            else
            {
                ++peer->unreliable_group_count;
                peer->unreliable_packet_count += prepared.chunk_count;
            }
            if (unchanged_ack && encoded.report.snapshot_kind == simnet::SnapshotKind::Patch)
            {
                measurement.repeated_without_ack_upsert_count = encoded.report.upsert_count;
                measurement.repeated_without_ack_delete_count = encoded.report.delete_count;
                peer->repeated_recovery_upserts += encoded.report.upsert_count;
                peer->repeated_recovery_deletes += encoded.report.delete_count;
            }
            if (delivery == simnet::TransportDelivery::UnreliableSequenced &&
                partial_selection_enabled &&
                encoded.report.snapshot_kind == simnet::SnapshotKind::Patch)
            {
                static_cast<void>(simnet::app::merge_recovery_upserts(
                    peer->snapshot_delivery,
                    peer->pipeline_scratch.logical_update
                ));
            }
            simnet::app::commit_submitted_snapshot(
                peer->snapshot_delivery,
                encoded.update.sequence,
                std::move(encoded.resulting_snapshot),
                encoded.report.snapshot_kind,
                simnet::steady_now_ns(),
                retention_plan
            );
            if (!peer->snapshot_delivery.recovery_active &&
                simnet::app::ack_progress_stalled(
                    peer->snapshot_delivery,
                    delivery_config.full_replace_after_unacknowledged_updates
                ))
            {
                simnet::app::enter_snapshot_recovery(
                    peer->snapshot_delivery,
                    simnet::app::SnapshotRecoveryReason::AckStalled
                );
            }
            measurement.submissions_since_ack_progress =
                peer->snapshot_delivery.submissions_since_ack_progress;
            peer->latest_area_of_interest = encoded.report.area_of_interest;
            peer->latest_level_of_detail = encoded.report.level_of_detail;
            peer->latest_representation = encoded.report.representation;
            peer->has_representation_report = true;
            peer->level_of_detail_full_replace_override_count +=
                encoded.report.level_of_detail.full_replace_override_count;
            peer->has_area_of_interest_report = true;
            peer->latest_upsert_count = encoded.report.upsert_count;
            peer->latest_delete_count = encoded.report.delete_count;
            peer->latest_submission_outcome = PacketSubmissionOutcome::Committed;
            ++peer->committed_emission_count;
            SIMNET_TRACE_PLOT(
                "server.retained_snapshot_count",
                static_cast<double>(peer->snapshot_delivery.submitted.size())
            );
            measurement.outcome = simnet::ServerReplicationOutcome::Sent;
            measurement.outcome_detail = "committed";
            measurement.total_replication_elapsed_time = finish_total_time();
            if (!peer->snapshot_delivery.submitted.empty())
            {
                auto const& committed = peer->snapshot_delivery.submitted.back();
                measurement.canonical_entity_count =
                    static_cast<std::uint32_t>(committed.snapshot.size());
                measurement.canonical_fingerprint =
                    simnet::app::snapshot_diagnostic_fingerprint(committed.snapshot);
            }
            observe_encoded_measurement();
            if (simnet::log_enabled(simnet::LogLevel::Debug) &&
                !peer->snapshot_delivery.submitted.empty())
            {
                auto const& committed = peer->snapshot_delivery.submitted.back();
                simnet::log(
                    simnet::LogCategory::Snapshot,
                    simnet::LogLevel::Debug,
                    "server canonical result peer_id=" + std::to_string(peer->peer) +
                        " role=" + std::string{peer_role_name(peer->role)} +
                        " player_id=" + std::to_string(peer->player_id) +
                        " sequence=" + std::to_string(committed.sequence) +
                        " baseline_sequence=" + std::to_string(encoded.report.baseline_sequence) +
                        " acknowledged_sequence=" +
                        std::to_string(peer->snapshot_delivery.latest_acknowledged_sequence) +
                        " recovery_active=" +
                        std::string{peer->snapshot_delivery.recovery_active ? "true" : "false"} +
                        " recovery_reason=" +
                        std::string{simnet::app::snapshot_recovery_reason_name(
                            peer->snapshot_delivery.recovery_reason
                        )} +
                        " entities=" + std::to_string(committed.snapshot.size()) +
                        " fingerprint=" + std::to_string(measurement.canonical_fingerprint)
                );
            }
            if (simnet::log_enabled(simnet::LogLevel::Debug) &&
                simnet::has_all_flags(
                    pipeline.techniques,
                    simnet::PipelineTechniqueFlags::DeltaFieldMask
                ) &&
                encoded.report.snapshot_kind == simnet::SnapshotKind::Patch)
            {
                auto const& delta = encoded.report.delta;
                simnet::log(
                    simnet::LogCategory::Pipeline,
                    simnet::LogLevel::Debug,
                    "server Delta field mask peer_id=" + std::to_string(peer->peer) +
                        " tick=" + std::to_string(encoded.report.tick) +
                        " sequence=" + std::to_string(encoded.report.sequence) +
                        " candidates=" + std::to_string(delta.candidate_count) +
                        " unchanged=" + std::to_string(delta.unchanged_count) +
                        " masked_existing=" + std::to_string(delta.masked_existing_upsert_count) +
                        " spawns=" + std::to_string(delta.spawned_count) +
                        " classification_fields=" +
                        std::to_string(delta.classification_inclusion_count) +
                        " position_fields=" + std::to_string(delta.position_inclusion_count) +
                        " heading_fields=" + std::to_string(delta.heading_inclusion_count) +
                        " hue_fields=" + std::to_string(delta.hue_inclusion_count) +
                        " complete_record_equivalent_bytes=" +
                        std::to_string(delta.complete_record_equivalent_bytes) +
                        " actual_upsert_representation_bytes=" +
                        std::to_string(delta.actual_upsert_representation_bytes)
                );
            }
            if (preparation.chunk_count > 1U && !peer->logged_multi_packet_commit)
            {
                peer->logged_multi_packet_commit = true;
                simnet::log(
                    simnet::LogCategory::Transport,
                    simnet::LogLevel::Info,
                    "server packet group committed peer_id=" + std::to_string(peer->peer) +
                        " role=" + std::string{peer_role_name(peer->role)} +
                        " group_id=" + std::to_string(preparation.group_id) +
                        " encoded_bytes=" + std::to_string(measurement.encoded_update_bytes) +
                        " compression_output_bytes=" +
                        std::to_string(compression_report.compression_output_bytes) +
                        " chunks=" + std::to_string(preparation.chunk_count) +
                        " header_bytes=" + std::to_string(preparation.total_header_bytes) +
                        " application_packet_bytes=" +
                        std::to_string(preparation.total_packet_bytes) + " attempted_submissions=" +
                        std::to_string(peer->latest_attempted_submissions) +
                        " accepted_submissions=" +
                        std::to_string(peer->latest_accepted_submissions) + " aoi_retained=" +
                        std::to_string(encoded.report.area_of_interest.retained_count) +
                        " compression_mode=" +
                        std::string{simnet::app::compression_mode_name(compression.mode)} +
                        " compression_payload_bytes=" +
                        std::to_string(compression_report.compression_payload_bytes) +
                        " compression_envelope_bytes=" +
                        std::to_string(compression_report.compression_envelope_bytes) +
                        " final_transport_bytes=" +
                        std::to_string(compression_report.final_transport_bytes) +
                        " zstd_packets=" + std::to_string(compression_report.zstd_packet_count) +
                        " raw_packets=" + std::to_string(compression_report.raw_packet_count)
                );
            }
            return true;
        };

        simnet::app::detail::process_sorted_peer_states(
            peers,
            [](PeerRuntimeState const& peer)
            {
                return peer.role.has_value();
            },
            replicate_peer,
            [&](PeerRuntimeState const& failed_peer)
            {
                transport.disconnect(failed_peer.peer, simnet::DisconnectCode::TransportError);
                remove_failed_peer_state(world, failed_peer, snapshot_state, delivery);
            }
        );
        return true;
    }
}
