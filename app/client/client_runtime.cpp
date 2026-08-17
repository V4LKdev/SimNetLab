module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <flecs.h>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <simnet/telemetry_trace.hpp>

module simnet.client_runtime;

import :replication;

import simnet.config;
import simnet.app_camera;
import simnet.app_common;
import simnet.app_player_input;
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
#if defined(SIMNET_ENABLE_RENDER)
import simnet.app_visual_setup;
import simnet.render;
#endif

using simnet::app::client_replication::ClientEvidenceIdentity;
using simnet::app::client_replication::ClientReplicationReceiver;

namespace
{
    struct ClientOptions
    {
        std::optional<std::filesystem::path> config_path{};
        std::optional<std::filesystem::path> shared_config_path{};
        std::optional<std::string> run_id{};
        std::uint64_t max_frames{};
        simnet::Tick max_ticks{};
        simnet::Nanoseconds max_runtime{};
    };

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

#if defined(SIMNET_ENABLE_RENDER)
    [[nodiscard]] std::optional<simnet::GameCameraView>
    player_game_camera(simnet::WorldSnapshot const& snapshot, simnet::EntityNetId player_id)
    {
        if (player_id == 0U)
        {
            return std::nullopt;
        }
        auto const found = std::ranges::find(snapshot.ids, player_id);
        if (found == snapshot.ids.end())
        {
            return std::nullopt;
        }
        auto const offset = static_cast<std::size_t>(std::distance(snapshot.ids.begin(), found));
        auto const pose = simnet::app::locked_chase_camera_pose(
            snapshot.positions[offset],
            snapshot.headings[offset]
        );
        return simnet::GameCameraView{
            .position = pose.position,
            .target = pose.target,
            .up = pose.up,
            .vertical_fov_degrees = 70.0F,
        };
    }

    [[nodiscard]] simnet::app::PlayerInputMessage
    player_input_message(simnet::PlayerViewInput input) noexcept
    {
        auto buttons = std::uint8_t{};
        auto const set = [&buttons](bool down, simnet::app::PlayerButton button)
        {
            if (down)
            {
                buttons |= static_cast<std::uint8_t>(button);
            }
        };
        set(input.pitch_up, simnet::app::PlayerButton::W);
        set(input.yaw_left, simnet::app::PlayerButton::A);
        set(input.pitch_down, simnet::app::PlayerButton::S);
        set(input.yaw_right, simnet::app::PlayerButton::D);
        set(input.accelerate, simnet::app::PlayerButton::Shift);
        set(input.decelerate, simnet::app::PlayerButton::Control);
        set(input.left_mouse, simnet::app::PlayerButton::LeftMouse);
        set(input.right_mouse, simnet::app::PlayerButton::RightMouse);
        return {.buttons = buttons};
    }

    struct ClientPresentationState
    {
        simnet::Tick observed_tick{};
        simnet::Nanoseconds elapsed{};
        simnet::WorldSnapshot interpolated{};
    };

    [[nodiscard]] simnet::WorldSnapshot const* presentation_snapshot(
        ClientReplicationReceiver const& replication,
        ClientPresentationState& state,
        simnet::Nanoseconds frame_delta,
        double tick_rate_hz,
        bool interpolation_enabled,
        bool paused,
        double& alpha
    )
    {
        auto const& history = replication.snapshot_history;
        alpha = 1.0;
        if (history.empty())
        {
            return nullptr;
        }

        auto const& current = history.back();
        if (state.observed_tick != current.snapshot.tick)
        {
            state.observed_tick = current.snapshot.tick;
            state.elapsed = {};
        }
        else if (!paused)
        {
            state.elapsed += std::max(frame_delta, simnet::Nanoseconds{});
        }
        if (!interpolation_enabled || paused || history.size() < 2U)
        {
            return &current.snapshot;
        }

        auto const& previous = history[history.size() - 2U];
        if (previous.snapshot.tick >= current.snapshot.tick || tick_rate_hz <= 0.0)
        {
            return &current.snapshot;
        }
        auto const tick_span = current.snapshot.tick - previous.snapshot.tick;
        auto const interval_seconds = static_cast<double>(tick_span) / tick_rate_hz;
        auto const elapsed_seconds = std::chrono::duration<double>(state.elapsed).count();
        alpha = std::clamp(elapsed_seconds / interval_seconds, 0.0, 1.0);
        SIMNET_TRACE_SCOPE_CATEGORY("client.presentation.interpolate", simnet::LogCategory::Render);
        // History contains only snapshots from successful reconstruction and is not mutated here.
        auto const interpolated = simnet::interpolate_world_snapshots_unchecked(
            previous.snapshot,
            current.snapshot,
            alpha,
            state.interpolated
        );
        return interpolated.valid ? &state.interpolated : nullptr;
    }
#endif

