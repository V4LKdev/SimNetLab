#include "client_runtime.hpp"

#include <algorithm>
#include <chrono>
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
        simnet::NS max_runtime {};
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
            .debug_observer_interest_radius = config.debug_observer_interest_radius,
            .debug_observer_vertical_fov_degrees = config.debug_observer_vertical_fov_degrees,
            .max_visible_spatial_cells = config.max_visible_spatial_cells,
            .entity_mesh_path = config.entity_mesh_path,
            .title = "SimNet Client",
        };
    }

    [[nodiscard]] simnet::RenderFrame render_frame(
        simnet::WorldSnapshot const& snapshot,
        simnet::SharedConfig const& config,
        simnet::NS frame_delta,
        std::optional<simnet::SequenceId> sequence,
        bool session_ready,
        std::optional<bool> simulation_paused,
        ClientConnectionState connection_state,
        std::optional<simnet::PeerId> peer,
        simnet::SnapshotAck const& ack,
        simnet::app::DebugObserverState const& observer
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
                .capabilities = { .can_pause_simulation = session_ready && simulation_paused.has_value() },
                .connection = std::optional<simnet::RenderConnectionInfo> {
                    simnet::RenderConnectionInfo {
                        .state = client_connection_state_name(connection_state),
                        .peer = peer,
                    }
                },
                .replication = std::move(replication),
            },
            .observer = simnet::ObserverView {
                .position = observer.position,
                .forward = simnet::app::debug_observer_forward(observer),
                .interest_radius = observer.interest_radius,
                .vertical_fov_degrees = observer.vertical_fov_degrees,
            },
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

    [[nodiscard]] simnet::ClientSnapshotPatch make_full_replace_patch(
        simnet::WorldSnapshot const& snapshot
    )
    {
        auto patch = simnet::ClientSnapshotPatch {
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
            decoded = simnet::decode_packet(
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
        } else if (decoded.patch.kind == simnet::SnapshotKind::Patch) {
            baseline = snapshot_history.empty()
                ? &empty_baseline
                : &snapshot_history.back().snapshot;
        }

        auto reconstructed = simnet::WorldSnapshot {};
        auto const reconstruction = simnet::reconstruct_world_snapshot(
            baseline,
            decoded.patch,
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
        auto replacement = simnet::ClientSnapshotPatch {};
        auto const* patch_to_apply = &decoded.patch;
        if (decoded.patch.kind == simnet::SnapshotKind::Patch && !baseline_is_current) {
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
        bool& simulation_paused,
        bool& pause_state_received
    )
    {
        auto message = simnet::app::AppMessage {};
        if (!simnet::app::decode_app_message(control.payload, message)
            || message.kind != simnet::app::AppMessageKind::PauseState) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                "client received invalid application-control message");
            return false;
        }

        auto const changed = !pause_state_received || simulation_paused != message.paused;
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
            auto const shared = load_shared_config(
                options.shared_config_path.value_or(default_shared_config_path())
            );
            auto const local = load_client_config(options.config_path.value_or(default_client_config_path()));
            auto telemetry = TelemetryLifetime { local.telemetry };
#if defined(SIMNET_ENABLE_TRACY)
            log(LogCategory::Telemetry, LogLevel::Info, "Tracy instrumentation compiled in");
#else
            log(LogCategory::Telemetry, LogLevel::Info, "Tracy instrumentation not compiled in");
#endif
            auto signals = SignalHandlers {};
            auto const pipeline = make_snapshot_pipeline(shared, local.transport);

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
            auto render_snapshot = WorldSnapshot {};
            auto debug_observer = DebugObserverState {
                .position = {},
                .interest_radius = local.visualization.debug_observer_interest_radius,
                .vertical_fov_degrees = local.visualization.debug_observer_vertical_fov_degrees,
            };
            if (local.visualization.enabled) {
                viewer.emplace(viewer_config(local.visualization));
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
                        log(LogCategory::Transport, LogLevel::Info,
                            "client session ready peer=" + std::to_string(ready->peer));
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
                                authoritative_pause_state,
                                pause_state_received
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
                    auto extracted = ClientSnapshotExtractionReport {};
                    {
                        SIMNET_TRACE_SCOPE_CATEGORY("client.render_snapshot_extraction", LogCategory::Snapshot);
                        extracted = extract_client_world_snapshot(world, stats.ticks, render_snapshot);
                    }
                    if (!extracted.valid) {
                        log(LogCategory::Simulation, LogLevel::Error,
                            "client render snapshot extraction failed: " + extracted.error);
                        stop_cause = ClientStopCause::ProtocolError;
                        static_cast<void>(stop.request(ShutdownReason::FatalError));
                    } else {
                        auto const sequence = latest_applied_sequence == 0
                            ? std::optional<SequenceId> {}
                            : std::optional<SequenceId> { latest_applied_sequence };
                        auto viewer_result = ViewerResult {};
                        {
                            SIMNET_TRACE_SCOPE_CATEGORY("client.viewer_draw", LogCategory::Render);
                            viewer_result = viewer->draw(
                                render_frame(
                                    render_snapshot,
                                    shared,
                                    delta,
                                    sequence,
                                    connection_state == ClientConnectionState::SessionReady,
                                    pause_state_received
                                        ? std::optional<bool> { authoritative_pause_state }
                                        : std::optional<bool> {},
                                    connection_state,
                                    server_peer,
                                    ack_tracker.value,
                                    debug_observer
                                )
                            );
                        }
                        if (viewer_result.close_requested) {
                            static_cast<void>(stop.request(ShutdownReason::WindowClosed));
                        }
                        apply_debug_observer_rotation(
                            debug_observer,
                            viewer_result.debug_observer_yaw_axis,
                            viewer_result.debug_observer_pitch_axis,
                            delta
                        );
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
