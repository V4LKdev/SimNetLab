#include "client_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
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

import simnet.config;
import simnet.app_camera;
import simnet.app_common;
import simnet.app_protocol;
import simnet.core;
import simnet.game_client;
import simnet.pipeline;
import simnet.runtime;
import simnet.snapshot;
import simnet.telemetry;
import simnet.transport;
#if defined(SIMNET_ENABLE_RENDER)
import simnet.app_visual_setup;
import simnet.render;
#endif

namespace
{
    struct ClientOptions
    {
        std::optional<std::filesystem::path> config_path {};
        std::optional<std::filesystem::path> shared_config_path {};
        std::uint64_t max_frames {};
        simnet::Tick max_ticks {};
        simnet::Nanoseconds max_runtime {};
    };

    struct SnapshotAckTracker
    {
        simnet::SnapshotAck value {};
    };

    struct RetainedClientSnapshot
    {
        simnet::SequenceId sequence {};
        simnet::WorldSnapshot snapshot {};
    };

    constexpr std::size_t retained_snapshot_limit = 64;

#if defined(SIMNET_ENABLE_RENDER)
    [[nodiscard]] std::optional<simnet::GameCameraView> player_game_camera(
        simnet::WorldSnapshot const& snapshot,
        simnet::EntityNetId player_id
    )
    {
        if (player_id == 0U) {
            return std::nullopt;
        }
        auto const found = std::ranges::find(snapshot.ids, player_id);
        if (found == snapshot.ids.end()) {
            return std::nullopt;
        }
        auto const offset = static_cast<std::size_t>(
            std::distance(snapshot.ids.begin(), found)
        );
        auto const pose = simnet::app::locked_chase_camera_pose(
            snapshot.positions[offset],
            snapshot.headings[offset]
        );
        return simnet::GameCameraView {
            .position = pose.position,
            .target = pose.target,
            .up = pose.up,
            .vertical_fov_degrees = 70.0F,
        };
    }

