module;

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

module simnet.server_runtime;

import :replication;

import simnet.config;
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

using simnet::app::server_replication::AreaOfInterestGridState;
using simnet::app::server_replication::CurrentSnapshotState;
using simnet::app::server_replication::PacketSubmissionOutcome;
using simnet::app::server_replication::PeerRuntimeState;
using simnet::app::server_replication::PeerRuntimeStates;
using simnet::app::server_replication::ServerCompressionReport;
using simnet::app::server_replication::ServerEvidenceIdentity;
using simnet::app::server_replication::apply_ack;
using simnet::app::server_replication::area_of_interest_mode_name;
using simnet::app::server_replication::ensure_current_snapshot;
using simnet::app::server_replication::entity_record_layout_name;
using simnet::app::server_replication::erase_peer_state;
using simnet::app::server_replication::find_peer;
using simnet::app::server_replication::level_of_detail_mode_name;
using simnet::app::server_replication::log_snapshot_delivery_state;
using simnet::app::server_replication::peer_role_name;
using simnet::app::server_replication::run_tick;
using simnet::app::server_replication::valid_ack;

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
        std::optional<std::string> run_id{};
        std::uint64_t max_frames{};
        simnet::Tick max_ticks{};
        simnet::Nanoseconds max_runtime{};
        simnet::Nanoseconds max_frame_time{std::chrono::milliseconds(250)};
        std::uint16_t max_steps_per_frame{5};
    };


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


#if defined(SIMNET_ENABLE_RENDER)
    [[nodiscard]] constexpr std::string_view
    compression_encoding_name(simnet::CompressionEncoding encoding) noexcept
    {
        switch (encoding)
        {
            case simnet::CompressionEncoding::Raw:
                return "Raw";
            case simnet::CompressionEncoding::Zstd:
                return "Zstd";
        }
        return "Unknown";
    }
