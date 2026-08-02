#include "server_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <flecs.h>
#include <fstream>
#include <iomanip>
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

import simnet.config;
import simnet.app_common;
import simnet.app_protocol;
import simnet.core;
import simnet.game_server;
import simnet.game_shared;
import simnet.pipeline;
import simnet.runtime;
import simnet.snapshot;
import simnet.telemetry;
import simnet.transport;
#if defined(SIMNET_ENABLE_RENDER)
import simnet.app_visual_setup;
import simnet.render;
import simnet.spatial;
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
    };
#endif

    constexpr std::size_t retained_snapshot_limit = 64;

    struct ServerOptions
    {
        std::optional<std::filesystem::path> config_path{};
        std::optional<std::filesystem::path> shared_config_path{};
        std::uint64_t max_frames{};
        simnet::Tick max_ticks{};
        simnet::Nanoseconds max_runtime{};
        simnet::Nanoseconds max_frame_time{std::chrono::milliseconds(250)};
        std::uint16_t max_steps_per_frame{5};
    };

    class BoidCsvEvidence
    {
    public:
        BoidCsvEvidence(
            simnet::TelemetryConfig const& config,
            double tick_rate_hz,
            std::uint32_t worker_count
        )
            : interval_(
                  std::max<simnet::Tick>(1U, static_cast<simnet::Tick>(std::llround(tick_rate_hz)))
              )
            , worker_count_(worker_count)
        {
            if (!config.metrics_csv_enabled) {
                return;
            }
            std::filesystem::create_directories(config.log_directory);
            auto const stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch()
            )
                                   .count();
            path_ = std::filesystem::path{config.log_directory}
                / ("server_boids_" + std::to_string(stamp) + ".csv");
            stream_.open(path_, std::ios::out | std::ios::trunc);
            if (!stream_) {
                throw std::runtime_error("failed to create boid metrics CSV: " + path_.string());
            }
            stream_
                << "tick,entity_count,worker_count,occupied_cells,max_occupancy,average_load,"
                   "raw_candidates_mean,raw_candidates_max,retained_neighbors_mean,"
                   "retained_neighbors_max,cap_hit_count,separation_count_mean,social_count_mean,"
                   "isolated_count,nearest_neighbor_distance_mean,speed_mean,speed_min,speed_max,"
                   "acceleration_mean,acceleration_max,acceleration_saturation_count,"
                   "overlap_recovery_count,wall_guard_count,polarization,capture_ms,grid_ms,"
                   "compute_ms,validate_ms,commit_ms,progress_ms\n";
            simnet::log(
                simnet::LogCategory::Telemetry,
                simnet::LogLevel::Info,
                "boid CSV evidence path=" + path_.string()
            );
        }

        void
        sample(simnet::Tick tick, simnet::ServerGameStepReport const& report, bool force = false)
        {
            if (!stream_ || (!force && tick % interval_ != 0U) || last_tick_ == tick) {
                return;
            }
            auto const& value = report.diagnostics;
            auto const& phase = report.phases;
            stream_ << std::setprecision(9) << tick << ',' << report.entity_count << ','
                    << worker_count_ << ',' << value.grid.occupied_cell_count << ','
                    << value.grid.max_cell_occupancy << ',' << value.grid.average_occupied_cell_load
                    << ',' << value.raw_candidates_mean << ',' << value.raw_candidates_max << ','
                    << value.retained_neighbors_mean << ',' << value.retained_neighbors_max << ','
                    << value.neighbor_cap_hit_count << ',' << value.separation_neighbors_mean << ','
                    << value.social_neighbors_mean << ',' << value.isolated_boid_count << ','
                    << value.nearest_neighbor_distance_mean << ',' << value.speed_mean << ','
                    << value.speed_min << ',' << value.speed_max << ',' << value.acceleration_mean
                    << ',' << value.acceleration_max << ',' << value.acceleration_saturation_count
                    << ',' << value.overlap_recovery_count << ',' << value.hard_wall_guard_count
                    << ',' << value.polarization << ',' << phase.capture_ms << ',' << phase.grid_ms
                    << ',' << phase.compute_ms << ',' << phase.validate_ms << ',' << phase.commit_ms
                    << ',' << phase.progress_ms << '\n';
            stream_.flush();
            last_tick_ = tick;
        }

    private:
        std::ofstream stream_{};
        std::filesystem::path path_{};
        simnet::Tick interval_{1U};
        simnet::Tick last_tick_{std::numeric_limits<simnet::Tick>::max()};
        std::uint32_t worker_count_{};
    };

    struct RetainedSnapshot
    {
        simnet::SequenceId sequence{};
        simnet::WorldSnapshot snapshot{};
    };

    struct CurrentSnapshotState
    {
        simnet::WorldSnapshot snapshot{};
        simnet::Tick extracted_tick{};
        bool valid{};
        bool dirty{true};
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

    struct PeerRuntimeState
    {
        simnet::PeerId peer{};
        std::optional<simnet::app::ClientRole> role{};
        simnet::EntityNetId player_id{};
        simnet::ClientReplicationState pipeline_state{};
        simnet::SnapshotAck latest_ack{};
        simnet::SequenceId newest_emitted_sequence{};
        std::optional<simnet::SequenceId> acknowledged_baseline_sequence{};
        std::deque<RetainedSnapshot> retained_snapshots{};
    };

    [[nodiscard]] ServerOptions parse_options(int argc, char** argv)
    {
        auto options = ServerOptions{};
        for (auto index = 1; index < argc; ++index) {
            auto const option = std::string_view{argv[index]};
            if (option == "--config") {
                options.config_path = std::filesystem::path{
                    simnet::app::next_option_value(index, argc, argv, option)
                };
            } else if (option == "--shared-config") {
                options.shared_config_path = std::filesystem::path{
                    simnet::app::next_option_value(index, argc, argv, option)
                };
            } else if (option == "--max-frames") {
                options.max_frames = simnet::app::parse_unsigned<std::uint64_t>(
                    simnet::app::next_option_value(index, argc, argv, option),
                    option
                );
            } else if (option == "--max-ticks") {
                options.max_ticks = simnet::app::parse_unsigned<simnet::Tick>(
                    simnet::app::next_option_value(index, argc, argv, option),
                    option
                );
            } else if (option == "--max-runtime-ms") {
                options.max_runtime = simnet::app::milliseconds_option(index, argc, argv, option);
            } else if (option == "--max-frame-delta-ms") {
                options.max_frame_time
                    = simnet::app::milliseconds_option(index, argc, argv, option);
            } else if (option == "--max-steps-per-frame") {
                options.max_steps_per_frame = simnet::app::parse_unsigned<std::uint16_t>(
                    simnet::app::next_option_value(index, argc, argv, option),
                    option
                );
            } else {
                throw std::runtime_error("unknown server option: " + std::string{option});
            }
        }
        return options;
    }

    [[nodiscard]] simnet::Delivery snapshot_delivery(simnet::TransportConfig const& config)
    {
        if (config.snapshot_delivery == "reliable_sequenced") {
            return simnet::Delivery::ReliableSequenced;
        }
        if (config.snapshot_delivery == "unreliable_sequenced") {
            return simnet::Delivery::UnreliableSequenced;
        }
        if (config.snapshot_delivery == "unreliable_unsequenced") {
            return simnet::Delivery::UnreliableUnsequenced;
        }
        if (config.snapshot_delivery == "unreliable_fragmented") {
            return simnet::Delivery::UnreliableFragmented;
        }
        throw std::runtime_error("unsupported snapshot delivery: " + config.snapshot_delivery);
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
        };
    }

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
            .stationary_observer_vertical_fov_degrees
            = config.stationary_observer_vertical_fov_degrees,
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
        std::optional<PeerRuntimeState> const& peer,
        SpatialRenderStorage const& spatial,
        SelectedDebugRenderStorage& debug_storage,
        std::optional<simnet::SelectedBoidDebug> const& selected_debug,
        simnet::RunSetupView setup
    )
    {
        auto connection = simnet::RenderConnectionInfo{
            .state = peer.has_value() ? "1 client connected" : "No clients connected",
            .connected_peer_count = peer.has_value() ? 1U : 0U,
            .peer_capacity = 1U,
        };
        auto replication = std::optional<simnet::RenderReplicationInfo>{};
        if (peer.has_value()) {
            connection.peer = peer->peer;
            auto details = simnet::RenderReplicationInfo{};
            if (peer->newest_emitted_sequence != 0) {
                details.latest_emitted_sequence = peer->newest_emitted_sequence;
            }
            if (peer->latest_ack.newest_received_snapshot != 0) {
                details.latest_received_sequence = peer->latest_ack.newest_received_snapshot;
            }
            if (peer->latest_ack.newest_applied_snapshot != 0) {
                details.latest_applied_sequence = peer->latest_ack.newest_applied_snapshot;
            }
            details.acknowledged_baseline_sequence = peer->acknowledged_baseline_sequence;
            details.retained_snapshot_count
                = static_cast<std::uint32_t>(peer->retained_snapshots.size());
            if (!peer->retained_snapshots.empty()) {
                details.oldest_retained_sequence = peer->retained_snapshots.front().sequence;
                details.newest_retained_sequence = peer->retained_snapshots.back().sequence;
                details.latest_snapshot_tick = peer->retained_snapshots.back().snapshot.tick;
            }
            replication = std::move(details);
        }
        auto selected_details = std::optional<simnet::SelectedEntityDetails>{};
        debug_storage.spheres.clear();
        debug_storage.vectors.clear();
        debug_storage.boxes.clear();
        debug_storage.cones.clear();
        if (selected_debug.has_value()) {
            selected_details = simnet::SelectedEntityDetails {
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
                .current_cell = simnet::SelectedCellCoord {
                    .x = selected_debug->current_cell.x,
                    .y = selected_debug->current_cell.y,
                    .z = selected_debug->current_cell.z,
                },
                .queried_cell_count = static_cast<std::uint32_t>(
                    selected_debug->queried_cell_bounds.size()
                ),
                .displayed_queried_cell_count = static_cast<std::uint32_t>(
                    selected_debug->queried_cell_bounds.size()
                ),
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
            if (found != snapshot.ids.end() && *found == selected_debug->id) {
                auto const index
                    = static_cast<std::size_t>(std::distance(snapshot.ids.begin(), found));
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
                debug_storage.vectors = {
                    {position, selected_debug->separation, {230U, 94U, 94U, 255U}, "separation"},
                    {position, selected_debug->alignment, {92U, 174U, 235U, 255U}, "alignment"},
                    {position, selected_debug->cohesion, {124U, 214U, 156U, 255U}, "cohesion"},
                    {position, selected_debug->containment, {247U, 184U, 74U, 255U}, "containment"},
                    {position, selected_debug->wander, {198U, 126U, 255U, 255U}, "wander"},
                    {position,
                     selected_debug->acceleration,
                     {245U, 245U, 245U, 255U},
                     "acceleration"},
                };
                debug_storage.boxes.reserve(selected_debug->queried_cell_bounds.size());
                for (auto const bounds : selected_debug->queried_cell_bounds) {
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
        return {
            .entities = {
                .ids = snapshot.ids,
                .positions = snapshot.positions,
                .headings = snapshot.headings,
                .hues = snapshot.hues,
            },
            .info = {
                .tick = snapshot.tick,
                .world_bounds = simnet::make_centered_bounds(config.simulation.world_half),
                .frame_delta = frame_delta,
                .fixed_tick_rate_hz = config.simulation.tick_rate_hz,
                .simulation_paused = paused,
                .interpolation = interpolation,
                .context = {
                    .kind = simnet::ViewerKind::Server,
                },
                .capabilities = {
                    .can_pause_simulation = true,
                    .has_networking = true,
                    .has_entity_diagnostics = true,
                    .has_spatial_visualization = true,
                },
                .connection = connection,
                .replication = std::move(replication),
            },
            .selected_details = std::move(selected_details),
            .spatial = simnet::SpatialDebugView {
                .cells = spatial.displayed_cells,
                .occupied_cell_count = spatial.grid.stats.occupied_cell_count,
                .max_cell_occupancy = spatial.grid.stats.max_cell_occupancy,
                .average_occupied_cell_load = spatial.grid.stats.average_occupied_cell_load,
                .query_radius = std::max({
                    config.boids.separation_radius,
                    config.boids.alignment_radius,
                    config.boids.cohesion_radius,
                }),
                .display_capped = spatial.displayed_cells.size() < spatial.grid.occupied_cells.size(),
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
            if (storage.grid.settings.cell_size != settings.cell_size
                || storage.grid.settings.bounds.min.x != settings.bounds.min.x
                || storage.grid.settings.bounds.max.x != settings.bounds.max.x) {
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
            for (auto const& range : storage.grid.occupied_cells) {
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
                [](SpatialRenderCandidate const& lhs, SpatialRenderCandidate const& rhs) {
                    if (lhs.distance_squared == rhs.distance_squared) {
                        return lhs.key < rhs.key;
                    }
                    return lhs.distance_squared < rhs.distance_squared;
                }
            );
        }
        {
            SIMNET_TRACE_SCOPE_CATEGORY("spatial.display_view", simnet::LogCategory::Spatial);
            auto const display_count
                = std::min<std::size_t>(visible_cell_limit, storage.candidates.size());
            storage.displayed_cells.clear();
            storage.displayed_cells.reserve(display_count);
            for (std::size_t index = 0; index < display_count; ++index) {
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
        auto const coordinate = [&](std::uint32_t cell_index, std::uint64_t salt) {
            auto const jitter = (unit_hash(key ^ salt) - 0.5F) * 0.5F;
            return -config.simulation.world_half
                + (static_cast<float>(cell_index) + 0.5F + jitter) * cell;
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
            .position = {
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
            for (std::uint32_t index = 0; index < config.simulation.initial_boid_count; ++index) {
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
        if (!simnet::prepare_server_game_runtime(world, game)) {
            return false;
        }
        auto const seconds = std::chrono::duration<float>(fixed_dt).count();
        return world.progress(seconds) && game.last_step_report().valid;
    }

    [[nodiscard]] bool valid_ack(PeerRuntimeState const& peer, simnet::SnapshotAck const& ack)
    {
        return ack.newest_received_snapshot != 0
            && ack.newest_applied_snapshot <= ack.newest_received_snapshot
            && ack.newest_received_snapshot >= peer.latest_ack.newest_received_snapshot
            && ack.newest_applied_snapshot >= peer.latest_ack.newest_applied_snapshot
            && ack.newest_received_snapshot <= peer.newest_emitted_sequence;
    }

    [[nodiscard]] auto find_retained_snapshot(PeerRuntimeState& peer, simnet::SequenceId sequence)
    {
        return std::find_if(
            peer.retained_snapshots.begin(),
            peer.retained_snapshots.end(),
            [sequence](RetainedSnapshot const& snapshot) {
                return snapshot.sequence == sequence;
            }
        );
    }

    void invalidate_baseline(PeerRuntimeState& peer)
    {
        peer.acknowledged_baseline_sequence.reset();
        simnet::log(
            simnet::LogCategory::Pipeline,
            simnet::LogLevel::Warn,
            "acknowledged snapshot no longer retained, next update uses FullReplace"
        );
        simnet::log(
            simnet::LogCategory::Pipeline,
            simnet::LogLevel::Info,
            "delta unavailable; using FullReplace"
        );
    }

    void copy_snapshot_reusing_capacity(
        simnet::WorldSnapshot const& source,
        simnet::WorldSnapshot& destination
    )
    {
        destination.tick = source.tick;
        destination.ids.resize(source.ids.size());
        destination.positions.resize(source.positions.size());
        destination.headings.resize(source.headings.size());
        destination.hues.resize(source.hues.size());
        std::copy(source.ids.begin(), source.ids.end(), destination.ids.begin());
        std::copy(source.positions.begin(), source.positions.end(), destination.positions.begin());
        std::copy(source.headings.begin(), source.headings.end(), destination.headings.begin());
        std::copy(source.hues.begin(), source.hues.end(), destination.hues.begin());
    }

#if defined(SIMNET_ENABLE_RENDER)
    void retain_presentation_snapshot(
        PresentationSnapshotState& state,
        simnet::WorldSnapshot const& snapshot
    )
    {
        if (state.has_current && state.current.tick == snapshot.tick) {
            return;
        }
        if (state.has_current) {
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
        if (!state.has_current) {
            return nullptr;
        }
        if (!interpolation_enabled || paused || !state.has_previous) {
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

    void retain_snapshot(
        PeerRuntimeState& peer,
        simnet::SequenceId sequence,
        simnet::WorldSnapshot const& snapshot
    )
    {
        auto retained = RetainedSnapshot{};
        if (peer.retained_snapshots.size() >= retained_snapshot_limit) {
            retained = std::move(peer.retained_snapshots.front());
            peer.retained_snapshots.pop_front();
            if (peer.acknowledged_baseline_sequence == retained.sequence) {
                invalidate_baseline(peer);
            }
        }
        {
            SIMNET_TRACE_SCOPE_CATEGORY(
                "server.retained_snapshot_copy",
                simnet::LogCategory::Snapshot
            );
            copy_snapshot_reusing_capacity(snapshot, retained.snapshot);
        }
        retained.sequence = sequence;
        peer.retained_snapshots.push_back(std::move(retained));
        SIMNET_TRACE_PLOT(
            "server.retained_snapshot_count",
            static_cast<double>(peer.retained_snapshots.size())
        );
    }

    [[nodiscard]] simnet::ServerSnapshotExtractionReport ensure_current_snapshot(
        flecs::world const& world,
        simnet::Tick tick,
        CurrentSnapshotState& state
    )
    {
        if (state.valid && !state.dirty) {
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
        if (!report.valid) {
            state.valid = false;
            state.dirty = true;
            return report;
        }
        state.extracted_tick = tick;
        state.valid = true;
        state.dirty = false;
        return report;
    }

    void apply_ack(PeerRuntimeState& peer, simnet::SnapshotAck const& ack)
    {
        peer.latest_ack = ack;
        auto const retained = find_retained_snapshot(peer, ack.newest_applied_snapshot);
        if (retained != peer.retained_snapshots.end()) {
            auto const needs_promotion_log = !peer.acknowledged_baseline_sequence.has_value();
            peer.acknowledged_baseline_sequence = retained->sequence;
            peer.retained_snapshots.erase(peer.retained_snapshots.begin(), retained);
            if (needs_promotion_log) {
                simnet::log(
                    simnet::LogCategory::Pipeline,
                    simnet::LogLevel::Info,
                    "baseline promoted sequence=" + std::to_string(ack.newest_applied_snapshot)
                );
            }
        } else {
            invalidate_baseline(peer);
        }
    }

    [[nodiscard]] bool send_pause_state(
        simnet::TransportServer& transport,
        std::optional<PeerRuntimeState> const& peer,
        bool paused
    )
    {
        if (!peer.has_value()) {
            return true;
        }

        auto const bytes = simnet::app::encode_app_message({
            .kind = simnet::app::AppMessageKind::PauseState,
            .paused = paused,
        });
        auto const sent = transport.send_application_control(peer->peer, bytes);
        if (!sent.ok) {
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
            .player_id = peer.player_id,
        });
        auto const sent = transport.send_application_control(peer.peer, bytes);
        if (!sent.ok) {
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
        flecs::world& world,
        PeerRuntimeState const& peer,
        CurrentSnapshotState& snapshot_state
    )
    {
        if (peer.player_id != 0U && simnet::delete_authoritative_player(world, peer.player_id)) {
            snapshot_state.dirty = true;
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Info,
                "server deleted disconnected player_id=" + std::to_string(peer.player_id)
            );
        }
    }

    [[nodiscard]] bool poll_transport(
        flecs::world& world,
        simnet::TransportServer& transport,
        std::optional<PeerRuntimeState>& peer,
        std::vector<simnet::TransportEvent>& events,
        std::uint32_t timeout_ms,
        bool delta_enabled,
        bool& simulation_paused,
        bool& pause_state_changed,
        CurrentSnapshotState& snapshot_state
    )
    {
        events.clear();
        auto const result = transport.poll(events, timeout_ms);
        if (!result.ok) {
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Error,
                "server transport poll failed: " + result.error.message
            );
            return false;
        }

        for (auto const& event : events) {
            if (auto const* ready = std::get_if<simnet::PeerSessionReady>(&event)) {
                if (peer.has_value() && peer->peer != ready->peer) {
                    transport.disconnect(ready->peer, simnet::DisconnectCode::ServerFull);
                } else {
                    peer = PeerRuntimeState{.peer = ready->peer};
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Info,
                        "server session ready peer=" + std::to_string(ready->peer)
                    );
                }
            } else if (auto const* disconnected = std::get_if<simnet::PeerDisconnected>(&event)) {
                if (peer.has_value() && peer->peer == disconnected->peer) {
                    remove_session_player(world, *peer, snapshot_state);
                    peer.reset();
                }
            } else if (auto const* received = std::get_if<simnet::SnapshotAckReceived>(&event)) {
                if (peer.has_value() && received->peer == peer->peer
                    && valid_ack(*peer, received->ack)) {
                    if (delta_enabled) {
                        apply_ack(*peer, received->ack);
                    } else {
                        peer->latest_ack = received->ack;
                    }
                } else {
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Warn,
                        "server ignored invalid snapshot ACK"
                    );
                }
            } else if (
                auto const* control = std::get_if<simnet::ReceivedApplicationControl>(&event)
            ) {
                auto message = simnet::app::AppMessage{};
                if (!peer.has_value() || control->peer != peer->peer
                    || !simnet::app::decode_app_message(control->payload, message)) {
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Error,
                        "server received invalid application-control message"
                    );
                    transport.disconnect(control->peer, simnet::DisconnectCode::ProtocolMismatch);
                    if (peer.has_value() && peer->peer == control->peer) {
                        remove_session_player(world, *peer, snapshot_state);
                        peer.reset();
                    }
                    continue;
                }
                if (message.kind == simnet::app::AppMessageKind::JoinRequest
                    && !peer->role.has_value()) {
                    peer->role = message.role;
                    if (message.role == simnet::app::ClientRole::Player) {
                        peer->player_id = simnet::spawn_authoritative_player(world);
                        if (peer->player_id == 0U) {
                            simnet::log(
                                simnet::LogCategory::Simulation,
                                simnet::LogLevel::Error,
                                "server failed to create authoritative player"
                            );
                            transport.disconnect(control->peer, simnet::DisconnectCode::ServerFull);
                            peer.reset();
                            continue;
                        }
                        snapshot_state.dirty = true;
                    }
                    simnet::log(
                        simnet::LogCategory::Simulation,
                        simnet::LogLevel::Info,
                        "server accepted role="
                            + std::
                                string{message.role == simnet::app::ClientRole::Player ? "player" : "stationary_observer"}
                            + " player_id=" + std::to_string(peer->player_id)
                    );
                    if (!send_join_accepted(transport, *peer)
                        || !send_pause_state(transport, peer, simulation_paused)) {
                        return false;
                    }
                } else if (
                    message.kind == simnet::app::AppMessageKind::PauseSetRequest
                    && peer->role.has_value()
                ) {
                    pause_state_changed
                        = pause_state_changed || simulation_paused != message.paused;
                    simulation_paused = message.paused;
                    if (pause_state_changed) {
                        simnet::log(
                            simnet::LogCategory::Simulation,
                            simnet::LogLevel::Info,
                            simulation_paused ? "server simulation paused by client"
                                              : "server simulation resumed by client"
                        );
                    }
                    if (!send_pause_state(transport, peer, simulation_paused)) {
                        return false;
                    }
                } else {
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Error,
                        "server rejected unauthorized or duplicate application-control message"
                    );
                    transport.disconnect(control->peer, simnet::DisconnectCode::ProtocolMismatch);
                    remove_session_player(world, *peer, snapshot_state);
                    peer.reset();
                    continue;
                }
            } else if (auto const* input = std::get_if<simnet::ReceivedApplicationInput>(&event)) {
                auto decoded = simnet::app::PlayerInputMessage{};
                if (!peer.has_value() || input->peer != peer->peer
                    || peer->role != simnet::app::ClientRole::Player || peer->player_id == 0U
                    || !simnet::app::decode_player_input(input->payload, decoded)
                    || !simnet::set_authoritative_player_input(
                        world,
                        peer->player_id,
                        player_control(decoded)
                    )) {
                    simnet::log(
                        simnet::LogCategory::Transport,
                        simnet::LogLevel::Error,
                        "server rejected invalid or unauthorized player input"
                    );
                    transport.disconnect(input->peer, simnet::DisconnectCode::ProtocolMismatch);
                    if (peer.has_value() && peer->peer == input->peer) {
                        remove_session_player(world, *peer, snapshot_state);
                        peer.reset();
                    }
                }
            } else if (auto const* error = std::get_if<simnet::TransportErrorEvent>(&event)) {
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

    [[nodiscard]] bool run_tick(
        flecs::world& world,
        simnet::ServerGameRuntime& game,
        simnet::Tick tick,
        simnet::Nanoseconds fixed_dt,
        simnet::PipelineDefinition const& pipeline,
        simnet::Delivery delivery,
        simnet::TransportServer& transport,
        std::optional<PeerRuntimeState>& peer,
        simnet::PipelineScratch& scratch,
        CurrentSnapshotState& snapshot_state
    )
    {
        SIMNET_TRACE_SCOPE_CATEGORY("server.fixed_tick", simnet::LogCategory::Simulation);
        if (!advance_world(world, game, fixed_dt)) {
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Error,
                "authoritative boid step failed: " + game.last_step_report().error
            );
            return false;
        }
        snapshot_state.dirty = true;
        if (!peer.has_value() || !peer->role.has_value()) {
            return true;
        }

        auto const extraction = ensure_current_snapshot(world, tick, snapshot_state);
        if (!extraction.valid) {
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Error,
                "snapshot extraction failed: " + extraction.error
            );
            return false;
        }

        auto const delta_enabled
            = simnet::has_all_flags(pipeline.techniques, simnet::PipelineTechniqueFlags::Delta);
        auto baseline = peer->retained_snapshots.end();
        if (delta_enabled && peer->acknowledged_baseline_sequence.has_value()) {
            baseline = find_retained_snapshot(*peer, *peer->acknowledged_baseline_sequence);
            if (baseline == peer->retained_snapshots.end()) {
                invalidate_baseline(*peer);
            }
        }
        auto const use_baseline = baseline != peer->retained_snapshots.end();
        auto encoded = simnet::EncodeOutput{};
        {
            SIMNET_TRACE_SCOPE_CATEGORY("server.snapshot_encode", simnet::LogCategory::Pipeline);
            // Current and retained baseline snapshots come only from successful extraction.
            encoded = simnet::encode_snapshot_unchecked(
                pipeline,
                peer->pipeline_state,
                scratch,
                {
                    .snapshot = &snapshot_state.snapshot,
                    .baseline_snapshot = use_baseline ? &baseline->snapshot : nullptr,
                    .baseline_sequence = use_baseline ? baseline->sequence : 0U,
                }
            );
        }
        if (encoded.kind == simnet::EncodeResultKind::Skipped) {
            return true;
        }
        auto sent = simnet::TransportResult{};
        {
            SIMNET_TRACE_SCOPE_CATEGORY("server.snapshot_send", simnet::LogCategory::Transport);
            sent = transport.send({
                .peer = peer->peer,
                .lane = simnet::Lane::Snapshot,
                .delivery = delivery,
                .payload = encoded.update.bytes,
            });
        }
        if (!sent.ok) {
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Error,
                "snapshot send failed: " + sent.error.message
            );
            return false;
        }

        retain_snapshot(*peer, encoded.update.sequence, snapshot_state.snapshot);
        peer->newest_emitted_sequence = encoded.update.sequence;
        return true;
    }

    void disconnect_before_stop(
        simnet::TransportServer& transport,
        std::optional<PeerRuntimeState> const& peer
    )
    {
        if (!peer.has_value()) {
            return;
        }

        transport.disconnect(peer->peer, simnet::DisconnectCode::None);
        auto events = std::vector<simnet::TransportEvent>{};
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (std::chrono::steady_clock::now() < deadline) {
            events.clear();
            auto const result = transport.poll(events, 5);
            if (!result.ok) {
                return;
            }
            auto const disconnected = std::any_of(
                events.begin(),
                events.end(),
                [peer_id = peer->peer](simnet::TransportEvent const& event) {
                    auto const* value = std::get_if<simnet::PeerDisconnected>(&event);
                    return value != nullptr && value->peer == peer_id;
                }
            );
            if (disconnected) {
                return;
            }
        }
    }
}

namespace simnet::app
{
    int run_server(int argc, char** argv)
    {
        try {
            auto const options = parse_options(argc, argv);
            auto const shared_config_source
                = options.shared_config_path.value_or(default_shared_config_path());
            auto const local_config_source
                = options.config_path.value_or(default_server_config_path());
            auto const shared = load_shared_config(shared_config_source);
            auto const local = load_server_config(local_config_source);
            if (local.transport.max_clients > 1U) {
                throw std::runtime_error("server currently supports one client");
            }
            auto telemetry = TelemetryLifetime{local.telemetry};
#if defined(SIMNET_ENABLE_TRACY)
            log(LogCategory::Telemetry, LogLevel::Info, "Tracy instrumentation compiled in");
#else
            log(LogCategory::Telemetry, LogLevel::Info, "Tracy instrumentation not compiled in");
#endif
            auto signals = SignalHandlers{};
            auto const pipeline = make_snapshot_pipeline(shared, local.transport);
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
                .expected_identity = make_session_identity(shared, pipeline),
                .limits = {
                    .max_payload_bytes = local.transport.max_payload_bytes,
                    .size_policy = transport_send_size_policy(local.transport),
                },
            });
            if (!started.ok) {
                log(LogCategory::Transport,
                    LogLevel::Error,
                    "server transport start failed: " + started.error.message);
                return 1;
            }

            auto const settings = RuntimeSettings {
                .fixed_step = {
                    .tick_rate_hz = shared.simulation.tick_rate_hz,
                    .max_steps_per_frame = options.max_steps_per_frame,
                },
                .max_frame_time = options.max_frame_time,
                .max_frames = options.max_frames,
                .max_ticks = options.max_ticks,
                .max_runtime = options.max_runtime,
            };
            auto clock = make_clock(settings.fixed_step);
            if (clock.fixed_dt <= Nanoseconds{} || settings.fixed_step.max_steps_per_frame == 0) {
                throw std::runtime_error("invalid fixed-step runtime settings");
            }

#if defined(SIMNET_ENABLE_RENDER)
            auto viewer = std::optional<Viewer>{};
            if (local.visualization.enabled) {
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

            auto game = ServerGameRuntime{
                boid_settings(shared),
                player_settings(shared),
            };
            auto world = flecs::world{};
            register_server_game(world, game);
            auto const initialization_start = std::chrono::steady_clock::now();
            log(LogCategory::Simulation,
                LogLevel::Info,
                "initializing authoritative world entities="
                    + std::to_string(shared.simulation.initial_boid_count));
            auto const population = initialize_world(world, shared);
            if (!population.success()) {
                throw std::runtime_error(
                    "authoritative world initialization failed: "
                    + std::string{authoritative_spawn_error_name(population.error)}
                );
            }
            auto const initialization_elapsed = std::chrono::duration_cast<Nanoseconds>(
                std::chrono::steady_clock::now() - initialization_start
            );
            log(LogCategory::Simulation,
                LogLevel::Info,
                "authoritative world initialized elapsed_ns="
                    + std::to_string(initialization_elapsed.count()));
            if (local.flecs.thread_count > 1U) {
                world.set_threads(static_cast<std::int32_t>(local.flecs.thread_count));
            }
            log(LogCategory::Simulation,
                LogLevel::Info,
                "Flecs scheduler threads=" + std::to_string(local.flecs.thread_count));
            SIMNET_TRACE_PLOT(
                "server.flecs.thread_count",
                static_cast<double>(local.flecs.thread_count)
            );
            auto boid_csv = BoidCsvEvidence{
                local.telemetry,
                shared.simulation.tick_rate_hz,
                local.flecs.thread_count,
            };

            auto stats = RuntimeStats{};
            auto timer = RuntimeFrameTimer{};
            reset_frame_timer(timer);
            auto stop = StopRequest{};
            auto peer = std::optional<PeerRuntimeState>{};
            auto events = std::vector<TransportEvent>{};
            auto scratch = PipelineScratch{};
            auto const delivery = snapshot_delivery(local.transport);
            auto const delta_enabled
                = has_all_flags(pipeline.techniques, PipelineTechniqueFlags::Delta);
            auto simulation_paused = false;
            auto current_snapshot = CurrentSnapshotState{};
#if defined(SIMNET_ENABLE_RENDER)
            auto spatial_render = SpatialRenderStorage{};
            auto selected_debug_render = SelectedDebugRenderStorage{};
            auto presentation = PresentationSnapshotState{};
            auto spatial_snapshot_tick = std::optional<Tick>{};
            auto selected_entity = std::optional<EntityNetId>{};
            if (viewer.has_value()) {
                // Viewer startup is not simulation time and must not create an initial catch-up frame.
                reset_frame_timer(timer);
            }
#endif

            log(LogCategory::Simulation,
                LogLevel::Info,
                "server runtime started entities="
                    + std::to_string(shared.simulation.initial_boid_count));

            while (!stop.requested()) {
                if (signal_stop_requested()) {
                    static_cast<void>(stop.request(ShutdownReason::Signal));
                    break;
                }
                auto pause_state_changed = false;
                auto transport_ok = false;
                {
                    SIMNET_TRACE_SCOPE_CATEGORY("server.transport_poll", LogCategory::Transport);
                    transport_ok = poll_transport(
                        world,
                        transport,
                        peer,
                        events,
                        1,
                        delta_enabled,
                        simulation_paused,
                        pause_state_changed,
                        current_snapshot
                    );
                }
                if (!transport_ok) {
                    static_cast<void>(stop.request(ShutdownReason::FatalError));
                    break;
                }
                if (pause_state_changed) {
                    clock.accumulator = Nanoseconds{};
                }

                auto const frame_delta = sample_frame_delta(timer);
                auto frame = RuntimeFramePlan{};
                if (simulation_paused) {
                    ++stats.frames;
                    stats.raw_time += frame_delta;
                    stats.accepted_time += frame_delta;
                    clock.accumulator = Nanoseconds{};
                } else {
                    frame = plan_runtime_frame(clock, stats, frame_delta, settings);
                }
                for (std::uint16_t offset = 0; offset < frame.step_count; ++offset) {
                    auto const tick = frame.first_tick + offset;
                    if (!run_tick(
                            world,
                            game,
                            tick,
                            clock.fixed_dt,
                            pipeline,
                            delivery,
                            transport,
                            peer,
                            scratch,
                            current_snapshot
                        )) {
                        static_cast<void>(stop.request(ShutdownReason::FatalError));
                        break;
                    }
#if defined(SIMNET_ENABLE_RENDER)
                    if (viewer.has_value() && local.visualization.interpolation_enabled) {
                        auto const extracted
                            = ensure_current_snapshot(world, tick, current_snapshot);
                        if (!extracted.valid) {
                            log(LogCategory::Simulation,
                                LogLevel::Error,
                                "presentation snapshot extraction failed: " + extracted.error);
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                            break;
                        }
                        retain_presentation_snapshot(presentation, current_snapshot.snapshot);
                    }
#endif
                    boid_csv.sample(tick, game.last_step_report());
                }

                if (frame.step_limit_reached) {
                    log(LogCategory::Core,
                        LogLevel::Warn,
                        "server dropped simulation time ns="
                            + std::to_string(frame.dropped_time.count()));
                }
#if defined(SIMNET_ENABLE_RENDER)
                if (viewer.has_value()) {
                    auto const extracted
                        = ensure_current_snapshot(world, stats.ticks, current_snapshot);
                    if (!extracted.valid) {
                        log(LogCategory::Simulation,
                            LogLevel::Error,
                            "render snapshot extraction failed: " + extracted.error);
                        static_cast<void>(stop.request(ShutdownReason::FatalError));
                    } else if (
                        !spatial_snapshot_tick.has_value()
                        || *spatial_snapshot_tick != current_snapshot.extracted_tick
                    ) {
                        spatial_snapshot_tick = current_snapshot.extracted_tick;
                        rebuild_spatial_render_view(
                            spatial_render,
                            current_snapshot.snapshot,
                            shared,
                            Vec3f{},
                            local.visualization.max_visible_spatial_cells
                        );
                    }
                    if (!stop.requested() && current_snapshot.valid) {
                        if (local.visualization.interpolation_enabled) {
                            retain_presentation_snapshot(presentation, current_snapshot.snapshot);
                        }
                        auto const* displayed_snapshot = local.visualization.interpolation_enabled
                            ? presentation_snapshot(
                                  presentation,
                                  local.visualization.interpolation_enabled,
                                  simulation_paused,
                                  frame.interpolation_alpha
                              )
                            : &current_snapshot.snapshot;
                        if (displayed_snapshot == nullptr) {
                            log(LogCategory::Render,
                                LogLevel::Error,
                                "server presentation interpolation failed");
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                            continue;
                        }
                        auto const interpolation_active = local.visualization.interpolation_enabled
                            && !simulation_paused && presentation.has_previous;
                        auto const interpolation = RenderInterpolationInfo{
                            .enabled = local.visualization.interpolation_enabled,
                            .interpolating = interpolation_active,
                            .from_tick = presentation.has_previous ? presentation.previous.tick
                                                                   : current_snapshot.snapshot.tick,
                            .to_tick = current_snapshot.snapshot.tick,
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
                                peer,
                                spatial_render,
                                selected_debug_render,
                                game.selected_boid_debug(),
                                run_setup.view()
                            ));
                        }
                        selected_entity = viewer_result.selected_entity;
                        game.select_boid(selected_entity);
                        if (viewer_result.close_requested) {
                            static_cast<void>(stop.request(ShutdownReason::WindowClosed));
                        }
                        if (viewer_result.toggle_simulation_pause_requested) {
                            simulation_paused = !simulation_paused;
                            clock.accumulator = Nanoseconds{};
                            log(LogCategory::Simulation,
                                LogLevel::Info,
                                simulation_paused ? "server simulation paused by viewer"
                                                  : "server simulation resumed by viewer");
                            if (!send_pause_state(transport, peer, simulation_paused)) {
                                static_cast<void>(stop.request(ShutdownReason::FatalError));
                            }
                        }
                    }
                }
#endif
                auto const limit = reached_runtime_limit(settings, stats);
                if (limit != ShutdownReason::None) {
                    static_cast<void>(stop.request(limit));
                }
                SIMNET_TRACE_PLOT("server.runtime.steps", static_cast<double>(frame.step_count));
                SIMNET_TRACE_PLOT(
                    "server.runtime.entities",
                    static_cast<double>(shared.simulation.initial_boid_count)
                );
                SIMNET_TRACE_FRAME("server");
            }

            if (stats.ticks != 0U) {
                boid_csv.sample(stats.ticks, game.last_step_report(), true);
            }
            disconnect_before_stop(transport, peer);
            transport.stop();
            log(LogCategory::Simulation,
                LogLevel::Info,
                "server runtime stopped reason=" + std::string{shutdown_reason_name(stop.reason())}
                    + " frames=" + std::to_string(stats.frames)
                    + " ticks=" + std::to_string(stats.ticks)
                    + " dropped_ns=" + std::to_string(stats.dropped_time.count()));
            return stop.reason() == ShutdownReason::FatalError ? 1 : 0;
        } catch (std::exception const& error) {
            std::cerr << "Server failed: " << error.what() << '\n';
            return 1;
        }
    }
}
