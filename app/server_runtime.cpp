#include "server_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <flecs.h>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <simnet/telemetry_trace.hpp>

#include "server_peer_iteration.hpp"

import simnet.config;
import simnet.app_compression_corpus;
import simnet.app_compression_dictionary;
import simnet.app_evidence;
import simnet.app_common;
import simnet.app_protocol;
import simnet.app_snapshot_delivery;
import simnet.compression;
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
#if defined(SIMNET_ENABLE_RENDER)
import simnet.app_visual_setup;
import simnet.render;
#endif

namespace
{
#if defined(SIMNET_ENABLE_RENDER)
    struct SpatialRenderCandidate
    {
        simnet::CellKey key{};
        simnet::Aabb3f bounds{};
        std::uint32_t entity_count{};
        float distance_squared{};
    };

    struct SpatialRenderStorage
    {
        simnet::SpatialGrid grid{};
        simnet::SpatialGridScratch scratch{};
        std::vector<simnet::SpatialCellView> displayed_cells{};
        std::vector<SpatialRenderCandidate> candidates{};
    };

    struct SelectedDebugRenderStorage
    {
        std::vector<simnet::DebugSphereView> spheres{};
        std::vector<simnet::DebugVectorView> vectors{};
        std::vector<simnet::DebugBoxView> boxes{};
        std::vector<simnet::DebugConeView> cones{};
        std::vector<std::string> labels{};
    };
#endif

    struct ServerOptions
    {
        std::optional<std::filesystem::path> config_path{};
        std::optional<std::filesystem::path> shared_config_path{};
        std::optional<std::filesystem::path> compression_corpus_directory{};
        std::optional<std::string> run_id{};
        std::uint64_t max_frames{};
        simnet::Tick max_ticks{};
        simnet::Nanoseconds max_runtime{};
        simnet::Nanoseconds max_frame_time{std::chrono::milliseconds(250)};
        std::uint16_t max_steps_per_frame{5};
    };

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
        std::string_view dictionary_name{"none"};
        std::uint32_t dictionary_id{};
        simnet::PacketGroupId group_id{};
        std::uint32_t representation_bytes{};
        std::uint32_t compression_input_bytes{};
        std::uint32_t compression_payload_bytes{};
        std::uint32_t compression_envelope_bytes{};
        std::uint32_t compression_output_bytes{};
        std::uint32_t bytes_before_packetization{};
        std::uint32_t bytes_after_packetization{};
        std::uint32_t final_transport_bytes{};
        std::uint32_t zstd_packet_count{};
        std::uint32_t raw_packet_count{};
        double ratio{1.0};
        simnet::CompressionEncoding whole_encoding{simnet::CompressionEncoding::Raw};
        simnet::Nanoseconds compression_cpu_time{};
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

    [[nodiscard]] std::string_view
    server_replication_outcome_name(simnet::ServerReplicationOutcome outcome) noexcept
    {
        using enum simnet::ServerReplicationOutcome;
        switch (outcome)
        {
            case SnapshotExtractionFailed:
                return "snapshot_extraction_failed";
            case Skipped:
                return "skipped";
            case Abandoned:
                return "abandoned";
            case TransportSendFailed:
                return "transport_send_failed";
            case Sent:
                return "sent";
        }
        return "unknown";
    }

    void
    log_server_replication_measurements(simnet::ServerReplicationMeasurements const& measurements)
    {
        if (!measurements.latest_attempt.has_value())
        {
            simnet::log(
                simnet::LogCategory::Telemetry,
                simnet::LogLevel::Info,
                "server replication measurements attempts=0 sent=0"
            );
            return;
        }

        auto const& value = *measurements.latest_attempt;
        simnet::log(
            simnet::LogCategory::Telemetry,
            simnet::LogLevel::Info,
            "server replication measurements attempts=" +
                std::to_string(measurements.attempt_count) +
                " sent=" + std::to_string(measurements.sent_count) +
                " latest_outcome=" + std::string{server_replication_outcome_name(value.outcome)} +
                " peer_id=" + std::to_string(value.peer_id) + " role=" +
                std::string{value.accepted_gameplay_role} + " tick=" + std::to_string(value.tick) +
                " sequence=" + std::to_string(value.sequence) +
                " baseline_sequence=" + std::to_string(value.baseline_sequence) +
                " snapshot_kind=" + std::to_string(static_cast<unsigned>(value.snapshot_kind)) +
                " source_entities=" + std::to_string(value.source_entity_count) +
                " selected_entities=" + std::to_string(value.selected_entity_count) +
                " upserts=" + std::to_string(value.upsert_count) +
                " deletes=" + std::to_string(value.delete_count) +
                " encoded_update_bytes=" + std::to_string(value.encoded_update_bytes) +
                " application_payload_bytes=" + std::to_string(value.application_payload_bytes) +
                " transport_payload_bytes=" + std::to_string(value.transport_payload_bytes) +
                " snapshot_extraction_elapsed_ns=" +
                std::to_string(value.snapshot_extraction_elapsed_time.count()) +
                " baseline_resolution_elapsed_ns=" +
                std::to_string(value.baseline_resolution_elapsed_time.count()) +
                " encode_elapsed_ns=" + std::to_string(value.encode_elapsed_time.count()) +
                " transport_send_elapsed_ns=" +
                std::to_string(value.transport_send_elapsed_time.count()) +
                " snapshot_retention_elapsed_ns=" +
                std::to_string(value.snapshot_retention_elapsed_time.count()) +
                " total_replication_elapsed_ns=" +
                std::to_string(value.total_replication_elapsed_time.count())
        );
    }

#if defined(SIMNET_ENABLE_RENDER)
    struct PresentationSnapshotState
    {
        simnet::WorldSnapshot previous{};
        simnet::WorldSnapshot current{};
        simnet::WorldSnapshot interpolated{};
        bool has_previous{};
        bool has_current{};
    };
#endif

    enum class PacketSubmissionOutcome : std::uint8_t
    {
        None,
        Prepared,
        Committed,
        Abandoned
    };

    [[nodiscard]] constexpr std::string_view
    packet_submission_outcome_name(PacketSubmissionOutcome outcome) noexcept
    {
        switch (outcome)
        {
            case PacketSubmissionOutcome::None:
                return "None";
            case PacketSubmissionOutcome::Prepared:
                return "Prepared";
            case PacketSubmissionOutcome::Committed:
                return "Committed";
            case PacketSubmissionOutcome::Abandoned:
                return "Abandoned";
        }
        return "Unknown";
    }