#endif


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
            details.snapshot_recovery_reason =
                simnet::app::snapshot_recovery_reason_name(peer->snapshot_delivery.recovery_reason);
            details.forced_full_replace_count = peer->snapshot_delivery.forced_full_replace_count;
            details.recovery_request_count = peer->snapshot_delivery.recovery_request_count;
            details.area_of_interest_mode = config.pipeline.area_of_interest.mode;
            details.level_of_detail_mode = config.pipeline.level_of_detail.mode;
            details.send_interval_ticks = config.pipeline.send_interval_ticks;
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
                details.lod_near_represented = lod.represented.near;
                details.lod_medium_represented = lod.represented.medium;
                details.lod_far_represented = lod.represented.far;
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
                details.encoded_group_bytes = peer->latest_packetization.group_bytes;
                details.packet_chunk_count = peer->latest_packetization.chunk_count;
                details.application_packet_bytes = peer->latest_packetization.total_packet_bytes;
            }
            auto const& compression = peer->latest_compression;
            details.compression_mode = simnet::app::compression_mode_name(compression.mode);
            details.compression_dictionary = "none";
            if (compression.group_id != 0U)
            {
                details.representation_bytes = compression.representation_bytes;
                details.compression_output_bytes = compression.compression_output_bytes;
                details.final_transport_bytes = compression.final_transport_bytes;
                details.compression_ratio = compression.ratio;
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
            if (!peer->snapshot_delivery.submitted.empty())
            {
                details.latest_snapshot_tick =
                    peer->snapshot_delivery.submitted.back().snapshot.tick;
            }
            replication = details;
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
                    .replication = replication,
                },
            .selected_details = selected_details,
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
        simnet::EntityNetId const id = index + 1U;
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
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Warn,
                        "server rejected session at configured capacity peer=" +
                            std::to_string(ready->peer)
                    );
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
                        if (message.role == simnet::app::ClientRole::Player)
                        {
                            auto const player_already_connected = std::ranges::any_of(
                                peers,
                                [peer](PeerRuntimeState const& candidate)
                                {
                                    return candidate.peer != peer->peer &&
                                           candidate.role == simnet::app::ClientRole::Player;
                                }
                            );
                            if (player_already_connected)
                            {
                                simnet::log(
                                    simnet::LogCategory::Simulation,
                                    simnet::LogLevel::Warn,
                                    "server rejected additional Player peer_id=" +
                                        std::to_string(peer->peer)
                                );
                                reject_peer(simnet::DisconnectCode::ServerFull);
                                continue;
                            }
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
                            peer->role = message.role;
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
                        else
                        {
                            peer->role = message.role;
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
                    simnet::LogLevel::Warn,
                    "server transport error: " + error->message
                );
                // TransportErrorEvent is a non-fatal diagnostic. Fatal backend failures are
                // returned by poll() itself; a malformed or incompatible peer must not stop the
                // authoritative Server.
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
            auto const packetization = make_packetization_settings(shared);
            if (packetization.max_payload_bytes > local.transport.max_payload_bytes ||
                (packetization.enabled && local.transport.send_size_policy != "enforce_limit"))
            {
                throw std::runtime_error(
                    "packetization payload limit must fit the hard transport payload limit"
                );
            }
            auto const session_identity = make_session_identity(shared, pipeline);
            auto const evidence_identity = ServerEvidenceIdentity{
                .runtime_config_fingerprint = fingerprint_runtime_config(shared, local).value,
                .network_compatibility_fingerprint = session_identity.compatibility_fingerprint,
                .application_wire_fingerprint = session_identity.application_wire_fingerprint,
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
                // Keep one transport-level admission slot beyond the application capacity so a
                // newly connecting peer can receive an explicit ServerFull rejection.
                .max_peers = local.transport.max_clients + 1U,
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
                            *replication_csv
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
                }

                if (replication_csv->needs_drain() && !replication_csv->drain())
                {
                    throw std::runtime_error(
                        "server replication CSV drain failed: " +
                        std::string{replication_csv->error()}
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

            for (auto const& peer : peers)
            {
                log_snapshot_delivery_state(peer, shared.snapshot_delivery.mode);
            }
            disconnect_before_stop(transport, peers);
            transport.stop();
            if (!replication_csv->close())
            {
                throw std::runtime_error(
                    "server replication CSV close failed: " + std::string{replication_csv->error()}
                );
            }
            auto const final_canonical_count =
                replication_measurements.latest_sent.has_value()
                    ? replication_measurements.latest_sent->canonical_entity_count
                    : 0U;
            auto const final_canonical_fingerprint =
                replication_measurements.latest_sent.has_value()
                    ? replication_measurements.latest_sent->canonical_fingerprint
                    : 0U;
            log(LogCategory::Telemetry,
                LogLevel::Info,
                "server evidence summary run_id=" + run_context.run_id + " path=" +
                    (replication_csv->enabled() ? replication_csv->path().string()
                                                : std::string{"disabled"}) +
                    " shutdown_reason=" + std::string{shutdown_reason_name(stop.reason())} +
                    " final_tick=" + std::to_string(stats.ticks) + " writer_healthy=" +
                    std::string{replication_csv->healthy() ? "true" : "false"} +
                    " submitted_rows=" + std::to_string(replication_csv->submitted_count()) +
                    " attempts=" + std::to_string(replication_measurements.attempt_count) +
                    " sent=" + std::to_string(replication_measurements.sent_count) +
                    " final_canonical_count=" + std::to_string(final_canonical_count) +
                    " final_canonical_fingerprint=" +
                    std::to_string(final_canonical_fingerprint) +
                    " recovery_forced_upserts=" +
                    std::to_string(replication_measurements.recovery_forced_upsert_count) +
                    " recovery_forced_deletes=" +
                    std::to_string(replication_measurements.recovery_forced_delete_count) +
                    " repeated_without_ack_upserts=" +
                    std::to_string(replication_measurements.repeated_without_ack_upsert_count) +
                    " repeated_without_ack_deletes=" +
                    std::to_string(replication_measurements.repeated_without_ack_delete_count) +
                    " dropped_ns=" + std::to_string(stats.dropped_time.count()) +
                    " dropped_time_warning=" +
                    std::string{stats.dropped_time > Nanoseconds{} ? "true" : "false"});
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
            if (replication_csv.has_value() && !replication_csv->close())
            {
                if (!close_error.empty())
                {
                    close_error += ". ";
                }
                close_error += std::string{replication_csv->error()};
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
