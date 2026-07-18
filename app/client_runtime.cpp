#include "client_runtime.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <flecs.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <simnet/telemetry_trace.hpp>

import simnet.config;
import simnet.app_common;
import simnet.core;
import simnet.game_client;
import simnet.pipeline;
import simnet.runtime;
import simnet.snapshot;
import simnet.telemetry;
import simnet.transport;

namespace
{
    struct ClientOptions
    {
        std::uint64_t max_frames {};
        simnet::Tick max_ticks {};
        simnet::NS max_runtime {};
    };

    struct SnapshotAckTracker
    {
        simnet::SnapshotAck value {};
    };

    [[nodiscard]] ClientOptions parse_options(int argc, char** argv)
    {
        auto options = ClientOptions {};
        for (auto index = 1; index < argc; ++index) {
            auto const option = std::string_view { argv[index] };
            if (option == "--max-frames") {
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

    [[nodiscard]] bool apply_packet(
        simnet::ReceivedPacket const& packet,
        simnet::PipelineDefinition const& pipeline,
        simnet::ClientReplicationState& decode_state,
        simnet::PipelineScratch& scratch,
        simnet::SequenceId& latest_applied_sequence,
        SnapshotAckTracker& ack_tracker,
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

        auto decoded = simnet::decode_packet(
            pipeline,
            decode_state,
            scratch,
            {
                .bytes = packet.payload,
                .applied_baseline_sequence = latest_applied_sequence,
            }
        );
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

        auto const applied = simnet::apply_client_snapshot_patch(world, decoded.patch);
        if (!applied.valid) {
            simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Error,
                "client snapshot apply failed: " + applied.error);
            return false;
        }

        latest_applied_sequence = decoded.report.sequence;
        ack_tracker.value.newest_applied_snapshot = decoded.report.sequence;
        stats.ticks = applied.tick;
        auto const sent = transport.send_snapshot_ack(ack_tracker.value);
        if (!sent.ok) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                "client snapshot ACK send failed: " + sent.error.message);
            return false;
        }

        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
            "client snapshot applied tick=" + std::to_string(applied.tick)
                + " sequence=" + std::to_string(decoded.report.sequence)
                + " entities=" + std::to_string(applied.final_entities));
        return true;
    }

}

namespace simnet::app
{
    int run_client(int argc, char** argv)
    {
        try {
            auto const options = parse_options(argc, argv);
            auto const shared = load_shared_config(default_shared_config_path());
            auto const local = load_client_config(default_client_config_path());
            auto telemetry = TelemetryLifetime { local.telemetry };
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

            auto stats = RuntimeStats {};
            auto timer = RuntimeFrameTimer {};
            reset_frame_timer(timer);
            auto stop = StopRequest {};
            auto events = std::vector<TransportEvent> {};
            auto decode_state = ClientReplicationState {};
            auto scratch = PipelineScratch {};
            auto latest_applied_sequence = SequenceId {};
            auto ack_tracker = SnapshotAckTracker {};
            auto session_ready = false;

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
                auto const polled = transport.poll(events, 1);
                if (!polled.ok) {
                    log(LogCategory::Transport, LogLevel::Error,
                        "client transport poll failed: " + polled.error.message);
                    static_cast<void>(stop.request(ShutdownReason::FatalError));
                    break;
                }

                for (auto const& event : events) {
                    if (auto const* ready = std::get_if<PeerSessionReady>(&event)) {
                        session_ready = true;
                        log(LogCategory::Transport, LogLevel::Info,
                            "client session ready peer=" + std::to_string(ready->peer));
                    } else if (auto const* packet = std::get_if<ReceivedPacket>(&event)) {
                        if (!session_ready || !apply_packet(
                                *packet,
                                pipeline,
                                decode_state,
                                scratch,
                                latest_applied_sequence,
                                ack_tracker,
                                world,
                                transport,
                                stats
                            )) {
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                            break;
                        }
                    } else if (auto const* disconnected = std::get_if<PeerDisconnected>(&event)) {
                        log(LogCategory::Transport, LogLevel::Info,
                            "client disconnected peer=" + std::to_string(disconnected->peer));
                        static_cast<void>(stop.request(
                            session_ready ? ShutdownReason::TransportDisconnected
                                          : ShutdownReason::FatalError
                        ));
                        break;
                    } else if (auto const* error = std::get_if<TransportErrorEvent>(&event)) {
                        log(LogCategory::Transport, LogLevel::Error,
                            "client transport error: " + error->message);
                        static_cast<void>(stop.request(ShutdownReason::FatalError));
                        break;
                    }
                }

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