    [[nodiscard]] simnet::app::PlayerInputMessage player_input_message(
        simnet::PlayerViewInput input
    ) noexcept
    {
        auto buttons = std::uint8_t {};
        auto const set = [&buttons](bool down, simnet::app::PlayerButton button) {
            if (down) {
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
        return { .buttons = buttons };
    }

    struct ClientPresentationState
    {
        simnet::Tick observed_tick {};
        simnet::Nanoseconds elapsed {};
        simnet::WorldSnapshot interpolated {};
    };

    [[nodiscard]] simnet::WorldSnapshot const* presentation_snapshot(
        std::deque<RetainedClientSnapshot> const& history,
        ClientPresentationState& state,
        simnet::Nanoseconds frame_delta,
        double tick_rate_hz,
        bool interpolation_enabled,
        bool paused,
        double& alpha
    )
    {
        alpha = 1.0;
        if (history.empty()) {
            return nullptr;
        }

        auto const& current = history.back();
        if (state.observed_tick != current.snapshot.tick) {
            state.observed_tick = current.snapshot.tick;
            state.elapsed = {};
        } else if (!paused) {
            state.elapsed += std::max(frame_delta, simnet::Nanoseconds {});
        }
        if (!interpolation_enabled || paused || history.size() < 2U) {
            return &current.snapshot;
        }

        auto const& previous = history[history.size() - 2U];
        if (previous.snapshot.tick >= current.snapshot.tick || tick_rate_hz <= 0.0) {
            return &current.snapshot;
        }
        auto const tick_span = current.snapshot.tick - previous.snapshot.tick;
        auto const interval_seconds = static_cast<double>(tick_span) / tick_rate_hz;
        auto const elapsed_seconds =
            std::chrono::duration<double>(state.elapsed).count();
        alpha = std::clamp(elapsed_seconds / interval_seconds, 0.0, 1.0);
        SIMNET_TRACE_SCOPE_CATEGORY("client.presentation.interpolate", simnet::LogCategory::Render);
        auto const interpolated = simnet::interpolate_world_snapshots(
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
        switch (cause) {
        case ClientStopCause::None: return "none";
        case ClientStopCause::ConnectionTimeout: return "connection_timeout";
        case ClientStopCause::RemoteShutdown: return "remote_shutdown";
        case ClientStopCause::ConnectionLost: return "connection_lost";
        case ClientStopCause::ProtocolError: return "protocol_error";
        }
        return "unknown";
    }

    [[nodiscard]] simnet::app::ClientRole configured_role(std::string_view role)
    {
        if (role == "stationary_observer") {
            return simnet::app::ClientRole::StationaryObserver;
        }
        if (role == "player") {
            return simnet::app::ClientRole::Player;
        }
        throw std::runtime_error("unsupported gameplay role: " + std::string { role });
    }

#if defined(SIMNET_ENABLE_RENDER)
    [[nodiscard]] std::string_view client_connection_state_name(ClientConnectionState state) noexcept
    {
        switch (state) {
        case ClientConnectionState::Connecting: return "connecting";
        case ClientConnectionState::SessionReady: return "session ready";
        case ClientConnectionState::Disconnected: return "disconnected";
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
            .stationary_observer_interest_radius =
                config.stationary_observer_interest_radius,
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
        simnet::SnapshotAck const& ack,
        std::string_view role,
        std::optional<simnet::StationaryObserverView> stationary_observer,
        std::optional<simnet::GameCameraView> game_camera,
        simnet::RunSetupView setup
    )
    {
        auto replication = std::optional<simnet::RenderReplicationInfo> {};
        if (ack.newest_received_snapshot != 0 || ack.newest_applied_snapshot != 0) {
            replication = simnet::RenderReplicationInfo {
                .latest_received_sequence = ack.newest_received_snapshot != 0
                    ? std::optional<simnet::SequenceId> { ack.newest_received_snapshot }
                    : std::optional<simnet::SequenceId> {},
                .latest_applied_sequence = ack.newest_applied_snapshot != 0
                    ? std::optional<simnet::SequenceId> { ack.newest_applied_snapshot }
                    : std::optional<simnet::SequenceId> {},
                .latest_snapshot_tick = snapshot.tick,
            };
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
                .snapshot_sequence = sequence,
                .session_ready = session_ready,
                .world_bounds = simnet::make_centered_bounds(config.simulation.world_half),
                .frame_delta = frame_delta,
                .fixed_tick_rate_hz = config.simulation.tick_rate_hz,
                .simulation_paused = simulation_paused,
                .interpolation = interpolation,
                .context = {
                    .kind = simnet::ViewerKind::Client,
                    .client_role = role,
                },
                .capabilities = {
                    .can_pause_simulation = session_ready && simulation_paused.has_value(),
                    .has_networking = true,
                    .has_stationary_observer = role == "stationary observer",
                    .has_game_camera = role == "player",
                },
                .connection = std::optional<simnet::RenderConnectionInfo> {
                    simnet::RenderConnectionInfo {
                        .state = client_connection_state_name(connection_state),
                        .peer = peer,
                    }
                },
                .replication = std::move(replication),
            },
            .stationary_observer = std::move(stationary_observer),
            .game_camera = std::move(game_camera),
            .setup = setup,
        };
    }
#endif

    [[nodiscard]] ClientOptions parse_options(int argc, char** argv)
    {
        auto options = ClientOptions {};
        for (auto index = 1; index < argc; ++index) {
            auto const option = std::string_view { argv[index] };
            if (option == "--config") {
                options.config_path = std::filesystem::path {
                    simnet::app::next_option_value(index, argc, argv, option)
                };
            } else if (option == "--shared-config") {
                options.shared_config_path = std::filesystem::path {
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
            } else {
                throw std::runtime_error("unknown client option: " + std::string { option });
            }
        }
        return options;
    }

    [[nodiscard]] bool record_received_snapshot(
        SnapshotAckTracker& tracker,
        simnet::SequenceId sequence
    ) noexcept
    {
        auto const previous = tracker.value.newest_received_snapshot;
        if (sequence == 0 || sequence <= previous) {
            return false;
        }
        if (previous == 0) {
            tracker.value.newest_received_snapshot = sequence;
            tracker.value.received_mask = 0;
            return true;
        }

        auto const shift = sequence - previous;
        if (shift >= 33U) {
            tracker.value.received_mask = 0;
        } else {
            auto const shifted_history = shift == 32U
                ? 0U
                : tracker.value.received_mask << shift;
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
        auto const found = std::ranges::find(
            history,
            sequence,
            &RetainedClientSnapshot::sequence
        );
        return found == history.end() ? nullptr : &found->snapshot;
    }

    void retain_snapshot(
        std::deque<RetainedClientSnapshot>& history,
        simnet::SequenceId sequence,
        simnet::WorldSnapshot snapshot
    )
    {
        if (history.size() == retained_snapshot_limit) {
            history.pop_front();
        }
        history.push_back({
            .sequence = sequence,
            .snapshot = std::move(snapshot),
        });
    }

    [[nodiscard]] simnet::SnapshotUpdate make_full_replace_patch(
        simnet::WorldSnapshot const& snapshot
    )
    {
        auto patch = simnet::SnapshotUpdate {
            .tick = snapshot.tick,
            .kind = simnet::SnapshotKind::FullReplace,
        };
        patch.upserts.reserve(snapshot.size());
        for (std::size_t index = 0; index < snapshot.size(); ++index) {
            patch.upserts.push_back({
                .id = snapshot.ids[index],
                .position = snapshot.positions[index],
                .heading = snapshot.headings[index],
                .hue = snapshot.hues[index],
            });
        }
        return patch;
    }

    [[nodiscard]] bool apply_packet(
        simnet::ReceivedPacket const& packet,
        simnet::PipelineDefinition const& pipeline,
        simnet::ClientReplicationState& decode_state,
        simnet::PipelineScratch& scratch,
        simnet::SequenceId& latest_applied_sequence,
        SnapshotAckTracker& ack_tracker,
        std::deque<RetainedClientSnapshot>& snapshot_history,
        flecs::world& world,
        simnet::TransportClient& transport,
        simnet::RuntimeStats& stats
    )
    {
        if (packet.lane != simnet::Lane::Snapshot) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Warn,
                "client ignored payload on non-snapshot lane");
            return true;
        }

        auto decoded = simnet::DecodeOutput {};
        {
            SIMNET_TRACE_SCOPE_CATEGORY("client.snapshot_decode", simnet::LogCategory::Pipeline);
            decoded = simnet::decode_update(
                pipeline,
                decode_state,
                scratch,
                {
                    .bytes = packet.payload,
                }
            );
        }
        if (!decoded.report.valid) {
            simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Error,
                "client snapshot decode failed: " + decoded.report.error);
            return false;
        }
        if (!record_received_snapshot(ack_tracker, decoded.report.sequence)) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Warn,
                "client ignored stale snapshot sequence=" + std::to_string(decoded.report.sequence));
            return true;
        }

        auto const empty_baseline = simnet::WorldSnapshot {};
        auto const* baseline = static_cast<simnet::WorldSnapshot const*>(nullptr);
        if (decoded.report.delta) {
            baseline = find_retained_snapshot(snapshot_history, decoded.report.baseline_sequence);
            if (baseline == nullptr) {
                simnet::log(simnet::LogCategory::Snapshot, simnet::LogLevel::Error,
                    "client delta baseline is not retained sequence="
                        + std::to_string(decoded.report.baseline_sequence));
                return false;
            }
        } else if (decoded.update.kind == simnet::SnapshotKind::Patch) {
            baseline = snapshot_history.empty()
                ? &empty_baseline
                : &snapshot_history.back().snapshot;
        }

        auto reconstructed = simnet::WorldSnapshot {};
        auto const reconstruction = simnet::reconstruct_world_snapshot(
            baseline,
            decoded.update,
            reconstructed
        );
        if (!reconstruction.valid) {
            simnet::log(simnet::LogCategory::Snapshot, simnet::LogLevel::Error,
                "client snapshot reconstruction failed: " + reconstruction.message);
            return false;
        }

        auto const baseline_is_current = baseline != nullptr
            && !snapshot_history.empty()
            && baseline == &snapshot_history.back().snapshot;
        auto replacement = simnet::SnapshotUpdate {};
        auto const* patch_to_apply = &decoded.update;
        if (decoded.update.kind == simnet::SnapshotKind::Patch && !baseline_is_current) {
            replacement = make_full_replace_patch(reconstructed);
            patch_to_apply = &replacement;
        }

        auto applied = simnet::ApplyPatchReport {};
        {
            SIMNET_TRACE_SCOPE_CATEGORY("client.snapshot_apply", simnet::LogCategory::Simulation);
            applied = simnet::apply_client_snapshot_patch(world, *patch_to_apply);
        }
        if (!applied.valid) {
            simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Error,
                "client snapshot apply failed: " + applied.error);
            return false;
        }