    [[nodiscard]] constexpr std::string_view
    compression_encoding_name(simnet::CompressionEncoding encoding) noexcept
    {
        switch (encoding)
        {
            case simnet::CompressionEncoding::Raw:
                return "Raw";
            case simnet::CompressionEncoding::Zstd:
                return "Zstd";
            case simnet::CompressionEncoding::ZstdDictionary:
                return "ZstdDictionary";
        }
        return "Unknown";
    }

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
        simnet::app::SnapshotAck latest_ack{};
        simnet::app::SnapshotDeliveryState snapshot_delivery{};
        std::vector<simnet::EntityNetId> recovery_upsert_ids{};
        simnet::ZstdCompressor compressor{};
        std::vector<simnet::Byte> compression_scratch{};
        std::vector<simnet::Byte> packet_serialization_scratch{};
        PreparedTransportGroup prepared_transport_group{};
        ServerCompressionReport latest_compression{};
        std::uint64_t whole_update_raw_fallback_count{};
        simnet::Nanoseconds whole_update_compression_cpu_time{};
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
                    " dictionary=" + std::string{peer.latest_compression.dictionary_name} +
                    " dictionary_id=" + std::to_string(peer.latest_compression.dictionary_id) +
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
                    " latest_compression_cpu_ns=" +
                    std::to_string(
                        std::max(
                            peer.latest_compression.compression_cpu_time,
                            simnet::Nanoseconds{}
                        )
                            .count()
                    ) +
                    " raw_fallbacks=" + std::to_string(peer.whole_update_raw_fallback_count) +
                    " compression_cpu_ns=" +
                    std::to_string(
                        std::max(peer.whole_update_compression_cpu_time, simnet::Nanoseconds{})
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

    [[nodiscard]] bool prepare_per_packet_transport_group(
        simnet::app::CompressionSettings compression,
        simnet::PacketizationSettings const& packetization,
        std::uint32_t transport_payload_limit,
        simnet::PreparedByteGroup const& prepared,
        PeerRuntimeState& peer,
        ServerCompressionReport& report
    )
    {
        auto const maximum_storage =
            static_cast<std::uint64_t>(prepared.chunk_count) * transport_payload_limit;
        if (maximum_storage > std::numeric_limits<std::uint32_t>::max() ||
            maximum_storage > peer.prepared_transport_group.bytes.max_size())
        {
            report.error = "compressed transport group storage bound is invalid";
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
            auto const next_size =
                static_cast<std::uint64_t>(transport_group.bytes.size()) + compressed.output_bytes;
            if (next_size > maximum_storage ||
                next_size > std::numeric_limits<std::uint32_t>::max())
            {
                report.error = "compressed transport group exceeds its validated storage bound";
                return false;
            }

            auto const offset = static_cast<std::uint32_t>(transport_group.bytes.size());
            transport_group.bytes.insert(
                transport_group.bytes.end(),
                peer.compression_scratch.begin(),
                peer.compression_scratch.end()
            );
            transport_group.payloads.push_back({
                .offset = offset,
                .size = compressed.output_bytes,
                .encoding = compressed.encoding,
            });
            report.compression_input_bytes += compressed.input_bytes;
            report.compression_payload_bytes += compressed.encoded_payload_bytes;
            report.compression_envelope_bytes += compressed.envelope_bytes;
            report.compression_output_bytes += compressed.output_bytes;
            report.compression_cpu_time += compressed.compression_cpu_time;
            if (compressed.encoding == simnet::CompressionEncoding::Zstd)
            {
                ++report.zstd_packet_count;
            }
            else
            {
                ++report.raw_packet_count;
            }
        }
        report.final_transport_bytes = static_cast<std::uint32_t>(transport_group.bytes.size());
        report.valid = true;
        return true;
    }

    [[nodiscard]] std::optional<simnet::InterestSource>
    resolve_interest_source(PeerRuntimeState const& peer, simnet::WorldSnapshot const& snapshot);

    [[nodiscard]] ServerOptions parse_options(int argc, char** argv)
    {
        auto options = ServerOptions{};
        for (auto index = 1; index < argc; ++index)
        {
            auto const option = std::string_view{argv[index]};
            if (option == "--config")
            {
                options.config_path = std::filesystem::path{
                    simnet::app::next_option_value(index, argc, argv, option)
                };
            }
            else if (option == "--shared-config")
            {
                options.shared_config_path = std::filesystem::path{
                    simnet::app::next_option_value(index, argc, argv, option)
                };
            }
            else if (option == "--run-id")
            {
                options.run_id = simnet::app::next_option_value(index, argc, argv, option);
            }
            else if (option == "--compression-corpus-dir")
            {
                options.compression_corpus_directory = std::filesystem::path{
                    simnet::app::next_option_value(index, argc, argv, option)
                };
            }
            else if (option == "--max-frames")
            {
                options.max_frames = simnet::app::parse_unsigned<std::uint64_t>(
                    simnet::app::next_option_value(index, argc, argv, option),
                    option
                );
            }
            else if (option == "--max-ticks")
            {
                options.max_ticks = simnet::app::parse_unsigned<simnet::Tick>(
                    simnet::app::next_option_value(index, argc, argv, option),
                    option
                );
            }
            else if (option == "--max-runtime-ms")
            {
                options.max_runtime = simnet::app::milliseconds_option(index, argc, argv, option);
            }
            else if (option == "--max-frame-delta-ms")
            {
                options.max_frame_time =
                    simnet::app::milliseconds_option(index, argc, argv, option);
            }
            else if (option == "--max-steps-per-frame")
            {
                options.max_steps_per_frame = simnet::app::parse_unsigned<std::uint16_t>(
                    simnet::app::next_option_value(index, argc, argv, option),
                    option
                );
            }
            else
            {
                throw std::runtime_error("unknown server option: " + std::string{option});
            }
        }
        return options;
    }

    [[nodiscard]] simnet::BoidSimulationSettings boid_settings(simnet::SharedConfig const& config)
    {
        return {
            .seed = config.run.seed,
            .world_half = config.simulation.world_half,
            .cell_size = config.spatial.cell_size,
            .max_neighbors = config.spatial.max_neighbors,
            .enable_separation = config.boids.enable_separation,
            .enable_alignment = config.boids.enable_alignment,
            .enable_cohesion = config.boids.enable_cohesion,
            .enable_containment = config.boids.enable_containment,
            .enable_wander = config.boids.enable_wander,
            .enable_hue_assimilation = config.boids.enable_hue_assimilation,
            .enable_hue_drift = config.boids.enable_hue_drift,
            .min_speed = config.boids.min_speed,
            .cruise_speed = config.boids.cruise_speed,
            .max_speed = config.boids.max_speed,
            .max_acceleration = config.boids.max_acceleration,
            .separation_radius = config.boids.separation_radius,
            .alignment_radius = config.boids.alignment_radius,
            .cohesion_radius = config.boids.cohesion_radius,
            .field_of_view_degrees = config.boids.field_of_view_degrees,
            .containment_prediction_seconds = config.boids.containment_prediction_seconds,
            .containment_margin = config.boids.containment_margin,
            .separation_acceleration = config.boids.separation_acceleration,
            .containment_acceleration = config.boids.containment_acceleration,
            .alignment_acceleration = config.boids.alignment_acceleration,
            .cohesion_acceleration = config.boids.cohesion_acceleration,
            .wander_acceleration = config.boids.wander_acceleration,
            .wander_frequency_hz = config.boids.wander_frequency_hz,
            .hue_assimilation_rate = config.boids.hue_assimilation_rate,
            .hue_drift_rate = config.boids.hue_drift_rate,
            .player_lure =
                {
                    .enabled = config.boids.player_lure.enabled,
                    .radius = config.boids.player_lure.radius,
                    .max_acceleration = config.boids.player_lure.max_acceleration,
                },
            .player_predator = {
                .enabled = config.boids.player_predator.enabled,
                .radius = config.boids.player_predator.radius,
                .max_acceleration = config.boids.player_predator.max_acceleration,
            },
        };
    }

#if defined(SIMNET_ENABLE_SYNTHETIC)
    [[nodiscard]] simnet::SyntheticSnapshotSettings
    synthetic_snapshot_settings(simnet::SharedConfig const& config)
    {
        return {
            .seed = config.run.seed,
            .entity_count = config.simulation.initial_boid_count,
            .bounds = simnet::make_centered_bounds(config.simulation.world_half),
            .pattern = config.synthetic->pattern == "grid"
                           ? simnet::SyntheticPattern::Grid
                           : simnet::SyntheticPattern::RandomUniform,
        };
    }

    [[nodiscard]] simnet::SyntheticChangeSettings
    synthetic_change_settings(simnet::SharedConfig const& config)
    {
        auto mode = simnet::SyntheticFieldChangeMode::All;
        if (config.synthetic->field_change_mode == "transform")
        {
            mode = simnet::SyntheticFieldChangeMode::Transform;
        }
        else if (config.synthetic->field_change_mode == "position_only")
        {
            mode = simnet::SyntheticFieldChangeMode::PositionOnly;
        }
        else if (config.synthetic->field_change_mode == "heading_only")
        {
            mode = simnet::SyntheticFieldChangeMode::HeadingOnly;
        }
        return {
            .entity_change_fraction = config.synthetic->entity_change_fraction,
            .field_change_mode = mode,
        };
    }
#endif

    [[nodiscard]] simnet::PlayerMovementSettings player_settings(simnet::SharedConfig const& config)
    {
        return {
            .world_half = config.simulation.world_half,
            .cruise_speed = config.player.cruise_speed,
            .boost_speed = config.player.boost_speed,
            .slow_speed = config.player.slow_speed,
            .speed_change_rate = config.player.speed_change_rate,
            .yaw_acceleration_degrees = config.player.yaw_acceleration_degrees,
            .pitch_acceleration_degrees = config.player.pitch_acceleration_degrees,
            .yaw_damping = config.player.yaw_damping,
            .pitch_damping = config.player.pitch_damping,
            .max_yaw_rate_degrees = config.player.max_yaw_rate_degrees,
            .max_pitch_rate_degrees = config.player.max_pitch_rate_degrees,
            .pitch_limit_degrees = config.player.pitch_limit_degrees,
        };
    }

#if defined(SIMNET_ENABLE_RENDER)
    [[nodiscard]] simnet::ViewerConfig viewer_config(simnet::VisualizationConfig const& config)
    {
        return {
            .window_width = config.window_width,
            .window_height = config.window_height,
            .panel_width = config.panel_width,
            .target_frame_rate = config.target_fps,
            .entity_scale = config.entity_scale,
            .picking_radius = config.picking_radius,
            .stationary_observer_interest_radius = config.stationary_observer_interest_radius,
            .stationary_observer_vertical_fov_degrees =
                config.stationary_observer_vertical_fov_degrees,
            .max_visible_spatial_cells = config.max_visible_spatial_cells,
            .entity_mesh_path = config.entity_mesh_path,
            .title = "SimNet Server",
        };
    }

    [[nodiscard]] simnet::RenderFrame render_frame(
        simnet::WorldSnapshot const& snapshot,
        simnet::SharedConfig const& config,
        simnet::Nanoseconds frame_delta,
        bool paused,
        simnet::RenderInterpolationInfo interpolation,
        PeerRuntimeStates const& peers,
        std::uint32_t peer_capacity,
        SpatialRenderStorage const& spatial,
        SelectedDebugRenderStorage& debug_storage,
        std::optional<simnet::SelectedBoidDebug> const& selected_debug,
        simnet::RunSetupView setup
    )
    {
        auto const focused = std::ranges::find_if(
            peers,
            [](PeerRuntimeState const& candidate)
            {
                return candidate.role.has_value();
            }
        );
        auto const* peer = focused == peers.end() ? nullptr : &*focused;
        auto connection = simnet::RenderConnectionInfo{
            .state = peers.empty() ? "No clients connected" : "Clients connected",
            .connected_peer_count = static_cast<std::uint32_t>(peers.size()),
            .peer_capacity = peer_capacity,
        };
        auto replication = std::optional<simnet::RenderReplicationInfo>{};
        if (peer != nullptr)
        {
            connection.peer = peer->peer;
            auto details = simnet::RenderReplicationInfo{};
            if (peer->snapshot_delivery.latest_submitted_sequence != 0)
            {
                details.latest_emitted_sequence = peer->snapshot_delivery.latest_submitted_sequence;
            }
            if (peer->latest_ack.newest_received_snapshot != 0)
            {
                details.latest_received_sequence = peer->latest_ack.newest_received_snapshot;
            }
            if (peer->latest_ack.newest_applied_snapshot != 0)
            {
                details.latest_applied_sequence = peer->latest_ack.newest_applied_snapshot;
            }
            if (peer->snapshot_delivery.latest_acknowledged_sequence != 0U)
            {
                details.acknowledged_baseline_sequence =
                    peer->snapshot_delivery.latest_acknowledged_sequence;
            }
            details.configured_delivery = config.snapshot_delivery.mode;
            details.effective_delivery = config.snapshot_delivery.mode;
            details.ack_lag_updates = peer->snapshot_delivery.latest_submitted_sequence -
                                      peer->snapshot_delivery.latest_acknowledged_sequence;
            auto const now = simnet::steady_now_ns();
            auto const ack_lag_start =
                peer->snapshot_delivery.latest_ack_progress_time != simnet::Nanoseconds{}
                    ? peer->snapshot_delivery.latest_ack_progress_time
                : peer->snapshot_delivery.submitted.empty()
                    ? now
                    : peer->snapshot_delivery.submitted.front().submitted_at;
            details.ack_lag_ns =
                peer->snapshot_delivery.latest_submitted_sequence == 0U
                    ? 0U
                    : static_cast<std::uint64_t>(
                          std::max(now - ack_lag_start, simnet::Nanoseconds{}).count()
                      );
            details.retained_snapshot_capacity_bytes =
                peer->snapshot_delivery.retained_capacity_bytes;
            details.snapshot_recovery_reason =
                simnet::app::snapshot_recovery_reason_name(peer->snapshot_delivery.recovery_reason);
            details.forced_full_replace_count = peer->snapshot_delivery.forced_full_replace_count;
            details.recovery_request_count = peer->snapshot_delivery.recovery_request_count;
            details.baseline_eviction_count = peer->snapshot_delivery.baseline_eviction_count;
            details.reliable_group_count = peer->reliable_group_count;
            details.unreliable_group_count = peer->unreliable_group_count;
            details.reliable_packet_count = peer->reliable_packet_count;
            details.unreliable_packet_count = peer->unreliable_packet_count;
            details.repeated_recovery_upserts = peer->repeated_recovery_upserts;
            details.repeated_recovery_deletes = peer->repeated_recovery_deletes;
            details.area_of_interest_mode = config.pipeline.area_of_interest.mode;
            details.level_of_detail_mode = config.pipeline.level_of_detail.mode;
            details.send_interval_ticks = config.pipeline.send_interval_ticks;
            details.committed_emission_count = peer->committed_emission_count;
            details.cadence_skip_count = peer->cadence_skip_count;
            details.packetization_enabled = config.packetization.enabled;
            if (config.pipeline.area_of_interest.mode == "none")
            {
                details.interest_source_status = "not required";
            }
            else if (peer->role == simnet::app::ClientRole::Player)
            {
                details.interest_source_status = "authoritative Player";
            }
            else if (peer->stationary_observer_interest.initialized)
            {
                details.interest_source_status = "accepted stationary observer";
            }
            else
            {
                details.interest_source_status = "waiting for stationary observer";
            }
            if (peer->has_area_of_interest_report)
            {
                details.source_entity_count = peer->latest_area_of_interest.source_entity_count;
                details.candidate_entity_count = peer->latest_area_of_interest.candidate_count;
                details.retained_entity_count = peer->latest_area_of_interest.retained_count;
                details.culled_entity_count = peer->latest_area_of_interest.culled_count;
            }
            if (config.pipeline.level_of_detail.mode == "distance_bands")
            {
                auto const& lod = peer->latest_level_of_detail;
                details.lod_near_population = lod.population.near;
                details.lod_medium_population = lod.population.medium;
                details.lod_far_population = lod.population.far;
                details.lod_near_eligible = lod.eligible.near;
                details.lod_medium_eligible = lod.eligible.medium;
                details.lod_far_eligible = lod.eligible.far;
                details.lod_near_serviced = lod.serviced.near;
                details.lod_medium_serviced = lod.serviced.medium;
                details.lod_far_serviced = lod.serviced.far;
                details.lod_near_represented = lod.represented.near;
                details.lod_medium_represented = lod.represented.medium;
                details.lod_far_represented = lod.represented.far;
                details.lod_near_deferred = lod.deferred.near;
                details.lod_medium_deferred = lod.deferred.medium;
                details.lod_far_deferred = lod.deferred.far;
                details.lod_pending_due = lod.pending_due_count;
                details.lod_transitions = lod.transition_count;
                details.lod_forced_immediate = lod.forced_immediate_count;
                details.lod_recovery_forced = lod.recovery_forced_count;
                details.lod_deletions_bypassing = lod.deletions_bypassing_count;
                details.lod_full_replace_overrides =
                    peer->level_of_detail_full_replace_override_count;
            }
            if (peer->snapshot_delivery.latest_submitted_sequence != 0U)
            {
                details.transmitted_upsert_count = peer->latest_upsert_count;
                details.transmitted_delete_count = peer->latest_delete_count;
            }
            if (peer->has_representation_report)
            {
                auto const& representation = peer->latest_representation;
                details.representation_layout = entity_record_layout_name(representation.layout);
                details.entity_record_bytes = representation.record_bytes;
                details.representation_quality_samples = representation.quality_sample_count;
                if (representation.quality_sample_count != 0U)
                {
                    auto const sample_count =
                        static_cast<double>(representation.quality_sample_count);
                    details.mean_position_error = representation.position_error_sum / sample_count;
                    details.maximum_position_error = representation.position_error_maximum;
                    details.mean_heading_error_degrees =
                        representation.heading_angular_error_degrees_sum / sample_count;
                    details.maximum_heading_error_degrees =
                        representation.heading_angular_error_degrees_maximum;
                }
            }
            if (peer->latest_packetization.group_id != 0U)
            {
                details.packet_group_id = peer->latest_packetization.group_id;
                details.encoded_group_bytes = peer->latest_packetization.group_bytes;
                details.packet_chunk_count = peer->latest_packetization.chunk_count;
                details.packet_header_bytes = peer->latest_packetization.total_header_bytes;
                details.application_packet_bytes = peer->latest_packetization.total_packet_bytes;
                details.attempted_packet_submissions = peer->latest_attempted_submissions;
                details.accepted_packet_submissions = peer->latest_accepted_submissions;
                details.packet_submission_outcome =
                    packet_submission_outcome_name(peer->latest_submission_outcome);
                if (!peer->latest_submission_error.empty())
                {
                    details.packet_submission_failure = peer->latest_submission_error;
                }
            }
            auto const& compression = peer->latest_compression;
            details.compression_mode = simnet::app::compression_mode_name(compression.mode);
            if (compression.dictionary_id != 0U)
            {
                details.compression_dictionary = compression.dictionary_name;
                details.compression_dictionary_id = compression.dictionary_id;
            }
            if (compression.group_id != 0U)
            {
                details.representation_bytes = compression.representation_bytes;
                details.compression_input_bytes = compression.compression_input_bytes;
                details.compression_payload_bytes = compression.compression_payload_bytes;
                details.compression_envelope_bytes = compression.compression_envelope_bytes;
                details.compression_output_bytes = compression.compression_output_bytes;
                details.bytes_before_packetization = compression.bytes_before_packetization;
                details.bytes_after_packetization = compression.bytes_after_packetization;
                details.final_transport_bytes = compression.final_transport_bytes;
                details.compressed_packet_count = compression.zstd_packet_count;
                details.raw_packet_count = compression.raw_packet_count;
                details.compression_ratio = compression.ratio;
                details.compression_cpu_ns = static_cast<std::uint64_t>(
                    std::max(compression.compression_cpu_time, simnet::Nanoseconds{}).count()
                );
                if (compression.mode == simnet::app::CompressionMode::WholeUpdate)
                {
                    details.compression_outcome =
                        compression_encoding_name(compression.whole_encoding);
                }
                else if (compression.mode == simnet::app::CompressionMode::PerPacket)
                {
                    details.compression_outcome = compression.zstd_packet_count == 0U
                                                      ? "Raw fallback"
                                                  : compression.raw_packet_count == 0U ? "Zstd"
                                                                                       : "Mixed";
                }
                else
                {
                    details.compression_outcome = "Disabled";
                }
            }
            details.retained_snapshot_count = static_cast<std::uint32_t>(
                peer->snapshot_delivery.submitted.size() +
                (peer->snapshot_delivery.acknowledged.has_value() ? 1U : 0U)
            );
            if (!peer->snapshot_delivery.submitted.empty())
            {
                details.oldest_retained_sequence =
                    peer->snapshot_delivery.submitted.front().sequence;
                details.newest_retained_sequence =
                    peer->snapshot_delivery.submitted.back().sequence;
                details.latest_snapshot_tick =
                    peer->snapshot_delivery.submitted.back().snapshot.tick;
            }
            replication = std::move(details);
        }
        auto selected_details = std::optional<simnet::SelectedEntityDetails>{};
        debug_storage.spheres.clear();
        debug_storage.vectors.clear();
        debug_storage.boxes.clear();
        debug_storage.cones.clear();
        debug_storage.labels.clear();
        auto const player_count = static_cast<std::size_t>(
            std::ranges::count(snapshot.classifications, simnet::player_entity_classification)
        );
        debug_storage.labels.reserve(peers.size() * 3U + player_count * 2U);
        if (selected_debug.has_value())
        {
            selected_details = simnet::SelectedEntityDetails{
                .id = selected_debug->id,
                .velocity = selected_debug->velocity,
                .acceleration = selected_debug->acceleration,
                .speed = selected_debug->speed,
                .maximum_speed = config.boids.max_speed,
                .raw_candidate_count = selected_debug->raw_candidate_count,
                .retained_neighbor_count = selected_debug->retained_neighbor_count,
                .separation_neighbor_count = selected_debug->separation_neighbor_count,
                .alignment_neighbor_count = selected_debug->alignment_neighbor_count,
                .cohesion_neighbor_count = selected_debug->cohesion_neighbor_count,
                .hue_neighbor_count = selected_debug->hue_neighbor_count,
                .current_cell =
                    simnet::SelectedCellCoord{
                        .x = selected_debug->current_cell.x,
                        .y = selected_debug->current_cell.y,
                        .z = selected_debug->current_cell.z,
                    },
                .queried_cell_count =
                    static_cast<std::uint32_t>(selected_debug->queried_cell_bounds.size()),
                .displayed_queried_cell_count =
                    static_cast<std::uint32_t>(selected_debug->queried_cell_bounds.size()),
                .query_visualization_capped = false,
                .separation_radius = selected_debug->separation_radius,
                .alignment_radius = selected_debug->alignment_radius,
                .cohesion_radius = selected_debug->cohesion_radius,
                .query_radius = selected_debug->query_radius,
                .field_of_view_degrees = selected_debug->field_of_view_degrees,
                .maximum_neighbors = selected_debug->maximum_neighbors,
                .neighbor_cap_hit = selected_debug->neighbor_cap_hit,
                .overlap_recovery = selected_debug->overlap_recovery,
                .acceleration_saturated = selected_debug->acceleration_saturated,
                .wall_guard = selected_debug->wall_guard,
                .wander_active = selected_debug->wander_active,
                .hue_assimilation_active = selected_debug->hue_assimilation_active,
                .hue_drift_active = selected_debug->hue_drift_active,
                .separation = selected_debug->separation,
                .alignment = selected_debug->alignment,
                .cohesion = selected_debug->cohesion,
                .containment = selected_debug->containment,
                .wander = selected_debug->wander,
                .current_hue = selected_debug->current_hue,
                .hue_target = selected_debug->hue_target,
                .hue_delta = selected_debug->hue_delta,
                .applied_hue_step = selected_debug->applied_hue_step,
                .replicated = false,
            };
            auto const found = std::ranges::lower_bound(snapshot.ids, selected_debug->id);
            if (found != snapshot.ids.end() && *found == selected_debug->id)
            {
                auto const index =
                    static_cast<std::size_t>(std::distance(snapshot.ids.begin(), found));
                auto const position = snapshot.positions[index];
                auto const heading = snapshot.headings[index];
                debug_storage.spheres = {
                    {
                        .center = position,
                        .radius = selected_debug->separation_radius,
                        .color = {230U, 94U, 94U, 110U},
                        .label = "separation",
                    },
                    {
                        .center = position,
                        .radius = selected_debug->alignment_radius,
                        .color = {92U, 174U, 235U, 85U},
                        .label = "alignment",
                    },
                    {
                        .center = position,
                        .radius = selected_debug->cohesion_radius,
                        .color = {124U, 214U, 156U, 85U},
                        .label = "cohesion",
                    },
                };
                debug_storage.vectors = {{
                    position,
                    selected_debug->separation,
                    {230U, 94U, 94U, 255U},
                    "separation",
                }};
                if (config.boids.player_predator.enabled)
                {
                    debug_storage.vectors.push_back({
                        position,
                        selected_debug->predator,
                        {255U, 78U, 68U, 255U},
                        "Player predator",
                    });
                }
                debug_storage.vectors.push_back({
                    position,
                    selected_debug->containment,
                    {247U, 184U, 74U, 255U},
                    "containment",
                });
                debug_storage.vectors.push_back({
                    position,
                    selected_debug->alignment,
                    {92U, 174U, 235U, 255U},
                    "alignment",
                });
                debug_storage.vectors.push_back({
                    position,
                    selected_debug->cohesion,
                    {124U, 214U, 156U, 255U},
                    "cohesion",
                });
                if (config.boids.player_lure.enabled)
                {
                    debug_storage.vectors.push_back({
                        position,
                        selected_debug->lure,
                        {252U, 112U, 202U, 255U},
                        "Player lure",
                    });
                }
                debug_storage.vectors.push_back({
                    position,
                    selected_debug->wander,
                    {198U, 126U, 255U, 255U},
                    "wander",
                });
                debug_storage.vectors.push_back({
                    position,
                    selected_debug->acceleration,
                    {245U, 245U, 245U, 255U},
                    "acceleration",
                });
                debug_storage.boxes.reserve(selected_debug->queried_cell_bounds.size());
                for (auto const bounds : selected_debug->queried_cell_bounds)
                {
                    debug_storage.boxes.push_back({
                        .bounds = bounds,
                        .color = {180U, 205U, 235U, 72U},
                        .label = "queried cell",
                    });
                }
                debug_storage.cones.push_back({
                    .apex = position,
                    .direction = heading,
                    .length = selected_debug->query_radius,
                    .half_angle_degrees = selected_debug->field_of_view_degrees * 0.5F,
                    .color = {255U, 205U, 120U, 90U},
                    .label = "FOV",
                });
            }
        }
        for (auto const& overlay_peer : peers)
        {
            if (!overlay_peer.role.has_value() || config.pipeline.area_of_interest.mode == "none")
            {
                continue;
            }
            auto const source = resolve_interest_source(overlay_peer, snapshot);
            auto label = [&](std::string_view suffix) -> std::string_view
            {
                debug_storage.labels.push_back(
                    "peer " + std::to_string(overlay_peer.peer) + " " +
                    std::string{peer_role_name(overlay_peer.role)} + " " + std::string{suffix}
                );
                return debug_storage.labels.back();
            };
            if (source.has_value() && config.pipeline.level_of_detail.mode == "distance_bands")
            {
                debug_storage.spheres.push_back({
                    .center = source->position,
                    .radius = config.pipeline.level_of_detail.near_distance,
                    .color = {118U, 238U, 146U, 90U},
                    .label = label("LOD near"),
                });
                debug_storage.spheres.push_back({
                    .center = source->position,
                    .radius = config.pipeline.level_of_detail.medium_distance,
                    .color = {248U, 202U, 92U, 82U},
                    .label = label("LOD medium"),
                });
            }
            if (source.has_value() && config.pipeline.area_of_interest.mode == "radius")
            {
                debug_storage.spheres.push_back({
                    .center = source->position,
                    .radius = config.pipeline.area_of_interest.radius,
                    .color = {102U, 214U, 255U, 72U},
                    .label = label("AOI radius"),
                });
            }
            else if (source.has_value())
            {
                debug_storage.cones.push_back({
                    .apex = source->position,
                    .direction = source->forward,
                    .length = config.pipeline.area_of_interest.radius,
                    .half_angle_degrees = config.pipeline.area_of_interest.fov_degrees * 0.5F,
                    .color = {102U, 214U, 255U, 90U},
                    .label = label("AOI cone"),
                });
            }
        }
        for (std::size_t index = 0; index < snapshot.size(); ++index)
        {
            if (snapshot.classifications[index] != simnet::player_entity_classification)
            {
                continue;
            }
            auto label = [&](std::string_view suffix) -> std::string_view
            {
                debug_storage.labels.push_back(
                    "Player " + std::to_string(snapshot.ids[index]) + " " + std::string{suffix}
                );
                return debug_storage.labels.back();
            };
            if (config.boids.player_lure.enabled)
            {
                debug_storage.spheres.push_back({
                    .center = snapshot.positions[index],
                    .radius = config.boids.player_lure.radius,
                    .color = {252U, 112U, 202U, 72U},
                    .label = label("lure"),
                });
            }
            if (config.boids.player_predator.enabled)
            {
                debug_storage.spheres.push_back({
                    .center = snapshot.positions[index],
                    .radius = config.boids.player_predator.radius,
                    .color = {255U, 78U, 68U, 72U},
                    .label = label("predator"),
                });
            }
        }
        return {
            .entities =
                {
                    .ids = snapshot.ids,
                    .positions = snapshot.positions,
                    .headings = snapshot.headings,
                    .hues = snapshot.hues,
                },
            .info =
                {
                    .tick = snapshot.tick,
                    .world_bounds = simnet::make_centered_bounds(config.simulation.world_half),
                    .frame_delta = frame_delta,
                    .fixed_tick_rate_hz = config.simulation.tick_rate_hz,
                    .simulation_paused = paused,
                    .interpolation = interpolation,
                    .context =
                        {
                            .kind = simnet::ViewerKind::Server,
                        },
                    .capabilities =
                        {
                            .can_pause_simulation = true,
                            .has_networking = true,
                            .has_entity_diagnostics = true,
                            .has_spatial_visualization = true,
                        },
                    .connection = connection,
                    .replication = std::move(replication),
                },
            .selected_details = std::move(selected_details),
            .spatial =
                simnet::SpatialDebugView{
                    .cells = spatial.displayed_cells,
                    .occupied_cell_count = spatial.grid.stats.occupied_cell_count,
                    .max_cell_occupancy = spatial.grid.stats.max_cell_occupancy,
                    .average_occupied_cell_load = spatial.grid.stats.average_occupied_cell_load,
                    .query_radius = std::max({
                        config.boids.separation_radius,
                        config.boids.alignment_radius,
                        config.boids.cohesion_radius,
                    }),
                    .display_capped =
                        spatial.displayed_cells.size() < spatial.grid.occupied_cells.size(),
                },
            .setup = setup,
            .debug_primitives = {
                .spheres = debug_storage.spheres,
                .vectors = debug_storage.vectors,
                .boxes = debug_storage.boxes,
                .cones = debug_storage.cones,
            },
        };
    }

    void rebuild_spatial_render_view(
        SpatialRenderStorage& storage,
        simnet::WorldSnapshot const& snapshot,
        simnet::SharedConfig const& config,
        simnet::Vec3f display_anchor,
        std::uint32_t visible_cell_limit
    )
    {
        {
            SIMNET_TRACE_SCOPE_CATEGORY("render.spatial.build", simnet::LogCategory::Spatial);
            auto const settings = simnet::make_spatial_grid_settings(
                simnet::make_centered_bounds(config.simulation.world_half),
                config.spatial.cell_size
            );
            if (storage.grid.settings.cell_size != settings.cell_size ||
                storage.grid.settings.bounds.min.x != settings.bounds.min.x ||
                storage.grid.settings.bounds.max.x != settings.bounds.max.x)
            {
                simnet::resize_spatial_grid(storage.grid, settings);
            }
            simnet::prepare_spatial_grid_scratch(storage.scratch, snapshot.positions.size(), 1U);
            simnet::build_spatial_grid_serial(
                storage.grid,
                storage.scratch,
                snapshot.positions,
                snapshot.ids
            );
        }

        {
            SIMNET_TRACE_SCOPE_CATEGORY("spatial.display_candidates", simnet::LogCategory::Spatial);
            storage.candidates.clear();
            storage.candidates.reserve(storage.grid.occupied_cells.size());
            for (auto const& range : storage.grid.occupied_cells)
            {
                auto const bounds = simnet::cell_bounds(
                    storage.grid,
                    simnet::cell_coord_from_key(storage.grid, range.key)
                );
                auto const center = (bounds.min + bounds.max) * 0.5F;
                storage.candidates.push_back({
                    .key = range.key,
                    .bounds = bounds,
                    .entity_count = range.count,
                    .distance_squared = simnet::length_squared(center - display_anchor),
                });
            }
        }
        {
            SIMNET_TRACE_SCOPE_CATEGORY("spatial.display_sort", simnet::LogCategory::Spatial);
            std::ranges::sort(
                storage.candidates,
                [](SpatialRenderCandidate const& lhs, SpatialRenderCandidate const& rhs)
                {
                    if (lhs.distance_squared == rhs.distance_squared)
                    {
                        return lhs.key < rhs.key;
                    }
                    return lhs.distance_squared < rhs.distance_squared;
                }
            );
        }
        {
            SIMNET_TRACE_SCOPE_CATEGORY("spatial.display_view", simnet::LogCategory::Spatial);
            auto const display_count =
                std::min<std::size_t>(visible_cell_limit, storage.candidates.size());
            storage.displayed_cells.clear();
            storage.displayed_cells.reserve(display_count);
            for (std::size_t index = 0; index < display_count; ++index)
            {
                storage.displayed_cells.push_back({
                    .bounds = storage.candidates[index].bounds,
                    .entity_count = storage.candidates[index].entity_count,
                });
            }
        }
        SIMNET_TRACE_PLOT(
            "render.spatial.occupied_cells",
            static_cast<double>(storage.grid.occupied_cells.size())
        );
        SIMNET_TRACE_PLOT(
            "render.spatial.displayed_cells",
            static_cast<double>(storage.displayed_cells.size())
        );
    }
#endif

    [[nodiscard]] std::uint64_t mix64(std::uint64_t value) noexcept
    {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31U);
    }