    enum class ClientConnectionState : std::uint8_t
    {
        Connecting,
        SessionReady,
        Disconnected
    };

    enum class ClientStopCause : std::uint8_t
    {
        None,
        ConnectionTimeout,
        RemoteShutdown,
        ConnectionLost,
        ProtocolError
    };

    [[nodiscard]] std::string_view client_stop_cause_name(ClientStopCause cause) noexcept
    {
        switch (cause)
        {
            case ClientStopCause::None:
                return "none";
            case ClientStopCause::ConnectionTimeout:
                return "connection_timeout";
            case ClientStopCause::RemoteShutdown:
                return "remote_shutdown";
            case ClientStopCause::ConnectionLost:
                return "connection_lost";
            case ClientStopCause::ProtocolError:
                return "protocol_error";
        }
        return "unknown";
    }

    [[nodiscard]] simnet::app::ClientRole configured_role(std::string_view role)
    {
        if (role == "stationary_observer")
        {
            return simnet::app::ClientRole::StationaryObserver;
        }
        if (role == "player")
        {
            return simnet::app::ClientRole::Player;
        }
        throw std::runtime_error("unsupported gameplay role: " + std::string{role});
    }

#if defined(SIMNET_ENABLE_RENDER)
    [[nodiscard]] std::string_view
    client_connection_state_name(ClientConnectionState state) noexcept
    {
        switch (state)
        {
            case ClientConnectionState::Connecting:
                return "connecting";
            case ClientConnectionState::SessionReady:
                return "session ready";
            case ClientConnectionState::Disconnected:
                return "disconnected";
        }
        return "unknown";
    }
#endif

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
            .title = "SimNet Client",
        };
    }

    [[nodiscard]] simnet::RenderFrame render_frame(
        simnet::WorldSnapshot const& snapshot,
        simnet::SharedConfig const& config,
        simnet::Nanoseconds frame_delta,
        std::optional<simnet::SequenceId> sequence,
        bool session_ready,
        std::optional<bool> simulation_paused,
        simnet::RenderInterpolationInfo interpolation,
        ClientConnectionState connection_state,
        std::optional<simnet::PeerId> peer,
        ClientReplicationReceiver const& client_replication,
        std::string_view role,
        std::optional<simnet::StationaryObserverView> stationary_observer,
        std::optional<simnet::GameCameraView> game_camera,
        simnet::RunSetupView setup
    )
    {
        auto const& ack = client_replication.ack_tracker.value;
        auto const& packet_report = client_replication.reassembly_state.report;
        auto const& compression_report = client_replication.compression_report;
        auto const reconstructed_entity_count =
            client_replication.snapshot_history.empty()
                ? std::optional<std::uint32_t>{}
                : std::optional<std::uint32_t>{static_cast<std::uint32_t>(
                      client_replication.snapshot_history.back().snapshot.size()
                  )};
        auto const applied_upsert_count =
            client_replication.latest_applied_sequence == 0U
                ? std::optional<std::uint32_t>{}
                : std::optional<std::uint32_t>{client_replication.latest_applied_upserts};
        auto const applied_delete_count =
            client_replication.latest_applied_sequence == 0U
                ? std::optional<std::uint32_t>{}
                : std::optional<std::uint32_t>{client_replication.latest_applied_deletes};
        auto replication = std::optional<simnet::RenderReplicationInfo>{};
        if (session_ready || ack.newest_received_snapshot != 0 || ack.newest_applied_snapshot != 0)
        {
            replication = simnet::RenderReplicationInfo{
                .latest_received_sequence =
                    ack.newest_received_snapshot != 0
                        ? std::optional<simnet::SequenceId>{ack.newest_received_snapshot}
                        : std::optional<simnet::SequenceId>{},
                .latest_applied_sequence =
                    ack.newest_applied_snapshot != 0
                        ? std::optional<simnet::SequenceId>{ack.newest_applied_snapshot}
                        : std::optional<simnet::SequenceId>{},
                .configured_delivery = config.snapshot_delivery.mode,
                .effective_delivery =
                    client_replication.effective_snapshot_delivery.has_value()
                        ? std::optional<std::string_view>{simnet::app::transport_delivery_name(
                              *client_replication.effective_snapshot_delivery
                          )}
                        : std::optional<std::string_view>{},
                .recovery_request_count = client_replication.recovery_request_state.sent_count,
                .missing_baseline_rejection_count =
                    client_replication.recovery_request_state.missing_baseline_rejection_count,
                .sequence_gap_count = client_replication.sequence_gap_count,
                .reliable_promotion_count = client_replication.reliable_promotion_count,
                .latest_snapshot_tick = snapshot.tick,
                .area_of_interest_mode = config.pipeline.area_of_interest.mode,
                .interest_source_status =
                    config.pipeline.area_of_interest.mode == "none"
                        ? std::optional<std::string_view>{"not required"}
                    : role == "player"
                        ? std::optional<std::string_view>{"Server authoritative Player"}
                        : std::optional<std::string_view>{"local pose configured"},
                .level_of_detail_mode = config.pipeline.level_of_detail.mode,
                .applied_upsert_count = applied_upsert_count,
                .applied_delete_count = applied_delete_count,
                .reconstructed_entity_count = reconstructed_entity_count,
                .packetization_enabled = config.packetization.enabled,
                .incomplete_packet_groups = packet_report.incomplete_groups,
                .expired_packet_groups = packet_report.expired_groups,
                .invalid_packet_groups = packet_report.invalid_groups,
                .compression_mode = simnet::app::compression_mode_name(compression_report.mode),
                .compression_outcome =
                    compression_report.mode == simnet::app::CompressionMode::None
                        ? std::optional<std::string_view>{"Disabled"}
                    : compression_report.mode == simnet::app::CompressionMode::WholeUpdate
                        ? std::optional<std::string_view>{compression_encoding_name(
                              compression_report.latest_encoding
                          )}
                    : compression_report.compressed_packet_count == 0U
                        ? std::optional<std::string_view>{"Raw fallback"}
                    : compression_report.raw_packet_count == 0U
                        ? std::optional<std::string_view>{"Zstd"}
                        : std::optional<std::string_view>{"Mixed"},
                .representation_bytes = compression_report.latest_completed_representation_bytes,
            };
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
                    .snapshot_sequence = sequence,
                    .session_ready = session_ready,
                    .world_bounds = simnet::make_centered_bounds(config.simulation.world_half),
                    .frame_delta = frame_delta,
                    .fixed_tick_rate_hz = config.simulation.tick_rate_hz,
                    .simulation_paused = simulation_paused,
                    .interpolation = interpolation,
                    .context =
                        {
                            .kind = simnet::ViewerKind::Client,
                            .client_role = role,
                        },
                    .capabilities =
                        {
                            .can_pause_simulation = session_ready && simulation_paused.has_value(),
                            .has_networking = true,
                            .has_stationary_observer = role == "stationary observer",
                            .has_game_camera = role == "player",
                        },
                    .connection =
                        std::optional<simnet::RenderConnectionInfo>{simnet::RenderConnectionInfo{
                            .state = client_connection_state_name(connection_state),
                            .peer = peer,
                        }},
                    .replication = replication,
                },
            .stationary_observer = stationary_observer,
            .game_camera = game_camera,
            .setup = setup,
        };
    }