        latest_applied_sequence = decoded.report.sequence;
        ack_tracker.value.newest_applied_snapshot = decoded.report.sequence;
        retain_snapshot(
            snapshot_history,
            decoded.report.sequence,
            std::move(reconstructed)
        );
        stats.ticks = applied.tick;
        auto sent = simnet::TransportResult {};
        {
            SIMNET_TRACE_SCOPE_CATEGORY("client.snapshot_ack", simnet::LogCategory::Transport);
            sent = transport.send_snapshot_ack(ack_tracker.value);
        }
        if (!sent.ok) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                "client snapshot ACK send failed: " + sent.error.message);
            return false;
        }

        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Debug,
            "client snapshot applied tick=" + std::to_string(applied.tick)
                + " sequence=" + std::to_string(decoded.report.sequence)
                + " entities=" + std::to_string(applied.final_entities));
        return true;
    }

    [[nodiscard]] bool apply_application_control(
        simnet::ReceivedApplicationControl const& control,
        simnet::app::ClientRole requested_role,
        flecs::world& world,
        bool& simulation_paused,
        bool& pause_state_received,
        bool& join_accepted,
        simnet::EntityNetId& player_id
    )
    {
        auto message = simnet::app::AppMessage {};
        if (!simnet::app::decode_app_message(control.payload, message)) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                "client received invalid application-control message");
            return false;
        }

        if (message.kind == simnet::app::AppMessageKind::JoinAccepted
            && !join_accepted
            && message.role == requested_role) {
            join_accepted = true;
            player_id = message.player_id;
            simnet::set_client_player_entity_id(world, player_id);
            simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
                "client join accepted role="
                    + std::string { requested_role == simnet::app::ClientRole::Player
                        ? "player" : "stationary_observer" }
                    + " player_id=" + std::to_string(player_id));
            return true;
        }
        if (message.kind != simnet::app::AppMessageKind::PauseState) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                "client received unauthorized or duplicate application-control message");
            return false;
        }

        auto const changed =
            !pause_state_received || simulation_paused != message.paused;
        simulation_paused = message.paused;
        pause_state_received = true;
        if (changed) {
            simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
                simulation_paused ? "client received authoritative pause state" : "client received authoritative resume state");
        }
        return true;
    }

}