    [[nodiscard]] float unit_hash(std::uint64_t value) noexcept
    {
        auto const bits = static_cast<std::uint32_t>(mix64(value) >> 40U);
        return static_cast<float>(bits) / static_cast<float>(0xFFFFFFU);
    }

    [[nodiscard]] simnet::EntityState
    initial_boid(std::uint32_t index, std::uint32_t count, simnet::SharedConfig const& config)
    {
        auto const side = std::max(
            1U,
            static_cast<std::uint32_t>(std::ceil(std::cbrt(static_cast<double>(count))))
        );
        auto const x_index = index % side;
        auto const y_index = (index / side) % side;
        auto const z_index = index / (side * side);
        auto const cell = config.simulation.world_half * 2.0F / static_cast<float>(side);
        auto const id = static_cast<simnet::EntityNetId>(index + 1U);
        auto const key = config.run.seed ^ (static_cast<std::uint64_t>(id) << 1U);
        auto const coordinate = [&](std::uint32_t cell_index, std::uint64_t salt)
        {
            auto const jitter = (unit_hash(key ^ salt) - 0.5F) * 0.5F;
            return -config.simulation.world_half +
                   (static_cast<float>(cell_index) + 0.5F + jitter) * cell;
        };
        auto const heading = simnet::normalize_or(
            simnet::Vec3f{
                unit_hash(key ^ 0x243f6a8885a308d3ULL) * 2.0F - 1.0F,
                unit_hash(key ^ 0x13198a2e03707344ULL) * 2.0F - 1.0F,
                unit_hash(key ^ 0xa4093822299f31d0ULL) * 2.0F - 1.0F,
            },
            simnet::Vec3f{1.0F, 0.0F, 0.0F}
        );
        return {
            .id = id,
            .classification = simnet::boid_entity_classification,
            .position =
                {
                    coordinate(x_index, 0x082efa98ec4e6c89ULL),
                    coordinate(y_index, 0x452821e638d01377ULL),
                    coordinate(z_index, 0xbe5466cf34e90c6cULL),
                },
            .heading = heading,
            .hue = static_cast<std::uint8_t>((index * 23U) & 0xFFU),
        };
    }