#endif

    [[nodiscard]] ClientOptions parse_options(int argc, char** argv)
    {
        auto options = ClientOptions{};
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
            else
            {
                throw std::runtime_error("unknown client option: " + std::string{option});
            }
        }
        return options;
    }


    [[nodiscard]] bool apply_application_control(
        simnet::ReceivedPacket const& control,
        simnet::app::ClientRole requested_role,
        bool& simulation_paused,
        bool& pause_state_received,
        bool& join_accepted,
        simnet::PeerId& assigned_peer_id,
        simnet::EntityNetId& player_id,
        simnet::ClientReplicationCsvWriter& csv
    )
    {
        auto message = simnet::app::AppMessage{};
        if (!simnet::app::decode_app_message(control.payload, message))
        {
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Error,
                "client received invalid application-control message"
            );
            return false;
        }

        if (message.kind == simnet::app::AppMessageKind::JoinAccepted && !join_accepted &&
            message.role == requested_role)
        {
            auto const accepted_role = message.role == simnet::app::ClientRole::Player
                                           ? std::string_view{"player"}
                                           : std::string_view{"stationary_observer"};
            if (!csv.set_accepted_gameplay_role(accepted_role))
            {
                throw std::runtime_error(
                    "client replication CSV role assignment failed: " + std::string{csv.error()}
                );
            }
            join_accepted = true;
            assigned_peer_id = message.peer_id;
            player_id = message.player_id;
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Info,
                "client join accepted role=" +
                    std::string{
                        requested_role == simnet::app::ClientRole::Player ? "player"
                                                                          : "stationary_observer"
                    } +
                    " peer_id=" + std::to_string(assigned_peer_id) +
                    " player_id=" + std::to_string(player_id)
            );
            return true;
        }
        if (message.kind != simnet::app::AppMessageKind::PauseState)
        {
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Error,
                "client received unauthorized or duplicate application-control message"
            );
            return false;
        }

        auto const changed = !pause_state_received || simulation_paused != message.paused;
        simulation_paused = message.paused;
        pause_state_received = true;
        if (changed)
        {
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Info,
                simulation_paused ? "client received authoritative pause state"
                                  : "client received authoritative resume state"
            );
        }
        return true;
    }

}
namespace simnet::app
{
    int run_client(int argc, char** argv)
    {
        auto replication_csv = std::optional<ClientReplicationCsvWriter>{};
        try
        {
            auto const options = parse_options(argc, argv);
            auto const run_context = make_evidence_run_context(
                EvidenceProcessRole::Client,
                options.run_id.has_value() ? std::optional<std::string_view>{*options.run_id}
                                           : std::nullopt
            );
            auto const shared_config_source =
                options.shared_config_path.value_or(default_shared_config_path());
            auto const local_config_source =
                options.config_path.value_or(default_client_config_path());
            auto const shared = load_shared_config(shared_config_source);
            auto const local = load_client_config(local_config_source);
            auto const requested_role = configured_role(local.gameplay.role);
            auto telemetry = TelemetryLifetime{local.telemetry};
#if defined(SIMNET_ENABLE_TRACY)
            log(LogCategory::Telemetry, LogLevel::Info, "Tracy instrumentation compiled in");
#else
            log(LogCategory::Telemetry, LogLevel::Info, "Tracy instrumentation not compiled in");
#endif
            auto signals = SignalHandlers{};
            auto const pipeline = make_snapshot_pipeline(shared);
            auto const compression = make_compression_settings(shared);
            auto const packetization = make_packetization_settings(shared);
            auto const delivery = snapshot_transport_delivery(shared.snapshot_delivery);
            if (packetization.max_payload_bytes > local.transport.max_payload_bytes ||
                (packetization.enabled && local.transport.send_size_policy != "enforce_limit"))
            {
                throw std::runtime_error(
                    "packetization payload limit must fit the hard transport payload limit"
                );
            }
            auto const session_identity = make_session_identity(shared, pipeline);
            auto const evidence_identity = ClientEvidenceIdentity{
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

            auto transport = TransportClient{};
            auto const connected = transport.connect({
                .server_address = local.transport.host,
                .server_port = local.transport.port,
                .identity = session_identity,
                .limits = {
                    .max_payload_bytes = local.transport.max_payload_bytes,
                    .size_policy = transport_send_size_policy(local.transport),
                },
            });
            if (!connected.ok)
            {
                log(LogCategory::Transport,
                    LogLevel::Error,
                    "client transport connect failed: " + connected.error.message);
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
                    "client replication CSV path=" + replication_csv->path().string());
            }

            auto const settings = RuntimeSettings{
                .max_frames = options.max_frames,
                .max_ticks = options.max_ticks,
                .max_runtime = options.max_runtime,
            };
            auto world = flecs::world{};
            register_client_game(world);
            auto stationary_observer = std::optional<StationaryObserverState>{};
            if (requested_role == app::ClientRole::StationaryObserver)
            {
                stationary_observer = StationaryObserverState{
                    .position =
                        {
                            local.gameplay.stationary_observer_position[0],
                            local.gameplay.stationary_observer_position[1],
                            local.gameplay.stationary_observer_position[2],
                        },
                    .interest_radius = local.visualization.stationary_observer_interest_radius,
                    .vertical_fov_degrees =
                        local.visualization.stationary_observer_vertical_fov_degrees,
                };
            }
#if defined(SIMNET_ENABLE_RENDER)
            auto viewer = std::optional<Viewer>{};
            auto presentation = ClientPresentationState{};
            auto empty_presentation = WorldSnapshot{};
            if (local.visualization.enabled)
            {
                viewer.emplace(viewer_config(local.visualization), local.telemetry.log_directory);
            }
#endif

            auto stats = RuntimeStats{};
            auto timer = RuntimeFrameTimer{};
            reset_frame_timer(timer);
            auto stop = StopRequest{};
            auto events = std::vector<TransportEvent>{};
            auto replication = ClientReplicationReceiver{
                pipeline,
                compression,
                packetization,
                delivery,
                local.transport.max_payload_bytes,
                evidence_identity,
            };
            auto connection_state = ClientConnectionState::Connecting;
            auto stop_cause = ClientStopCause::None;
            auto server_peer = std::optional<PeerId>{};
            auto authoritative_pause_state = false;
            auto pause_state_received = false;
            auto join_accepted = false;
            auto assigned_peer_id = PeerId{};
            auto player_id = EntityNetId{};
            auto player_input_delivery = app::PlayerInputDeliveryState{};
            auto last_observer_interest_send_time = std::optional<Nanoseconds>{};
            auto last_observer_interest_forward = Vec3f{};
#if defined(SIMNET_ENABLE_RENDER)
            auto game_view_initialized = false;
#endif

            log(LogCategory::Simulation, LogLevel::Info, "client runtime started");
            while (!stop.requested())
            {
                if (signal_stop_requested())
                {
                    static_cast<void>(stop.request(ShutdownReason::Signal));
                    break;
                }

                auto const delta = sample_frame_delta(timer);
                ++stats.frames;
                stats.raw_time += delta;
                stats.accepted_time += delta;

                events.clear();
                auto polled = TransportResult{};
                {
                    SIMNET_TRACE_SCOPE_CATEGORY("client.transport_poll", LogCategory::Transport);
                    polled = transport.poll(events, 1);
                }
                if (!polled.ok)
                {
                    log(LogCategory::Transport,
                        LogLevel::Error,
                        "client transport poll failed: " + polled.error.message);
                    stop_cause = ClientStopCause::ProtocolError;
                    static_cast<void>(stop.request(ShutdownReason::FatalError));
                    break;
                }

                replication.expire(stats.raw_time);

                for (auto& event : events)
                {
                    if (auto const* ready = std::get_if<PeerSessionReady>(&event))
                    {
                        connection_state = ClientConnectionState::SessionReady;
                        server_peer = ready->peer;
                        pause_state_received = false;
                        join_accepted = false;
                        assigned_peer_id = 0U;
                        player_id = 0U;
                        app::reset_player_input_delivery(player_input_delivery);
                        last_observer_interest_send_time.reset();
                        last_observer_interest_forward = {};
                        replication.begin_session();
#if defined(SIMNET_ENABLE_RENDER)
                        game_view_initialized = false;
#endif
                        log(LogCategory::Transport,
                            LogLevel::Info,
                            "client session ready peer=" + std::to_string(ready->peer));
                        auto const join_bytes = app::encode_app_message({
                            .kind = app::AppMessageKind::JoinRequest,
                            .role = requested_role,
                        });
                        auto const joined = transport.send(
                            app::control_lane,
                            TransportDelivery::ReliableSequenced,
                            join_bytes
                        );
                        if (!joined.ok)
                        {
                            log(LogCategory::Transport,
                                LogLevel::Error,
                                "client join request send failed: " + joined.error.message);
                            stop_cause = ClientStopCause::ProtocolError;
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                            break;
                        }
                    }
                    else if (auto const* packet = std::get_if<ReceivedPacket>(&event))
                    {
                        auto valid = connection_state == ClientConnectionState::SessionReady;
                        if (valid && packet->lane == app::control_lane)
                        {
                            valid = packet->delivery == TransportDelivery::ReliableSequenced &&
                                    apply_application_control(
                                        *packet,
                                        requested_role,
                                        authoritative_pause_state,
                                        pause_state_received,
                                        join_accepted,
                                        assigned_peer_id,
                                        player_id,
                                        *replication_csv
                                    );
                        }
                        else if (valid && packet->lane == app::snapshot_lane)
                        {
                            valid = replication.receive_snapshot(
                                *packet,
                                assigned_peer_id,
                                stats.raw_time,
                                world,
                                transport,
                                stats,
                                *replication_csv
                            );
                        }
                        else if (valid)
                        {
                            valid = false;
                        }
                        if (!valid)
                        {
                            stop_cause = ClientStopCause::ProtocolError;
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                            break;
                        }
                    }
                    else if (auto const* disconnected = std::get_if<PeerDisconnected>(&event))
                    {
                        auto const was_session_ready =
                            connection_state == ClientConnectionState::SessionReady;
                        connection_state = ClientConnectionState::Disconnected;
                        app::reset_player_input_delivery(player_input_delivery);
                        replication.discard_incomplete();
                        if (!was_session_ready && disconnected->code == DisconnectCode::Timeout)
                        {
                            stop_cause = ClientStopCause::ConnectionTimeout;
                            log(LogCategory::Transport,
                                LogLevel::Error,
                                "client connection or session handshake timed out peer=" +
                                    std::to_string(disconnected->peer));
                        }
                        else if (was_session_ready && disconnected->code == DisconnectCode::None)
                        {
                            stop_cause = ClientStopCause::RemoteShutdown;
                            log(LogCategory::Transport,
                                LogLevel::Info,
                                "client received remote shutdown peer=" +
                                    std::to_string(disconnected->peer));
                        }
                        else if (was_session_ready)
                        {
                            stop_cause = ClientStopCause::ConnectionLost;
                            log(LogCategory::Transport,
                                LogLevel::Error,
                                "client connection lost peer=" +
                                    std::to_string(disconnected->peer));
                        }
                        else
                        {
                            stop_cause = ClientStopCause::ProtocolError;
                            log(LogCategory::Transport,
                                LogLevel::Error,
                                "client disconnected before session readiness peer=" +
                                    std::to_string(disconnected->peer));
                        }
                        static_cast<void>(stop.request(
                            stop_cause == ClientStopCause::RemoteShutdown
                                ? ShutdownReason::TransportDisconnected
                                : ShutdownReason::FatalError
                        ));
                        break;
                    }
                    else if (auto const* error = std::get_if<TransportErrorEvent>(&event))
                    {
                        log(LogCategory::Transport,
                            LogLevel::Error,
                            "client transport error: " + error->message);
                        stop_cause = ClientStopCause::ProtocolError;
                        static_cast<void>(stop.request(ShutdownReason::FatalError));
                        break;
                    }
                }

                if (replication_csv->needs_drain() && !replication_csv->drain())
                {
                    throw std::runtime_error(
                        "client replication CSV drain failed: " +
                        std::string{replication_csv->error()}
                    );
                }

#if defined(SIMNET_ENABLE_RENDER)
                if (!stop.requested() && viewer.has_value())
                {
                    auto interpolation_alpha = 1.0;
                    auto const* displayed_snapshot = presentation_snapshot(
                        replication,
                        presentation,
                        delta,
                        shared.simulation.tick_rate_hz,
                        local.visualization.interpolation_enabled,
                        pause_state_received && authoritative_pause_state,
                        interpolation_alpha
                    );
                    if (displayed_snapshot == nullptr && replication.snapshot_history.empty())
                    {
                        displayed_snapshot = &empty_presentation;
                    }
                    if (!replication.snapshot_history.empty() && displayed_snapshot == nullptr)
                    {
                        log(LogCategory::Render,
                            LogLevel::Error,
                            "client presentation interpolation failed");
                        stop_cause = ClientStopCause::ProtocolError;
                        static_cast<void>(stop.request(ShutdownReason::FatalError));
                    }
                    else if (displayed_snapshot != nullptr)
                    {
                        auto const has_pair =
                            replication.snapshot_history.size() >= 2U &&
                            replication.snapshot_history[replication.snapshot_history.size() - 2U].snapshot.tick <
                                replication.snapshot_history.back().snapshot.tick;
                        auto const interpolation_active =
                            local.visualization.interpolation_enabled &&
                            !(pause_state_received && authoritative_pause_state) && has_pair;
                        auto const interpolation = RenderInterpolationInfo{
                            .enabled = local.visualization.interpolation_enabled,
                            .interpolating = interpolation_active,
                            .from_tick =
                                has_pair
                                    ? replication.snapshot_history[replication.snapshot_history.size() - 2U].snapshot.tick
                                    : displayed_snapshot->tick,
                            .to_tick = replication.snapshot_history.empty()
                                           ? displayed_snapshot->tick
                                           : replication.snapshot_history.back().snapshot.tick,
                            .alpha = interpolation_active ? interpolation_alpha : 1.0,
                        };
                        SIMNET_TRACE_PLOT("client.render.interpolation_alpha", interpolation.alpha);
                        auto const sequence =
                            replication.latest_applied_sequence == 0
                                ? std::optional<SequenceId>{}
                                : std::optional<SequenceId>{replication.latest_applied_sequence};
                        auto viewer_result = ViewerResult{};
                        auto game_camera = requested_role == app::ClientRole::Player
                                               ? player_game_camera(*displayed_snapshot, player_id)
                                               : std::optional<GameCameraView>{};
                        auto stationary_observer_view = std::optional<StationaryObserverView>{};
                        if (stationary_observer.has_value())
                        {
                            stationary_observer_view = StationaryObserverView{
                                .position = stationary_observer->position,
                                .forward = app::stationary_observer_forward(*stationary_observer),
                                .interest_radius = stationary_observer->interest_radius,
                                .vertical_fov_degrees = stationary_observer->vertical_fov_degrees,
                            };
                        }
                        if (requested_role == app::ClientRole::Player && join_accepted &&
                            game_camera.has_value() && !game_view_initialized)
                        {
                            viewer->set_camera_mode(CameraMode::Game);
                            game_view_initialized = true;
                        }
                        {
                            SIMNET_TRACE_SCOPE_CATEGORY("client.viewer_draw", LogCategory::Render);
                            viewer_result = viewer->draw(render_frame(
                                *displayed_snapshot,
                                shared,
                                delta,
                                sequence,
                                connection_state == ClientConnectionState::SessionReady,
                                pause_state_received
                                    ? std::optional<bool>{authoritative_pause_state}
                                    : std::optional<bool>{},
                                interpolation,
                                connection_state,
                                join_accepted ? std::optional<PeerId>{assigned_peer_id}
                                              : server_peer,
                                replication,
                                requested_role == app::ClientRole::Player
                                    ? std::string_view{"player"}
                                    : std::string_view{"stationary observer"},
                                stationary_observer_view,
                                game_camera,
                                run_setup.view()
                            ));
                        }
                        if (viewer_result.close_requested)
                        {
                            static_cast<void>(stop.request(ShutdownReason::WindowClosed));
                        }
                        if (stationary_observer.has_value())
                        {
                            apply_stationary_observer_rotation(
                                *stationary_observer,
                                viewer_result.stationary_observer_yaw_axis,
                                viewer_result.stationary_observer_pitch_axis,
                                delta
                            );
                        }
                        if (viewer_result.toggle_simulation_pause_requested && pause_state_received)
                        {
                            auto const bytes = app::encode_app_message({
                                .kind = app::AppMessageKind::PauseSetRequest,
                                .paused = !authoritative_pause_state,
                            });
                            auto const sent = transport.send(
                                app::control_lane,
                                TransportDelivery::ReliableSequenced,
                                bytes
                            );
                            if (!sent.ok)
                            {
                                log(LogCategory::Transport,
                                    LogLevel::Error,
                                    "client pause request send failed: " + sent.error.message);
                                stop_cause = ClientStopCause::ProtocolError;
                                static_cast<void>(stop.request(ShutdownReason::FatalError));
                            }
                        }
                        if (!stop.requested() && requested_role == app::ClientRole::Player)
                        {
                            auto const input_active =
                                join_accepted && viewer_result.camera_mode == CameraMode::Game;
                            app::set_desired_player_input(
                                player_input_delivery,
                                input_active ? player_input_message(viewer_result.player_input)
                                             : app::PlayerInputMessage{}
                            );
                        }
                    }
                }
#endif

                if (!stop.requested())
                {
                    auto const accepted_player_session =
                        connection_state == ClientConnectionState::SessionReady &&
                        requested_role == app::ClientRole::Player && join_accepted;
                    auto const submission = app::plan_player_input_submission(
                        player_input_delivery,
                        accepted_player_session,
                        stats.raw_time
                    );
                    if (submission.has_value())
                    {
                        auto const bytes = app::encode_player_input(submission->input);
                        auto const sent = transport.send(
                            app::input_lane,
                            TransportDelivery::UnreliableSequenced,
                            bytes
                        );
                        if (!sent.ok)
                        {
                            app::record_player_input_submission_failure(player_input_delivery);
                            log(LogCategory::Transport,
                                LogLevel::Error,
                                "client player-input send failed: " + sent.error.message);
                            stop_cause = ClientStopCause::ProtocolError;
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                        }
                        else
                        {
                            app::record_player_input_submission(
                                player_input_delivery,
                                *submission,
                                stats.raw_time
                            );
                        }
                    }
                }

                if (!stop.requested() && stationary_observer.has_value() && join_accepted &&
                    pipeline.area_of_interest.mode != AreaOfInterestMode::None)
                {
                    auto const forward = app::stationary_observer_forward(*stationary_observer);
                    auto const direction_changed = forward.x != last_observer_interest_forward.x ||
                                                   forward.y != last_observer_interest_forward.y ||
                                                   forward.z != last_observer_interest_forward.z;
                    auto const since_last =
                        last_observer_interest_send_time.has_value()
                            ? stats.raw_time - *last_observer_interest_send_time
                            : app::stationary_observer_interest_heartbeat_interval;
                    auto const update_due =
                        !last_observer_interest_send_time.has_value() ||
                        (direction_changed &&
                         since_last >= app::stationary_observer_interest_min_interval) ||
                        since_last >= app::stationary_observer_interest_heartbeat_interval;
                    if (update_due)
                    {
                        auto const bytes = app::encode_stationary_observer_interest({
                            .position = stationary_observer->position,
                            .forward = forward,
                        });
                        auto const sent = transport.send(
                            app::input_lane,
                            TransportDelivery::UnreliableSequenced,
                            bytes
                        );
                        if (!sent.ok)
                        {
                            log(LogCategory::Transport,
                                LogLevel::Error,
                                "client observer-interest send failed: " + sent.error.message);
                            stop_cause = ClientStopCause::ProtocolError;
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                        }
                        else
                        {
                            last_observer_interest_send_time = stats.raw_time;
                            last_observer_interest_forward = forward;
                        }
                    }
                }

                if (!stop.requested())
                {
                    auto const limit = reached_runtime_limit(settings, stats);
                    if (limit != ShutdownReason::None)
                    {
                        static_cast<void>(stop.request(limit));
                    }
                }
                SIMNET_TRACE_FRAME("client");
            }

            transport.disconnect(DisconnectCode::None);
            if (!replication_csv->close())
            {
                throw std::runtime_error(
                    "client replication CSV close failed: " + std::string{replication_csv->error()}
                );
            }
            auto const canonical_entities =
                replication.snapshot_history.empty() ? 0U : replication.snapshot_history.back().snapshot.size();
            auto const canonical_fingerprint =
                replication.snapshot_history.empty()
                    ? 0U
                    : app::snapshot_diagnostic_fingerprint(replication.snapshot_history.back().snapshot);
            log(LogCategory::Telemetry,
                LogLevel::Info,
                "client evidence summary run_id=" + run_context.run_id + " path=" +
                    (replication_csv->enabled() ? replication_csv->path().string()
                                                : std::string{"disabled"}) +
                    " shutdown_reason=" + std::string{shutdown_reason_name(stop.reason())} +
                    " final_tick=" + std::to_string(stats.ticks) + " writer_healthy=" +
                    std::string{replication_csv->healthy() ? "true" : "false"} +
                    " submitted_packet_rows=" +
                    std::to_string(replication_csv->submitted_count()) + " applied_updates=" +
                    std::to_string(replication.measurements.applied_count) +
                    " final_canonical_count=" + std::to_string(canonical_entities) +
                    " final_canonical_fingerprint=" + std::to_string(canonical_fingerprint) +
                    " latest_applied_sequence=" +
                    std::to_string(replication.latest_applied_sequence) + " ack_sequence=" +
                    std::to_string(replication.ack_tracker.value.newest_applied_snapshot) +
                    " sequence_gaps=" + std::to_string(replication.sequence_gap_count) +
                    " recovery_requests=" +
                    std::to_string(replication.recovery_request_state.sent_count) +
                    " dropped_ns=" + std::to_string(stats.dropped_time.count()) +
                    " dropped_time_warning=" +
                    std::string{stats.dropped_time > Nanoseconds{} ? "true" : "false"});
            log(LogCategory::Transport,
                LogLevel::Info,
                "client snapshot delivery mode=" + shared.snapshot_delivery.mode + " effective=" +
                    std::string{
                        replication.effective_snapshot_delivery.has_value()
                            ? app::transport_delivery_name(*replication.effective_snapshot_delivery)
                            : "unavailable"
                    } +
                    " peer_id=" + std::to_string(assigned_peer_id) +
                    " player_id=" + std::to_string(player_id) +
                    " reliable_promotions=" + std::to_string(replication.reliable_promotion_count) +
                    " player_input_state_changes=" +
                    std::to_string(player_input_delivery.state_change_submission_count) +
                    " player_input_heartbeats=" +
                    std::to_string(player_input_delivery.heartbeat_submission_count) +
                    " player_input_failures=" +
                    std::to_string(player_input_delivery.failed_submission_count));
            log(LogCategory::Simulation,
                LogLevel::Info,
                "client runtime stopped reason=" +
                    std::string{shutdown_reason_name(stop.reason())} +
                    " cause=" + std::string{client_stop_cause_name(stop_cause)} + " frames=" +
                    std::to_string(stats.frames) + " ticks=" + std::to_string(stats.ticks) +
                    " runtime_ns=" + std::to_string(stats.raw_time.count()));
            telemetry.shutdown();
            return stop.reason() == ShutdownReason::FatalError ? 1 : 0;
        }
        catch (std::exception const& error)
        {
            auto close_error = std::string{};
            if (replication_csv.has_value() && !replication_csv->close())
            {
                close_error = std::string{replication_csv->error()};
            }
            std::cerr << "Client failed: " << error.what();
            if (!close_error.empty())
            {
                std::cerr << ". Evidence close failed: " << close_error;
            }
            std::cerr << '\n';
            return 1;
        }
    }
}