namespace simnet::app
{
    int run_client(int argc, char** argv)
    {
        try {
            auto const options = parse_options(argc, argv);
            auto const shared_config_source =
                options.shared_config_path.value_or(default_shared_config_path());
            auto const local_config_source =
                options.config_path.value_or(default_client_config_path());
            auto const shared = load_shared_config(shared_config_source);
            auto const local = load_client_config(local_config_source);
            auto const requested_role = configured_role(local.gameplay.role);
            auto telemetry = TelemetryLifetime { local.telemetry };
#if defined(SIMNET_ENABLE_TRACY)
            log(LogCategory::Telemetry, LogLevel::Info, "Tracy instrumentation compiled in");
#else
            log(LogCategory::Telemetry, LogLevel::Info, "Tracy instrumentation not compiled in");
#endif
            auto signals = SignalHandlers {};
            auto const pipeline = make_snapshot_pipeline(shared, local.transport);
#if defined(SIMNET_ENABLE_RENDER)
            auto const run_setup = RunSetupStorage {
                shared,
                local,
                pipeline,
                shared_config_source,
                local_config_source,
            };
#endif

            auto transport = TransportClient {};
            auto const connected = transport.connect({
                .server_address = local.transport.host,
                .server_port = local.transport.port,
                .identity = make_session_identity(shared, pipeline),
                .limits = {
                    .max_payload_bytes = local.transport.max_payload_bytes,
                    .size_policy = transport_send_size_policy(local.transport),
                },
            });
            if (!connected.ok) {
                log(LogCategory::Transport, LogLevel::Error,
                    "client transport connect failed: " + connected.error.message);
                return 1;
            }

            auto const settings = RuntimeSettings {
                .max_frames = options.max_frames,
                .max_ticks = options.max_ticks,
                .max_runtime = options.max_runtime,
            };
            auto world = flecs::world {};
            register_client_game(world);
#if defined(SIMNET_ENABLE_RENDER)
            auto viewer = std::optional<Viewer> {};
            auto presentation = ClientPresentationState {};
            auto empty_presentation = WorldSnapshot {};
            auto stationary_observer = std::optional<StationaryObserverState> {};
            if (requested_role == app::ClientRole::StationaryObserver) {
                stationary_observer = StationaryObserverState {
                    .position = {},
                    .interest_radius =
                        local.visualization.stationary_observer_interest_radius,
                    .vertical_fov_degrees =
                        local.visualization.stationary_observer_vertical_fov_degrees,
                };
            }
            if (local.visualization.enabled) {
                viewer.emplace(
                    viewer_config(local.visualization),
                    local.telemetry.log_directory
                );
            }
#endif

            auto stats = RuntimeStats {};
            auto timer = RuntimeFrameTimer {};
            reset_frame_timer(timer);
            auto stop = StopRequest {};
            auto events = std::vector<TransportEvent> {};
            auto decode_state = ClientReplicationState {};
            auto scratch = PipelineScratch {};
            auto latest_applied_sequence = SequenceId {};
            auto ack_tracker = SnapshotAckTracker {};
            auto snapshot_history = std::deque<RetainedClientSnapshot> {};
            auto connection_state = ClientConnectionState::Connecting;
            auto stop_cause = ClientStopCause::None;
            auto server_peer = std::optional<PeerId> {};
            auto authoritative_pause_state = false;
            auto pause_state_received = false;
            auto join_accepted = false;
            auto player_id = EntityNetId {};
#if defined(SIMNET_ENABLE_RENDER)
            auto game_view_initialized = false;
            auto player_input_was_active = false;
#endif

            log(LogCategory::Simulation, LogLevel::Info, "client runtime started");
            while (!stop.requested()) {
                if (signal_stop_requested()) {
                    static_cast<void>(stop.request(ShutdownReason::Signal));
                    break;
                }

                auto const delta = sample_frame_delta(timer);
                ++stats.frames;
                stats.raw_time += delta;
                stats.accepted_time += delta;

                events.clear();
                auto polled = TransportResult {};
                {
                    SIMNET_TRACE_SCOPE_CATEGORY("client.transport_poll", LogCategory::Transport);
                    polled = transport.poll(events, 1);
                }
                if (!polled.ok) {
                    log(LogCategory::Transport, LogLevel::Error,
                        "client transport poll failed: " + polled.error.message);
                    stop_cause = ClientStopCause::ProtocolError;
                    static_cast<void>(stop.request(ShutdownReason::FatalError));
                    break;
                }

                for (auto const& event : events) {
                    if (auto const* ready = std::get_if<PeerSessionReady>(&event)) {
                        connection_state = ClientConnectionState::SessionReady;
                        server_peer = ready->peer;
                        pause_state_received = false;
                        join_accepted = false;
                        player_id = 0U;
#if defined(SIMNET_ENABLE_RENDER)
                        game_view_initialized = false;
                        player_input_was_active = false;
#endif
                        log(LogCategory::Transport, LogLevel::Info,
                            "client session ready peer=" + std::to_string(ready->peer));
                        auto const joined = transport.send_application_control(
                            app::encode_app_message({
                                .kind = app::AppMessageKind::JoinRequest,
                                .role = requested_role,
                            })
                        );
                        if (!joined.ok) {
                            log(LogCategory::Transport, LogLevel::Error,
                                "client join request send failed: " + joined.error.message);
                            stop_cause = ClientStopCause::ProtocolError;
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                            break;
                        }
                    } else if (auto const* packet = std::get_if<ReceivedPacket>(&event)) {
                        if (connection_state != ClientConnectionState::SessionReady || !apply_packet(
                                *packet,
                                pipeline,
                                decode_state,
                                scratch,
                                latest_applied_sequence,
                                ack_tracker,
                                snapshot_history,
                                world,
                                transport,
                                stats
                            )) {
                            stop_cause = ClientStopCause::ProtocolError;
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                            break;
                        }
                    } else if (auto const* control = std::get_if<ReceivedApplicationControl>(&event)) {
                        if (connection_state != ClientConnectionState::SessionReady || !apply_application_control(
                                *control,
                                requested_role,
                                world,
                                authoritative_pause_state,
                                pause_state_received,
                                join_accepted,
                                player_id
                            )) {
                            stop_cause = ClientStopCause::ProtocolError;
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                            break;
                        }
                    } else if (auto const* disconnected = std::get_if<PeerDisconnected>(&event)) {
                        auto const was_session_ready = connection_state == ClientConnectionState::SessionReady;
                        connection_state = ClientConnectionState::Disconnected;
                        if (!was_session_ready && disconnected->code == DisconnectCode::Timeout) {
                            stop_cause = ClientStopCause::ConnectionTimeout;
                            log(LogCategory::Transport, LogLevel::Error,
                                "client connection or session handshake timed out peer="
                                    + std::to_string(disconnected->peer));
                        } else if (was_session_ready && disconnected->code == DisconnectCode::None) {
                            stop_cause = ClientStopCause::RemoteShutdown;
                            log(LogCategory::Transport, LogLevel::Info,
                                "client received remote shutdown peer=" + std::to_string(disconnected->peer));
                        } else if (was_session_ready) {
                            stop_cause = ClientStopCause::ConnectionLost;
                            log(LogCategory::Transport, LogLevel::Error,
                                "client connection lost peer=" + std::to_string(disconnected->peer));
                        } else {
                            stop_cause = ClientStopCause::ProtocolError;
                            log(LogCategory::Transport, LogLevel::Error,
                                "client disconnected before session readiness peer="
                                    + std::to_string(disconnected->peer));
                        }
                        static_cast<void>(stop.request(
                            stop_cause == ClientStopCause::RemoteShutdown
                                ? ShutdownReason::TransportDisconnected
                                : ShutdownReason::FatalError
                        ));
                        break;
                    } else if (auto const* error = std::get_if<TransportErrorEvent>(&event)) {
                        log(LogCategory::Transport, LogLevel::Error,
                            "client transport error: " + error->message);
                        stop_cause = ClientStopCause::ProtocolError;
                        static_cast<void>(stop.request(ShutdownReason::FatalError));
                        break;
                    }
                }

#if defined(SIMNET_ENABLE_RENDER)
                if (!stop.requested() && viewer.has_value()) {
                    auto interpolation_alpha = 1.0;
                    auto const* displayed_snapshot = presentation_snapshot(
                        snapshot_history,
                        presentation,
                        delta,
                        shared.simulation.tick_rate_hz,
                        local.visualization.interpolation_enabled,
                        pause_state_received && authoritative_pause_state,
                        interpolation_alpha
                    );
                    if (displayed_snapshot == nullptr && snapshot_history.empty()) {
                        displayed_snapshot = &empty_presentation;
                    }
                    if (!snapshot_history.empty() && displayed_snapshot == nullptr) {
                        log(LogCategory::Render, LogLevel::Error,
                            "client presentation interpolation failed");
                        stop_cause = ClientStopCause::ProtocolError;
                        static_cast<void>(stop.request(ShutdownReason::FatalError));
                    } else if (displayed_snapshot != nullptr) {
                        auto const has_pair = snapshot_history.size() >= 2U
                            && snapshot_history[snapshot_history.size() - 2U].snapshot.tick
                                < snapshot_history.back().snapshot.tick;
                        auto const interpolation_active =
                            local.visualization.interpolation_enabled
                            && !(pause_state_received && authoritative_pause_state)
                            && has_pair;
                        auto const interpolation = RenderInterpolationInfo {
                            .enabled = local.visualization.interpolation_enabled,
                            .interpolating = interpolation_active,
                            .from_tick = has_pair
                                ? snapshot_history[snapshot_history.size() - 2U].snapshot.tick
                                : displayed_snapshot->tick,
                            .to_tick = snapshot_history.empty()
                                ? displayed_snapshot->tick
                                : snapshot_history.back().snapshot.tick,
                            .alpha = interpolation_active ? interpolation_alpha : 1.0,
                        };
                        SIMNET_TRACE_PLOT(
                            "client.render.interpolation_alpha",
                            interpolation.alpha
                        );
                        auto const sequence = latest_applied_sequence == 0
                            ? std::optional<SequenceId> {}
                            : std::optional<SequenceId> { latest_applied_sequence };
                        auto viewer_result = ViewerResult {};
                        auto game_camera = requested_role == app::ClientRole::Player
                            ? player_game_camera(*displayed_snapshot, player_id)
                            : std::optional<GameCameraView> {};
                        auto stationary_observer_view =
                            std::optional<StationaryObserverView> {};
                        if (stationary_observer.has_value()) {
                            stationary_observer_view = StationaryObserverView {
                                .position = stationary_observer->position,
                                .forward = app::stationary_observer_forward(
                                    *stationary_observer
                                ),
                                .interest_radius = stationary_observer->interest_radius,
                                .vertical_fov_degrees =
                                    stationary_observer->vertical_fov_degrees,
                            };
                        }
                        if (requested_role == app::ClientRole::Player
                            && join_accepted
                            && game_camera.has_value()
                            && !game_view_initialized) {
                            viewer->set_camera_mode(CameraMode::Game);
                            game_view_initialized = true;
                        }
                        {
                            SIMNET_TRACE_SCOPE_CATEGORY("client.viewer_draw", LogCategory::Render);
                            viewer_result = viewer->draw(
                                render_frame(
                                    *displayed_snapshot,
                                    shared,
                                    delta,
                                    sequence,
                                    connection_state == ClientConnectionState::SessionReady,
                                    pause_state_received
                                        ? std::optional<bool> { authoritative_pause_state }
                                        : std::optional<bool> {},
                                    interpolation,
                                    connection_state,
                                    server_peer,
                                    ack_tracker.value,
                                    requested_role == app::ClientRole::Player
                                        ? std::string_view { "player" }
                                        : std::string_view { "stationary observer" },
                                    std::move(stationary_observer_view),
                                    std::move(game_camera),
                                    run_setup.view()
                                )
                            );
                        }
                        if (viewer_result.close_requested) {
                            static_cast<void>(stop.request(ShutdownReason::WindowClosed));
                        }
                        if (stationary_observer.has_value()) {
                            apply_stationary_observer_rotation(
                                *stationary_observer,
                                viewer_result.stationary_observer_yaw_axis,
                                viewer_result.stationary_observer_pitch_axis,
                                delta
                            );
                        }
                        if (viewer_result.toggle_simulation_pause_requested && pause_state_received) {
                            auto const bytes = app::encode_app_message({
                                .kind = app::AppMessageKind::PauseSetRequest,
                                .paused = !authoritative_pause_state,
                            });
                            auto const sent = transport.send_application_control(bytes);
                            if (!sent.ok) {
                                log(LogCategory::Transport, LogLevel::Error,
                                    "client pause request send failed: " + sent.error.message);
                                stop_cause = ClientStopCause::ProtocolError;
                                static_cast<void>(stop.request(ShutdownReason::FatalError));
                            }
                        }
                        if (!stop.requested()
                            && requested_role == app::ClientRole::Player
                            && join_accepted) {
                            auto const input_active =
                                viewer_result.camera_mode == CameraMode::Game;
                            auto const input = input_active
                                ? player_input_message(viewer_result.player_input)
                                : app::PlayerInputMessage {};
                            if (input_active || player_input_was_active) {
                                auto const sent = transport.send_application_input(
                                    app::encode_player_input(input)
                                );
                                if (!sent.ok) {
                                    log(LogCategory::Transport, LogLevel::Error,
                                        "client player-input send failed: "
                                            + sent.error.message);
                                    stop_cause = ClientStopCause::ProtocolError;
                                    static_cast<void>(stop.request(ShutdownReason::FatalError));
                                }
                            }
                            player_input_was_active = input_active;
                        }
                    }
                }
#endif

                if (!stop.requested()) {
                    auto const limit = reached_runtime_limit(settings, stats);
                    if (limit != ShutdownReason::None) {
                        static_cast<void>(stop.request(limit));
                    }
                }
                SIMNET_TRACE_FRAME("client");
            }

            transport.disconnect(DisconnectCode::None);
            log(LogCategory::Simulation, LogLevel::Info,
                "client runtime stopped reason=" + std::string { shutdown_reason_name(stop.reason()) }
                    + " cause=" + std::string { client_stop_cause_name(stop_cause) }
                    + " frames=" + std::to_string(stats.frames)
                    + " ticks=" + std::to_string(stats.ticks)
                    + " runtime_ns=" + std::to_string(stats.raw_time.count()));
            return stop.reason() == ShutdownReason::FatalError ? 1 : 0;
        } catch (std::exception const& error) {
            std::cerr << "Client failed: " << error.what() << '\n';
            return 1;
        }
    }
}