    [[nodiscard]] simnet::AuthoritativeSpawnReport
    initialize_world(flecs::world& world, simnet::SharedConfig const& config)
    {
        SIMNET_TRACE_SCOPE_CATEGORY("server.initialize_world", simnet::LogCategory::Simulation);
        auto boids = std::vector<simnet::EntityState>{};
        {
            SIMNET_TRACE_SCOPE_CATEGORY(
                "server.initial_state_generation",
                simnet::LogCategory::Simulation
            );
            boids.reserve(config.simulation.initial_boid_count);
            for (std::uint32_t index = 0; index < config.simulation.initial_boid_count; ++index)
            {
                boids.push_back(initial_boid(index, config.simulation.initial_boid_count, config));
            }
        }
        auto const report = simnet::append_authoritative_boids(world, boids);
        SIMNET_TRACE_PLOT(
            "server.initial_requested_entities",
            static_cast<double>(report.requested_count)
        );
        SIMNET_TRACE_PLOT(
            "server.initial_spawned_entities",
            static_cast<double>(report.spawned_count)
        );
        return report;
    }

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
        if (ack.newest_received_snapshot == 0U ||
            ack.newest_received_snapshot != ack.newest_applied_snapshot ||
            ack.newest_received_snapshot > peer.snapshot_delivery.latest_submitted_sequence)
        {
            return false;
        }
        if (ack.newest_applied_snapshot == peer.latest_ack.newest_applied_snapshot)
        {
            return ack.newest_received_snapshot == peer.latest_ack.newest_received_snapshot &&
                   ack.received_mask == peer.latest_ack.received_mask;
        }
        return true;
    }

#if defined(SIMNET_ENABLE_RENDER)
    void copy_snapshot_reusing_capacity(
        simnet::WorldSnapshot const& source,
        simnet::WorldSnapshot& destination
    )
    {
        destination.tick = source.tick;
        destination.ids.resize(source.ids.size());
        destination.classifications.resize(source.classifications.size());
        destination.positions.resize(source.positions.size());
        destination.headings.resize(source.headings.size());
        destination.hues.resize(source.hues.size());
        std::copy(source.ids.begin(), source.ids.end(), destination.ids.begin());
        std::copy(
            source.classifications.begin(),
            source.classifications.end(),
            destination.classifications.begin()
        );
        std::copy(source.positions.begin(), source.positions.end(), destination.positions.begin());
        std::copy(source.headings.begin(), source.headings.end(), destination.headings.begin());
        std::copy(source.hues.begin(), source.hues.end(), destination.hues.begin());
    }

    void retain_presentation_snapshot(
        PresentationSnapshotState& state,
        simnet::WorldSnapshot const& snapshot
    )
    {
        if (state.has_current && state.current.tick == snapshot.tick)
        {
            return;
        }
        if (state.has_current)
        {
            std::swap(state.previous, state.current);
            state.has_previous = true;
        }
        copy_snapshot_reusing_capacity(snapshot, state.current);
        state.has_current = true;
    }

    [[nodiscard]] simnet::WorldSnapshot const* presentation_snapshot(
        PresentationSnapshotState& state,
        bool interpolation_enabled,
        bool paused,
        double alpha
    )
    {
        if (!state.has_current)
        {
            return nullptr;
        }
        if (!interpolation_enabled || paused || !state.has_previous)
        {
            return &state.current;
        }
        SIMNET_TRACE_SCOPE_CATEGORY("server.presentation.interpolate", simnet::LogCategory::Render);
        // Presentation snapshots copy only successful authoritative extractions.
        auto const interpolated = simnet::interpolate_world_snapshots_unchecked(
            state.previous,
            state.current,
            alpha,
            state.interpolated
        );
        return interpolated.valid ? &state.interpolated : nullptr;
    }
#endif

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
            peer.latest_ack = ack;
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

    [[nodiscard]] bool
    send_pause_state(simnet::TransportServer& transport, PeerRuntimeState const& peer, bool paused)
    {
        auto const bytes = simnet::app::encode_app_message({
            .kind = simnet::app::AppMessageKind::PauseState,
            .paused = paused,
        });
        auto const sent = transport.send({
            .peer = peer.peer,
            .lane = simnet::app::control_lane,
            .delivery = simnet::TransportDelivery::ReliableSequenced,
            .payload = bytes,
        });
        if (!sent.ok)
        {
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Error,
                "server pause-state send failed: " + sent.error.message
            );
            return false;
        }
        return true;
    }

    [[nodiscard]] bool
    send_join_accepted(simnet::TransportServer& transport, PeerRuntimeState const& peer)
    {
        auto const bytes = simnet::app::encode_app_message({
            .kind = simnet::app::AppMessageKind::JoinAccepted,
            .role = *peer.role,
            .peer_id = peer.peer,
            .player_id = peer.player_id,
        });
        auto const sent = transport.send({
            .peer = peer.peer,
            .lane = simnet::app::control_lane,
            .delivery = simnet::TransportDelivery::ReliableSequenced,
            .payload = bytes,
        });
        if (!sent.ok)
        {
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Error,
                "server join response send failed: " + sent.error.message
            );
            return false;
        }
        return true;
    }

    [[nodiscard]] simnet::PlayerControlState
    player_control(simnet::app::PlayerInputMessage input) noexcept
    {
        using simnet::app::PlayerButton;
        return {
            .pitch_up = simnet::app::button_down(input, PlayerButton::W),
            .yaw_left = simnet::app::button_down(input, PlayerButton::A),
            .pitch_down = simnet::app::button_down(input, PlayerButton::S),
            .yaw_right = simnet::app::button_down(input, PlayerButton::D),
            .accelerate = simnet::app::button_down(input, PlayerButton::Shift),
            .decelerate = simnet::app::button_down(input, PlayerButton::Control),
            .left_mouse = simnet::app::button_down(input, PlayerButton::LeftMouse),
            .right_mouse = simnet::app::button_down(input, PlayerButton::RightMouse),
        };
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

    [[nodiscard]] bool poll_transport(
        flecs::world* world,
        simnet::TransportServer& transport,
        PeerRuntimeStates& peers,
        std::uint32_t max_clients,
        std::vector<simnet::TransportEvent>& events,
        std::uint32_t timeout_ms,
        simnet::TransportDelivery snapshot_delivery,
        bool& simulation_paused,
        bool& pause_state_changed,
        CurrentSnapshotState* snapshot_state
    )
    {
        events.clear();
        auto const result = transport.poll(events, timeout_ms);
        if (!result.ok)
        {
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Error,
                "server transport poll failed: " + result.error.message
            );
            return false;
        }

        for (auto const& event : events)
        {
            if (auto const* ready = std::get_if<simnet::PeerSessionReady>(&event))
            {
                auto const found = find_peer(peers, ready->peer);
                auto const admission = simnet::app::detail::peer_admission(
                    peers,
                    ready->peer,
                    max_clients,
                    &PeerRuntimeState::peer
                );
                if (admission != simnet::app::detail::PeerAdmission::Accept)
                {
                    transport.disconnect(ready->peer, simnet::DisconnectCode::ServerFull);
                }
                else
                {
                    peers.insert(found, PeerRuntimeState{.peer = ready->peer});
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Info,
                        "server session ready peer=" + std::to_string(ready->peer)
                    );
                }
            }
            else if (auto const* disconnected = std::get_if<simnet::PeerDisconnected>(&event))
            {
                static_cast<void>(erase_peer_state(
                    world,
                    peers,
                    disconnected->peer,
                    snapshot_state,
                    snapshot_delivery
                ));
            }
            else if (auto const* packet = std::get_if<simnet::ReceivedPacket>(&event))
            {
                auto found = find_peer(peers, packet->peer);
                if (found == peers.end() || found->peer != packet->peer)
                {
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Error,
                        "server received application payload from an unknown peer"
                    );
                    transport.disconnect(packet->peer, simnet::DisconnectCode::ProtocolMismatch);
                    continue;
                }
                auto* peer = &*found;
                auto reject_peer = [&](simnet::DisconnectCode code)
                {
                    auto const peer_id = peer->peer;
                    transport.disconnect(peer_id, code);
                    static_cast<void>(
                        erase_peer_state(world, peers, peer_id, snapshot_state, snapshot_delivery)
                    );
                };

                if (packet->lane == simnet::app::control_lane)
                {
                    auto message = simnet::app::AppMessage{};
                    if (packet->delivery != simnet::TransportDelivery::ReliableSequenced ||
                        !simnet::app::decode_app_message(packet->payload, message))
                    {
                        simnet::log(
                            simnet::LogCategory::Transport,
                            simnet::LogLevel::Error,
                            "server received invalid application-control message"
                        );
                        reject_peer(simnet::DisconnectCode::ProtocolMismatch);
                        continue;
                    }
                    if (message.kind == simnet::app::AppMessageKind::JoinRequest &&
                        !peer->role.has_value())
                    {
                        peer->role = message.role;
                        if (message.role == simnet::app::ClientRole::Player)
                        {
                            if (world == nullptr || snapshot_state == nullptr)
                            {
                                simnet::log(
                                    simnet::LogCategory::Simulation,
                                    simnet::LogLevel::Error,
                                    "synthetic workload accepts stationary observer clients only"
                                );
                                reject_peer(simnet::DisconnectCode::ProtocolMismatch);
                                continue;
                            }
                            peer->player_id = simnet::spawn_authoritative_player(*world);
                            if (peer->player_id == 0U)
                            {
                                simnet::log(
                                    simnet::LogCategory::Simulation,
                                    simnet::LogLevel::Error,
                                    "server failed to create authoritative player"
                                );
                                reject_peer(simnet::DisconnectCode::ServerFull);
                                continue;
                            }
                            snapshot_state->dirty = true;
                        }
                        simnet::log(
                            simnet::LogCategory::Simulation,
                            simnet::LogLevel::Info,
                            "server accepted role=" +
                                std::string{
                                    message.role == simnet::app::ClientRole::Player
                                        ? "player"
                                        : "stationary_observer"
                                } +
                                " peer_id=" + std::to_string(peer->peer) +
                                " player_id=" + std::to_string(peer->player_id)
                        );
                        if (!send_join_accepted(transport, *peer) ||
                            !send_pause_state(transport, *peer, simulation_paused))
                        {
                            reject_peer(simnet::DisconnectCode::TransportError);
                        }
                    }
                    else if (
                        message.kind == simnet::app::AppMessageKind::PauseSetRequest &&
                        peer->role.has_value()
                    )
                    {
                        pause_state_changed =
                            pause_state_changed || simulation_paused != message.paused;
                        simulation_paused = message.paused;
                        if (pause_state_changed)
                        {
                            simnet::log(
                                simnet::LogCategory::Simulation,
                                simnet::LogLevel::Info,
                                simulation_paused ? "server simulation paused by client"
                                                  : "server simulation resumed by client"
                            );
                        }
                    }
                    else
                    {
                        simnet::log(
                            simnet::LogCategory::Transport,
                            simnet::LogLevel::Error,
                            "server rejected unauthorized application-control message"
                        );
                        reject_peer(simnet::DisconnectCode::ProtocolMismatch);
                    }
                    continue;
                }

                if (packet->lane != simnet::app::input_lane)
                {
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Error,
                        "server received application payload on an unauthorized lane"
                    );
                    reject_peer(simnet::DisconnectCode::ProtocolMismatch);
                    continue;
                }

                auto const kind = simnet::app::decode_app_message_kind(packet->payload);
                if (kind == simnet::app::AppMessageKind::SnapshotAck)
                {
                    auto ack = simnet::app::SnapshotAck{};
                    if (packet->delivery == simnet::TransportDelivery::ReliableSequenced &&
                        simnet::app::decode_snapshot_ack(packet->payload, ack) &&
                        valid_ack(*peer, ack))
                    {
                        apply_ack(*peer, ack);
                    }
                    else
                    {
                        simnet::log(
                            simnet::LogCategory::Transport,
                            simnet::LogLevel::Warn,
                            "server ignored invalid snapshot ACK"
                        );
                    }
                    continue;
                }

                if (kind == simnet::app::AppMessageKind::SnapshotRecoveryRequest)
                {
                    auto request = simnet::app::SnapshotRecoveryRequest{};
                    auto const valid =
                        packet->delivery == simnet::TransportDelivery::ReliableSequenced &&
                        simnet::app::decode_snapshot_recovery_request(packet->payload, request) &&
                        simnet::app::valid_recovery_request(
                            peer->snapshot_delivery,
                            request.rejected_update_sequence,
                            request.missing_baseline_sequence
                        );
                    if (valid)
                    {
                        ++peer->snapshot_delivery.recovery_request_count;
                        simnet::app::enter_snapshot_recovery(
                            peer->snapshot_delivery,
                            simnet::app::SnapshotRecoveryReason::ClientRequest
                        );
                    }
                    else
                    {
                        simnet::log(
                            simnet::LogCategory::Transport,
                            simnet::LogLevel::Warn,
                            "server ignored invalid snapshot recovery request"
                        );
                    }
                    continue;
                }

                auto valid = peer->role.has_value() &&
                             packet->delivery == simnet::TransportDelivery::UnreliableSequenced;
                auto rate_limited = false;
                if (valid && peer->role == simnet::app::ClientRole::Player)
                {
                    auto decoded = simnet::app::PlayerInputMessage{};
                    valid = world != nullptr && peer->player_id != 0U &&
                            simnet::app::decode_player_input(packet->payload, decoded) &&
                            simnet::set_authoritative_player_input(
                                *world,
                                peer->player_id,
                                player_control(decoded)
                            );
                }
                else if (valid)
                {
                    auto decoded = simnet::app::StationaryObserverInterestMessage{};
                    valid =
                        simnet::app::decode_stationary_observer_interest(packet->payload, decoded);
                    if (valid)
                    {
                        auto const accepted = simnet::app::accept_stationary_observer_interest(
                            peer->stationary_observer_interest,
                            decoded,
                            simnet::steady_now_ns()
                        );
                        rate_limited =
                            accepted == simnet::app::StationaryObserverInterestResult::RateLimited;
                        valid =
                            accepted == simnet::app::StationaryObserverInterestResult::Accepted ||
                            rate_limited;
                    }
                }
                if (!valid)
                {
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Error,
                        "server rejected invalid or unauthorized application input"
                    );
                    reject_peer(simnet::DisconnectCode::ProtocolMismatch);
                }
                else if (rate_limited)
                {
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Debug,
                        "server ignored rate-limited stationary observer interest update"
                    );
                }
            }
            else if (auto const* error = std::get_if<simnet::TransportErrorEvent>(&event))
            {
                simnet::log(
                    simnet::LogCategory::Transport,
                    simnet::LogLevel::Error,
                    "server transport error: " + error->message
                );
                return false;
            }
        }
        return true;
    }

    void broadcast_pause_state(
        flecs::world* world,
        simnet::TransportServer& transport,
        PeerRuntimeStates& peers,
        bool paused,
        CurrentSnapshotState* snapshot_state,
        simnet::TransportDelivery snapshot_delivery
    )
    {
        auto index = std::size_t{};
        while (index < peers.size())
        {
            if (!peers[index].role.has_value() || send_pause_state(transport, peers[index], paused))
            {
                ++index;
                continue;
            }
            auto const failed_peer = peers[index].peer;
            transport.disconnect(failed_peer, simnet::DisconnectCode::TransportError);
            static_cast<void>(
                erase_peer_state(world, peers, failed_peer, snapshot_state, snapshot_delivery)
            );
        }
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
        std::uint64_t compression_dictionary_fingerprint{};
    };

    [[nodiscard]] simnet::ServerReplicationMeasurement make_server_measurement(
        ServerEvidenceIdentity const& identity,
        simnet::PipelineDefinition const& pipeline,
        simnet::app::CompressionSettings const& compression,
        simnet::PacketizationSettings const& packetization,
        simnet::TransportDelivery delivery,
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
            .latest_submitted_sequence = peer.snapshot_delivery.latest_submitted_sequence,
            .cadence_enabled = simnet::has_all_flags(
                pipeline.techniques,
                simnet::PipelineTechniqueFlags::SendInterval
            ),
            .incremental_enabled = simnet::has_all_flags(
                pipeline.techniques,
                simnet::PipelineTechniqueFlags::Incremental
            ),
            .quantization_enabled = simnet::has_all_flags(
                pipeline.techniques,
                simnet::PipelineTechniqueFlags::Quantization
            ),
            .oct_heading_enabled = simnet::has_all_flags(
                pipeline.techniques,
                simnet::PipelineTechniqueFlags::OctHeading
            ),
            .delta_enabled =
                simnet::has_all_flags(pipeline.techniques, simnet::PipelineTechniqueFlags::Delta),
            .delta_field_mask_enabled = simnet::has_all_flags(
                pipeline.techniques,
                simnet::PipelineTechniqueFlags::DeltaFieldMask
            ),
            .bit_packing_enabled = simnet::has_all_flags(
                pipeline.techniques,
                simnet::PipelineTechniqueFlags::BitPacking
            ),
            .cadence_interval_ticks = pipeline.send_interval.interval_ticks,
            .incremental_limit = pipeline.incremental.max_entities_per_update,
            .incremental_cursor_before = peer.pipeline_state.incremental_cursor,
            .incremental_cursor_after = peer.pipeline_state.incremental_cursor,
            .incremental_seeded_before = peer.pipeline_state.incremental_seeded,
            .incremental_seeded_after = peer.pipeline_state.incremental_seeded,
            .area_of_interest_mode = area_of_interest_mode_name(pipeline.area_of_interest.mode),
            .area_of_interest_source_status =
                pipeline.area_of_interest.mode == simnet::AreaOfInterestMode::None ? "not_required"
                                                                                   : "unavailable",
            .level_of_detail_mode = level_of_detail_mode_name(pipeline.level_of_detail.mode),
            .compression_mode = simnet::app::compression_mode_name(compression.mode),
            .compression_dictionary = compression.dictionary,
            .compression_dictionary_fingerprint = identity.compression_dictionary_fingerprint,
            .packetization_enabled = packetization.enabled,
            .delivery_mode = simnet::app::transport_delivery_name(delivery),
            .recovery_active = peer.snapshot_delivery.recovery_active,
            .recovery_reason =
                simnet::app::snapshot_recovery_reason_name(peer.snapshot_delivery.recovery_reason),
            .submissions_since_ack_progress = peer.snapshot_delivery.submissions_since_ack_progress,
        };
    }

    [[nodiscard]] std::string_view
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
                case simnet::CompressionEncoding::ZstdDictionary:
                    return "zstd_dictionary";
            }
        }
        if (report.zstd_packet_count != 0U && report.raw_packet_count != 0U)
        {
            return "mixed";
        }
        return report.zstd_packet_count != 0U ? "zstd" : "raw";
    }

    void flatten_transport_reports(
        simnet::ServerReplicationMeasurement& measurement,
        ServerCompressionReport const& compression,
        simnet::PacketizationReport const& packetization,
        std::uint32_t attempted_submissions,
        std::uint32_t accepted_submissions
    ) noexcept
    {
        measurement.compression_encoding = compression_result_name(compression);
        measurement.compression_dictionary_id = compression.dictionary_id;
        measurement.compression_raw_fallback =
            compression.mode != simnet::app::CompressionMode::None &&
            (compression.whole_encoding == simnet::CompressionEncoding::Raw ||
             compression.raw_packet_count != 0U);
        measurement.compression_input_bytes = compression.compression_input_bytes;
        measurement.compression_payload_bytes = compression.compression_payload_bytes;
        measurement.compression_envelope_bytes = compression.compression_envelope_bytes;
        measurement.compression_output_bytes = compression.compression_output_bytes;
        measurement.compression_elapsed_time = compression.compression_cpu_time;
        measurement.packet_group_id = packetization.group_id;
        measurement.packet_group_bytes = packetization.group_bytes;
        measurement.packet_payload_bytes =
            packetization.total_packet_bytes - packetization.total_header_bytes;
        measurement.packet_header_bytes = packetization.total_header_bytes;
        measurement.packet_chunk_count = packetization.chunk_count;
        measurement.attempted_submissions = attempted_submissions;
        measurement.accepted_submissions = accepted_submissions;
    }

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
        simnet::app::CompressionSettings compression,
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
        simnet::ServerReplicationCsvWriter& csv,
        simnet::app::CompressionCorpusWriter& compression_corpus,
        simnet::app::LoadedCompressionDictionary const* compression_dictionary
    )
    {
        SIMNET_TRACE_SCOPE_CATEGORY("server.fixed_tick", simnet::LogCategory::Simulation);
        auto const* source_snapshot = static_cast<simnet::WorldSnapshot const*>(nullptr);
        auto extraction = simnet::ServerSnapshotExtractionReport{};
        auto extraction_elapsed_time = simnet::Nanoseconds{};
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
            auto const extraction_start = simnet::steady_now_ns();
            source_snapshot = &simnet::update_synthetic_world_snapshot(
                *synthetic_snapshot_settings,
                *synthetic_change_settings,
                tick,
                *synthetic_state
            );
            extraction_elapsed_time = simnet::steady_now_ns() - extraction_start;
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
            auto const extraction_start = simnet::steady_now_ns();
            extraction = ensure_current_snapshot(*world, tick, *snapshot_state);
            extraction_elapsed_time = simnet::steady_now_ns() - extraction_start;
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
                    [&]
                    {
                        auto value = make_server_measurement(
                            evidence_identity,
                            pipeline,
                            compression,
                            packetization,
                            delivery,
                            failed_peer,
                            tick
                        );
                        value.snapshot_extraction_elapsed_time = extraction_elapsed_time;
                        value.total_replication_elapsed_time = extraction_elapsed_time;
                        return value;
                    }()
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

        auto first_joined_peer = true;
        auto replicate_peer = [&](PeerRuntimeState& peer_state)
        {
            auto* peer = &peer_state;
            auto measurement = make_server_measurement(
                evidence_identity,
                pipeline,
                compression,
                packetization,
                delivery,
                *peer,
                tick
            );
            auto const total_start = simnet::steady_now_ns();
            auto evidence_io_time = simnet::Nanoseconds{};
            auto const finish_total_time = [&]
            {
                return simnet::steady_now_ns() - total_start - evidence_io_time;
            };
            if (first_joined_peer)
            {
                measurement.snapshot_extraction_elapsed_time = extraction_elapsed_time;
                first_joined_peer = false;
            }

            auto const baseline_start = simnet::steady_now_ns();
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
                    measurement.baseline_resolution_elapsed_time =
                        simnet::steady_now_ns() - baseline_start;
                    measurement.source_entity_count = extraction.entity_count;
                    measurement.outcome = simnet::ServerReplicationOutcome::Skipped;
                    measurement.outcome_detail = "area_of_interest_source_unavailable";
                    measurement.total_replication_elapsed_time = finish_total_time();
                    observe_server_measurement(measurements, csv, measurement);
                    return peer->role != simnet::app::ClientRole::Player;
                }
                measurement.area_of_interest_source_status = "available";
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

            measurement.baseline_resolution_elapsed_time = simnet::steady_now_ns() - baseline_start;
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
                        .collect_representation_quality = collect_representation_quality,
                        .interest_source =
                            interest_source.has_value() ? &*interest_source : nullptr,
                        .candidate_indices = peer->area_of_interest_candidates,
                    }
                );
            }
            measurement.encode_elapsed_time = simnet::steady_now_ns() - encode_start;
            measurement.source_entity_count = extraction.entity_count;
            measurement.recovery_active = peer->snapshot_delivery.recovery_active;
            measurement.recovery_reason =
                simnet::app::snapshot_recovery_reason_name(peer->snapshot_delivery.recovery_reason);
            measurement.recovery_forced_upsert_count =
                force_full_replace ? encoded.report.upsert_count
                                   : static_cast<std::uint32_t>(peer->recovery_upsert_ids.size()) +
                                         encoded.report.level_of_detail.recovery_forced_count;
            measurement.recovery_forced_delete_count =
                force_full_replace ? encoded.report.delete_count : 0U;
            measurement.encoded_update_bytes =
                static_cast<std::uint32_t>(encoded.update.bytes.size());
            auto const observe_encoded_measurement = [&]
            {
                simnet::app::flatten_server_encode_report(
                    measurement,
                    encoded.report,
                    pending_pipeline_state
                );
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
                measurement.recovery_active = true;
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
                .dictionary_name = compression.dictionary,
                .dictionary_id = compression_dictionary == nullptr
                                     ? 0U
                                     : compression_dictionary->dictionary.identity().dictionary_id,
                .group_id = encoded.update.sequence,
                .representation_bytes = measurement.encoded_update_bytes,
                .bytes_before_packetization = measurement.encoded_update_bytes,
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
            auto corpus_captured = true;
            if (compression_corpus.enabled())
            {
                auto const corpus_capture_start = simnet::steady_now_ns();
                corpus_captured =
                    compression_corpus
                        .capture(peer->peer, pipeline, extraction.entity_count, encoded);
                evidence_io_time += simnet::steady_now_ns() - corpus_capture_start;
            }
            if (!corpus_captured)
            {
                measurement.outcome = simnet::ServerReplicationOutcome::Abandoned;
                measurement.outcome_detail = "corpus_capture_failed";
                measurement.total_replication_elapsed_time = finish_total_time();
                observe_encoded_measurement();
                throw std::runtime_error(
                    "compression corpus capture failed: " + std::string{compression_corpus.error()}
                );
            }
            auto group_bytes = std::move(encoded.update.bytes);
            if (compression.mode == simnet::app::CompressionMode::WholeUpdate)
            {
                auto const limits = simnet::CompressionLimits{
                    .max_uncompressed_bytes = packetization.max_group_bytes,
                    .max_output_bytes = packetization.max_group_bytes,
                };
                auto const compressed = compression_dictionary == nullptr
                                            ? simnet::compress_bytes(
                                                  peer->compressor,
                                                  group_bytes,
                                                  compression.level,
                                                  limits,
                                                  simnet::CompressionEnvelopePolicy::Always,
                                                  peer->compression_scratch
                                              )
                                            : simnet::compress_bytes_with_dictionary(
                                                  peer->compressor,
                                                  compression_dictionary->dictionary,
                                                  group_bytes,
                                                  limits,
                                                  peer->compression_scratch
                                              );
                compression_report.compression_input_bytes = compressed.input_bytes;
                compression_report.compression_payload_bytes = compressed.encoded_payload_bytes;
                compression_report.compression_envelope_bytes = compressed.envelope_bytes;
                compression_report.compression_output_bytes = compressed.output_bytes;
                compression_report.compression_cpu_time = compressed.compression_cpu_time;
                compression_report.whole_encoding = compressed.encoding;
                compression_report.bytes_before_packetization = compressed.output_bytes;
                if (!compressed.valid)
                {
                    compression_report.error = compressed.error;
                    peer->latest_compression = compression_report;
                    peer->latest_submission_outcome = PacketSubmissionOutcome::Abandoned;
                    peer->latest_submission_error = compressed.error;
                    flatten_transport_reports(
                        measurement,
                        compression_report,
                        {},
                        peer->latest_attempted_submissions,
                        peer->latest_accepted_submissions
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
                peer->whole_update_compression_cpu_time += compressed.compression_cpu_time;
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
                    preparation,
                    peer->latest_attempted_submissions,
                    peer->latest_accepted_submissions
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
            if (compression.mode == simnet::app::CompressionMode::None)
            {
                compression_report.final_transport_bytes = preparation.total_packet_bytes;
                compression_report.valid = true;
            }
            else if (compression.mode == simnet::app::CompressionMode::WholeUpdate)
            {
                compression_report.final_transport_bytes = preparation.total_packet_bytes;
                compression_report.valid = true;
            }
            else if (!prepare_per_packet_transport_group(
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
                    preparation,
                    peer->latest_attempted_submissions,
                    peer->latest_accepted_submissions
                );
                measurement.outcome = simnet::ServerReplicationOutcome::Abandoned;
                measurement.outcome_detail = "per_packet_compression_failed";
                measurement.total_replication_elapsed_time = finish_total_time();
                observe_encoded_measurement();
                simnet::log(
                    simnet::LogCategory::Pipeline,
                    simnet::LogLevel::Error,
                    "snapshot per-packet compression preparation failed: " +
                        compression_report.error
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
            measurement.application_payload_bytes = compression_report.final_transport_bytes;
            flatten_transport_reports(
                measurement,
                compression_report,
                preparation,
                peer->latest_attempted_submissions,
                peer->latest_accepted_submissions
            );
            auto accepted_packet_bytes = std::uint32_t{};
            auto const send_start = simnet::steady_now_ns();
            {
                SIMNET_TRACE_SCOPE_CATEGORY("server.snapshot_send", simnet::LogCategory::Transport);
                for (auto index = std::uint16_t{}; index < prepared.chunk_count; ++index)
                {
                    auto packet_bytes = simnet::ByteSpan{};
                    if (compression.mode == simnet::app::CompressionMode::PerPacket)
                    {
                        auto const& payload = peer->prepared_transport_group.payloads[index];
                        packet_bytes = simnet::ByteSpan{peer->prepared_transport_group.bytes}
                                           .subspan(payload.offset, payload.size);
                    }
                    else
                    {
                        packet_bytes = simnet::serialize_group_chunk(
                            packetization,
                            prepared,
                            index,
                            peer->packet_serialization_scratch
                        );
                    }
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
                        measurement.transport_send_elapsed_time =
                            simnet::steady_now_ns() - send_start;
                        measurement.transport_payload_bytes = accepted_packet_bytes;
                        measurement.attempted_submissions = peer->latest_attempted_submissions;
                        measurement.accepted_submissions = peer->latest_accepted_submissions;
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
            measurement.transport_send_elapsed_time = simnet::steady_now_ns() - send_start;
            measurement.transport_payload_bytes = accepted_packet_bytes;
            measurement.attempted_submissions = peer->latest_attempted_submissions;
            measurement.accepted_submissions = peer->latest_accepted_submissions;

            auto const retention_start = simnet::steady_now_ns();
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
            measurement.latest_submitted_sequence =
                peer->snapshot_delivery.latest_submitted_sequence;
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
            measurement.snapshot_retention_elapsed_time = simnet::steady_now_ns() - retention_start;
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

    void disconnect_before_stop(simnet::TransportServer& transport, PeerRuntimeStates const& peers)
    {
        if (peers.empty())
        {
            return;
        }

        for (auto const& peer : peers)
        {
            transport.disconnect(peer.peer, simnet::DisconnectCode::None);
        }
        auto events = std::vector<simnet::TransportEvent>{};
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        auto remaining = peers.size();
        while (std::chrono::steady_clock::now() < deadline)
        {
            events.clear();
            auto const result = transport.poll(events, 5);
            if (!result.ok)
            {
                return;
            }
            auto const disconnected_count = static_cast<std::size_t>(std::ranges::count_if(
                events,
                [](simnet::TransportEvent const& event)
                {
                    return std::holds_alternative<simnet::PeerDisconnected>(event);
                }
            ));
            if (disconnected_count >= remaining)
            {
                return;
            }
            remaining -= disconnected_count;
        }
    }
}

namespace simnet::app
{
    int run_server(int argc, char** argv)
    {
        auto replication_csv = std::optional<ServerReplicationCsvWriter>{};
        auto boid_csv = std::optional<ServerBoidCsvWriter>{};
        auto compression_corpus = std::optional<CompressionCorpusWriter>{};
        try
        {
            auto const options = parse_options(argc, argv);
            auto const run_context = make_evidence_run_context(
                EvidenceProcessRole::Server,
                options.run_id.has_value() ? std::optional<std::string_view>{*options.run_id}
                                           : std::nullopt
            );
            auto const shared_config_source =
                options.shared_config_path.value_or(default_shared_config_path());
            auto const local_config_source =
                options.config_path.value_or(default_server_config_path());
            auto const shared = load_shared_config(shared_config_source);
            auto const local = load_server_config(local_config_source);
            auto const synthetic_enabled = shared.synthetic.has_value();
#if !defined(SIMNET_ENABLE_SYNTHETIC)
            if (synthetic_enabled)
            {
                throw std::runtime_error(
                    "shared config enables synthetic workload, but Server was built with "
                    "SIMNET_ENABLE_SYNTHETIC=OFF; reconfigure with "
                    "-DSIMNET_ENABLE_SYNTHETIC=ON"
                );
            }
#endif
            if (synthetic_enabled && local.visualization.enabled)
            {
                throw std::runtime_error(
                    "synthetic workload requires Server visualization.enabled=false"
                );
            }
            auto telemetry = TelemetryLifetime{local.telemetry};
#if defined(SIMNET_ENABLE_TRACY)
            log(LogCategory::Telemetry, LogLevel::Info, "Tracy instrumentation compiled in");
#else
            log(LogCategory::Telemetry, LogLevel::Info, "Tracy instrumentation not compiled in");
#endif
            auto signals = SignalHandlers{};
            auto const pipeline = make_snapshot_pipeline(shared);
#if defined(SIMNET_ENABLE_RENDER)
            auto const collect_representation_quality =
                local.telemetry.metrics_csv_enabled || local.visualization.enabled;
#else
            auto const collect_representation_quality = local.telemetry.metrics_csv_enabled;
#endif
            auto const compression = make_compression_settings(shared);
            if (options.compression_corpus_directory.has_value() &&
                compression.mode != CompressionMode::WholeUpdate)
            {
                throw std::runtime_error(
                    "--compression-corpus-dir requires whole_update compression"
                );
            }
            compression_corpus.emplace(
                CompressionCorpusWriterConfig{
                    .output_directory = options.compression_corpus_directory,
                    .run = run_context,
                    .seed = shared.run.seed,
                }
            );
            if (compression_corpus->enabled())
            {
                log(LogCategory::Pipeline,
                    LogLevel::Info,
                    "compression corpus manifest path=" +
                        compression_corpus->manifest_path().string());
            }
            auto compression_dictionary = load_compression_dictionary(compression);
            if (compression_dictionary.has_value())
            {
                auto const& identity = compression_dictionary->dictionary.identity();
                log(LogCategory::Pipeline,
                    LogLevel::Info,
                    "compression dictionary name=" + std::string{compression_dictionary->name} +
                        " id=" + std::to_string(identity.dictionary_id) +
                        " bytes=" + std::to_string(identity.byte_count) +
                        " fingerprint=" + std::to_string(identity.content_fingerprint));
            }
            auto const packetization = make_packetization_settings(shared);
            if (packetization.max_payload_bytes > local.transport.max_payload_bytes ||
                (packetization.enabled && local.transport.send_size_policy != "enforce_limit"))
            {
                throw std::runtime_error(
                    "packetization payload limit must fit the hard transport payload limit"
                );
            }
            auto const session_identity = make_session_identity(
                shared,
                pipeline,
                compression_dictionary.has_value() ? &compression_dictionary->dictionary.identity()
                                                   : nullptr
            );
            auto const evidence_identity = ServerEvidenceIdentity{
                .runtime_config_fingerprint = fingerprint_runtime_config(shared, local).value,
                .network_compatibility_fingerprint = session_identity.compatibility_fingerprint,
                .application_wire_fingerprint = session_identity.application_wire_fingerprint,
                .compression_dictionary_fingerprint =
                    compression_dictionary.has_value()
                        ? compression_dictionary->dictionary.identity().content_fingerprint
                        : 0U,
            };
#if defined(SIMNET_ENABLE_RENDER)
            auto const run_setup = RunSetupStorage{
                shared,
                local,
                pipeline,
                shared_config_source,
                local_config_source,
            };
#endif

            auto transport = TransportServer{};
            auto const started = transport.start({
                .bind_address = local.transport.host,
                .port = local.transport.port,
                .max_peers = local.transport.max_clients,
                .expected_identity = session_identity,
                .limits = {
                    .max_payload_bytes = local.transport.max_payload_bytes,
                    .size_policy = transport_send_size_policy(local.transport),
                },
            });
            if (!started.ok)
            {
                log(LogCategory::Transport,
                    LogLevel::Error,
                    "server transport start failed: " + started.error.message);
                return 1;
            }
            replication_csv.emplace(
                ReplicationCsvWriterConfig{
                    .enabled = local.telemetry.metrics_csv_enabled,
                    .output_directory = local.telemetry.log_directory,
                    .run = run_context,
                }
            );
            if (replication_csv->enabled())
            {
                log(LogCategory::Telemetry,
                    LogLevel::Info,
                    "server replication CSV path=" + replication_csv->path().string());
            }

            auto const settings = RuntimeSettings{
                .fixed_step =
                    {
                        .tick_rate_hz = shared.simulation.tick_rate_hz,
                        .max_steps_per_frame = options.max_steps_per_frame,
                    },
                .max_frame_time = options.max_frame_time,
                .max_frames = options.max_frames,
                .max_ticks = options.max_ticks,
                .max_runtime = options.max_runtime,
            };
            auto clock = make_clock(settings.fixed_step);
            if (clock.fixed_dt <= Nanoseconds{} || settings.fixed_step.max_steps_per_frame == 0)
            {
                throw std::runtime_error("invalid fixed-step runtime settings");
            }

#if defined(SIMNET_ENABLE_RENDER)
            auto viewer = std::optional<Viewer>{};
            if (local.visualization.enabled)
            {
                viewer.emplace(viewer_config(local.visualization), local.telemetry.log_directory);
                static_cast<void>(viewer->draw({
                    .info = {
                        .world_bounds = make_centered_bounds(shared.simulation.world_half),
                        .fixed_tick_rate_hz = shared.simulation.tick_rate_hz,
                        .status_message = "Initializing authoritative world",
                    },
                }));
            }
#endif

            auto game = std::optional<ServerGameRuntime>{};
            auto world = std::optional<flecs::world>{};
            auto current_snapshot = std::optional<CurrentSnapshotState>{};
#if defined(SIMNET_ENABLE_SYNTHETIC)
            auto synthetic_state = std::optional<SyntheticSnapshotState>{};
            auto synthetic_snapshots = std::optional<SyntheticSnapshotSettings>{};
            auto synthetic_changes = std::optional<SyntheticChangeSettings>{};
#endif
            if (synthetic_enabled)
            {
#if defined(SIMNET_ENABLE_SYNTHETIC)
                synthetic_state.emplace();
                synthetic_snapshots.emplace(synthetic_snapshot_settings(shared));
                synthetic_changes.emplace(synthetic_change_settings(shared));
                log(LogCategory::Simulation,
                    LogLevel::Info,
                    "synthetic authoritative producer configured entities=" +
                        std::to_string(shared.simulation.initial_boid_count) +
                        " pattern=" + shared.synthetic->pattern + " entity_change_fraction=" +
                        std::to_string(shared.synthetic->entity_change_fraction) +
                        " field_change_mode=" + shared.synthetic->field_change_mode);
#endif
            }
            else
            {
                game.emplace(boid_settings(shared), player_settings(shared));
                world.emplace();
                current_snapshot.emplace();
                register_server_game(*world, *game);
                auto const initialization_start = std::chrono::steady_clock::now();
                log(LogCategory::Simulation,
                    LogLevel::Info,
                    "initializing authoritative world entities=" +
                        std::to_string(shared.simulation.initial_boid_count));
                auto const population = initialize_world(*world, shared);
                if (!population.success())
                {
                    throw std::runtime_error(
                        "authoritative world initialization failed: " +
                        std::string{authoritative_spawn_error_name(population.error)}
                    );
                }
                auto const initialization_elapsed = std::chrono::duration_cast<Nanoseconds>(
                    std::chrono::steady_clock::now() - initialization_start
                );
                log(LogCategory::Simulation,
                    LogLevel::Info,
                    "authoritative world initialized elapsed_ns=" +
                        std::to_string(initialization_elapsed.count()));
                if (local.flecs.thread_count > 1U)
                {
                    world->set_threads(static_cast<std::int32_t>(local.flecs.thread_count));
                }
                log(LogCategory::Simulation,
                    LogLevel::Info,
                    "Flecs scheduler threads=" + std::to_string(local.flecs.thread_count));
                SIMNET_TRACE_PLOT(
                    "server.flecs.thread_count",
                    static_cast<double>(local.flecs.thread_count)
                );
                boid_csv.emplace(
                    ServerBoidCsvWriterConfig{
                        .enabled = local.telemetry.metrics_csv_enabled,
                        .output_directory = local.telemetry.log_directory,
                        .run = run_context,
                        .tick_rate_hz = shared.simulation.tick_rate_hz,
                        .worker_count = local.flecs.thread_count,
                    }
                );
                if (boid_csv->enabled())
                {
                    log(LogCategory::Telemetry,
                        LogLevel::Info,
                        "Server boid CSV path=" + boid_csv->path().string());
                }
            }

            auto stats = RuntimeStats{};
            auto timer = RuntimeFrameTimer{};
            reset_frame_timer(timer);
            auto stop = StopRequest{};
            auto peers = PeerRuntimeStates{};
            peers.reserve(local.transport.max_clients);
            auto events = std::vector<TransportEvent>{};
            auto replication_measurements = ServerReplicationMeasurements{};
            auto const delivery = snapshot_transport_delivery(shared.snapshot_delivery);
            auto const area_of_interest_grid_settings = make_spatial_grid_settings(
                make_centered_bounds(shared.simulation.world_half),
                shared.spatial.cell_size
            );
            auto simulation_paused = false;
            auto area_of_interest_grid_state = AreaOfInterestGridState{};
#if defined(SIMNET_ENABLE_RENDER)
            auto spatial_render = SpatialRenderStorage{};
            auto selected_debug_render = SelectedDebugRenderStorage{};
            auto presentation = PresentationSnapshotState{};
            auto spatial_snapshot_tick = std::optional<Tick>{};
            auto selected_entity = std::optional<EntityNetId>{};
            if (viewer.has_value())
            {
                // Viewer startup is not simulation time and must not create an initial catch-up frame.
                reset_frame_timer(timer);
            }
#endif

            log(LogCategory::Simulation,
                LogLevel::Info,
                "server runtime started entities=" +
                    std::to_string(shared.simulation.initial_boid_count));

            while (!stop.requested())
            {
                if (signal_stop_requested())
                {
                    static_cast<void>(stop.request(ShutdownReason::Signal));
                    break;
                }
                auto pause_state_changed = false;
                auto transport_ok = false;
                {
                    SIMNET_TRACE_SCOPE_CATEGORY("server.transport_poll", LogCategory::Transport);
                    transport_ok = poll_transport(
                        world.has_value() ? &*world : nullptr,
                        transport,
                        peers,
                        local.transport.max_clients,
                        events,
                        1,
                        delivery,
                        simulation_paused,
                        pause_state_changed,
                        current_snapshot.has_value() ? &*current_snapshot : nullptr
                    );
                }
                if (!transport_ok)
                {
                    static_cast<void>(stop.request(ShutdownReason::FatalError));
                    break;
                }
                if (pause_state_changed)
                {
                    broadcast_pause_state(
                        world.has_value() ? &*world : nullptr,
                        transport,
                        peers,
                        simulation_paused,
                        current_snapshot.has_value() ? &*current_snapshot : nullptr,
                        delivery
                    );
                    clock.accumulator = Nanoseconds{};
                }

                auto const frame_delta = sample_frame_delta(timer);
                auto frame = RuntimeFramePlan{};
                if (simulation_paused)
                {
                    ++stats.frames;
                    stats.raw_time += frame_delta;
                    stats.accepted_time += frame_delta;
                    clock.accumulator = Nanoseconds{};
                }
                else
                {
                    frame = plan_runtime_frame(clock, stats, frame_delta, settings);
                }
                for (std::uint16_t offset = 0; offset < frame.step_count; ++offset)
                {
                    auto const tick = frame.first_tick + offset;
                    if (!run_tick(
                            world.has_value() ? &*world : nullptr,
                            game.has_value() ? &*game : nullptr,
#if defined(SIMNET_ENABLE_SYNTHETIC)
                            synthetic_state.has_value() ? &*synthetic_state : nullptr,
                            synthetic_snapshots.has_value() ? &*synthetic_snapshots : nullptr,
                            synthetic_changes.has_value() ? &*synthetic_changes : nullptr,
#endif
                            tick,
                            clock.fixed_dt,
                            pipeline,
                            collect_representation_quality,
                            compression,
                            packetization,
                            shared.snapshot_delivery,
                            area_of_interest_grid_settings,
                            delivery,
                            evidence_identity,
                            transport,
                            peers,
                            current_snapshot.has_value() ? &*current_snapshot : nullptr,
                            area_of_interest_grid_state,
                            replication_measurements,
                            *replication_csv,
                            *compression_corpus,
                            compression_dictionary.has_value() ? &*compression_dictionary : nullptr
                        ))
                    {
                        static_cast<void>(stop.request(ShutdownReason::FatalError));
                        break;
                    }
#if defined(SIMNET_ENABLE_RENDER)
                    if (viewer.has_value() && local.visualization.interpolation_enabled)
                    {
                        auto const extracted =
                            ensure_current_snapshot(*world, tick, *current_snapshot);
                        if (!extracted.valid)
                        {
                            log(LogCategory::Simulation,
                                LogLevel::Error,
                                "presentation snapshot extraction failed: " + extracted.error);
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                            break;
                        }
                        retain_presentation_snapshot(presentation, current_snapshot->snapshot);
                    }
#endif
                    if (boid_csv.has_value() && !boid_csv->sample(tick, game->last_step_report()))
                    {
                        throw std::runtime_error(
                            "Server boid CSV submission failed: " + std::string{boid_csv->error()}
                        );
                    }
                }

                if (replication_csv->needs_drain() && !replication_csv->drain())
                {
                    throw std::runtime_error(
                        "server replication CSV drain failed: " +
                        std::string{replication_csv->error()}
                    );
                }
                if (boid_csv.has_value() && boid_csv->needs_drain() && !boid_csv->drain())
                {
                    throw std::runtime_error(
                        "Server boid CSV drain failed: " + std::string{boid_csv->error()}
                    );
                }

                if (frame.step_limit_reached && log_enabled(LogLevel::Warn))
                {
                    log(LogCategory::Core,
                        LogLevel::Warn,
                        "server dropped simulation time ns=" +
                            std::to_string(frame.dropped_time.count()));
                }
#if defined(SIMNET_ENABLE_RENDER)
                if (viewer.has_value())
                {
                    auto const extracted =
                        ensure_current_snapshot(*world, stats.ticks, *current_snapshot);
                    if (!extracted.valid)
                    {
                        log(LogCategory::Simulation,
                            LogLevel::Error,
                            "render snapshot extraction failed: " + extracted.error);
                        static_cast<void>(stop.request(ShutdownReason::FatalError));
                    }
                    else if (
                        !spatial_snapshot_tick.has_value() ||
                        *spatial_snapshot_tick != current_snapshot->extracted_tick
                    )
                    {
                        spatial_snapshot_tick = current_snapshot->extracted_tick;
                        rebuild_spatial_render_view(
                            spatial_render,
                            current_snapshot->snapshot,
                            shared,
                            Vec3f{},
                            local.visualization.max_visible_spatial_cells
                        );
                    }
                    if (!stop.requested() && current_snapshot->valid)
                    {
                        if (local.visualization.interpolation_enabled)
                        {
                            retain_presentation_snapshot(presentation, current_snapshot->snapshot);
                        }
                        auto const* displayed_snapshot =
                            local.visualization.interpolation_enabled
                                ? presentation_snapshot(
                                      presentation,
                                      local.visualization.interpolation_enabled,
                                      simulation_paused,
                                      frame.interpolation_alpha
                                  )
                                : &current_snapshot->snapshot;
                        if (displayed_snapshot == nullptr)
                        {
                            log(LogCategory::Render,
                                LogLevel::Error,
                                "server presentation interpolation failed");
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                            continue;
                        }
                        auto const interpolation_active =
                            local.visualization.interpolation_enabled && !simulation_paused &&
                            presentation.has_previous;
                        auto const interpolation = RenderInterpolationInfo{
                            .enabled = local.visualization.interpolation_enabled,
                            .interpolating = interpolation_active,
                            .from_tick = presentation.has_previous
                                             ? presentation.previous.tick
                                             : current_snapshot->snapshot.tick,
                            .to_tick = current_snapshot->snapshot.tick,
                            .alpha = interpolation_active ? frame.interpolation_alpha : 1.0,
                        };
                        SIMNET_TRACE_PLOT("server.render.interpolation_alpha", interpolation.alpha);
                        auto viewer_result = ViewerResult{};
                        {
                            SIMNET_TRACE_SCOPE_CATEGORY("server.viewer_draw", LogCategory::Render);
                            viewer_result = viewer->draw(render_frame(
                                *displayed_snapshot,
                                shared,
                                frame_delta,
                                simulation_paused,
                                interpolation,
                                peers,
                                local.transport.max_clients,
                                spatial_render,
                                selected_debug_render,
                                game->selected_boid_debug(),
                                run_setup.view()
                            ));
                        }
                        selected_entity = viewer_result.selected_entity;
                        game->select_boid(selected_entity);
                        if (viewer_result.close_requested)
                        {
                            static_cast<void>(stop.request(ShutdownReason::WindowClosed));
                        }
                        if (viewer_result.toggle_simulation_pause_requested)
                        {
                            simulation_paused = !simulation_paused;
                            clock.accumulator = Nanoseconds{};
                            log(LogCategory::Simulation,
                                LogLevel::Info,
                                simulation_paused ? "server simulation paused by viewer"
                                                  : "server simulation resumed by viewer");
                            broadcast_pause_state(
                                world.has_value() ? &*world : nullptr,
                                transport,
                                peers,
                                simulation_paused,
                                current_snapshot.has_value() ? &*current_snapshot : nullptr,
                                delivery
                            );
                        }
                    }
                }
#endif
                auto const limit = reached_runtime_limit(settings, stats);
                if (limit != ShutdownReason::None)
                {
                    static_cast<void>(stop.request(limit));
                }
                SIMNET_TRACE_PLOT("server.runtime.steps", static_cast<double>(frame.step_count));
                SIMNET_TRACE_PLOT(
                    "server.runtime.entities",
                    static_cast<double>(shared.simulation.initial_boid_count)
                );
                SIMNET_TRACE_FRAME("server");
            }

            if (stats.ticks != 0U && boid_csv.has_value())
            {
                if (!boid_csv->sample(stats.ticks, game->last_step_report(), true))
                {
                    throw std::runtime_error(
                        "Server boid CSV final submission failed: " + std::string{boid_csv->error()}
                    );
                }
            }
            for (auto const& peer : peers)
            {
                log_snapshot_delivery_state(peer, shared.snapshot_delivery.mode);
            }
            disconnect_before_stop(transport, peers);
            transport.stop();
            if (!compression_corpus->close())
            {
                throw std::runtime_error(
                    "compression corpus close failed: " + std::string{compression_corpus->error()}
                );
            }
            if (!replication_csv->close())
            {
                throw std::runtime_error(
                    "server replication CSV close failed: " + std::string{replication_csv->error()}
                );
            }
            if (boid_csv.has_value() && !boid_csv->close())
            {
                throw std::runtime_error(
                    "Server boid CSV close failed: " + std::string{boid_csv->error()}
                );
            }
            log_server_replication_measurements(replication_measurements);
            log(LogCategory::Simulation,
                LogLevel::Info,
                "server runtime stopped reason=" +
                    std::string{shutdown_reason_name(stop.reason())} + " frames=" +
                    std::to_string(stats.frames) + " ticks=" + std::to_string(stats.ticks) +
                    " dropped_ns=" + std::to_string(stats.dropped_time.count()));
            telemetry.shutdown();
            return stop.reason() == ShutdownReason::FatalError ? 1 : 0;
        }
        catch (std::exception const& error)
        {
            auto close_error = std::string{};
            if (compression_corpus.has_value() && !compression_corpus->close())
            {
                close_error = std::string{compression_corpus->error()};
            }
            if (replication_csv.has_value() && !replication_csv->close())
            {
                if (!close_error.empty())
                {
                    close_error += ". ";
                }
                close_error += std::string{replication_csv->error()};
            }
            if (boid_csv.has_value() && !boid_csv->close())
            {
                if (!close_error.empty())
                {
                    close_error += ". ";
                }
                close_error += std::string{boid_csv->error()};
            }
            std::cerr << "Server failed: " << error.what();
            if (!close_error.empty())
            {
                std::cerr << ". Evidence close failed: " << close_error;
            }
            std::cerr << '\n';
            return 1;
        }
    }
}
