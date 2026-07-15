#include <chrono>
#include <cstdint>
#include <exception>
#include <flecs.h>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include <simnet/telemetry_trace.hpp>

import simnet.config;
import simnet.core;
import simnet.game_client;
import simnet.pipeline;
import simnet.snapshot;
import simnet.telemetry;
import simnet.transport;

#include "app_smoke.hpp"

namespace
{
    constexpr auto handshake_timeout = std::chrono::seconds(2);
    constexpr auto final_ack_drain_time = std::chrono::milliseconds(50);

    struct SnapshotAckTracker
    {
        simnet::SnapshotAck value {};
    };

    [[nodiscard]] std::string snapshot_kind_name(simnet::SnapshotKind kind)
    {
        switch (kind) {
        case simnet::SnapshotKind::FullReplace:
            return "FullReplace";
        case simnet::SnapshotKind::Patch:
            return "Patch";
        }
        return "Unknown";
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

    [[nodiscard]] bool drain_final_ack(simnet::TransportClient& transport)
    {
        auto events = std::vector<simnet::TransportEvent> {};
        auto const deadline = std::chrono::steady_clock::now() + final_ack_drain_time;
        while (std::chrono::steady_clock::now() < deadline) {
            events.clear();
            auto const poll = transport.poll(events, 10);
            if (!poll.ok) {
                simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                    "client final ACK drain failed: " + poll.error.message);
                return false;
            }
            for (auto const& event : events) {
                if (std::holds_alternative<simnet::PeerDisconnected>(event)) {
                    return true;
                }
                if (auto const* error = std::get_if<simnet::TransportErrorEvent>(&event)) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                        "client transport error during final ACK drain: " + error->message);
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] bool wait_for_session(simnet::TransportClient& transport)
    {
        auto events = std::vector<simnet::TransportEvent> {};
        auto const deadline = std::chrono::steady_clock::now() + handshake_timeout;

        while (std::chrono::steady_clock::now() < deadline) {
            events.clear();
            auto const poll = transport.poll(events, 10);
            if (!poll.ok) {
                simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                    "client transport poll failed: " + poll.error.message);
                return false;
            }

            for (auto const& event : events) {
                if (auto const* value = std::get_if<simnet::PeerConnected>(&event)) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Info,
                        "client transport peer connected peer=" + std::to_string(value->peer));
                } else if (auto const* value = std::get_if<simnet::PeerSessionReady>(&event)) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Info,
                        "client transport session ready peer=" + std::to_string(value->peer));
                    return true;
                } else if (auto const* value = std::get_if<simnet::PeerDisconnected>(&event)) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                        "client transport disconnected before session ready peer=" + std::to_string(value->peer)
                            + " code=" + std::to_string(static_cast<std::uint16_t>(value->code)));
                    return false;
                } else if (auto const* value = std::get_if<simnet::TransportErrorEvent>(&event)) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                        "client transport error: " + value->message);
                    return false;
                } else if (std::holds_alternative<simnet::ReceivedPacket>(event)) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Warn,
                        "client received unexpected app payload during handshake phase");
                }
            }
        }

        simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
            "client transport session readiness timed out");
        return false;
    }

    [[nodiscard]] bool receive_final_snapshot(
        simnet::TransportClient& transport,
        simnet::PipelineDefinition const& pipeline,
        flecs::world& world
    )
    {
        auto decode_state = simnet::ClientReplicationState {};
        auto pipeline_scratch = simnet::PipelineScratch {};
        auto ack_tracker = SnapshotAckTracker {};
        auto latest_applied_sequence = simnet::SequenceId {};
        auto events = std::vector<simnet::TransportEvent> {};
        auto const deadline = std::chrono::steady_clock::now() + handshake_timeout;

        while (std::chrono::steady_clock::now() < deadline) {
            events.clear();
            auto const poll = transport.poll(events, 10);
            if (!poll.ok) {
                simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                    "client transport poll failed: " + poll.error.message);
                return false;
            }

            for (auto const& event : events) {
                if (auto const* packet = std::get_if<simnet::ReceivedPacket>(&event)) {
                    if (packet->lane != simnet::Lane::Snapshot) {
                        simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                            "client received payload on unexpected lane");
                        return false;
                    }

                    auto decoded = simnet::decode_packet(
                        pipeline,
                        decode_state,
                        pipeline_scratch,
                        {
                            .bytes = packet->payload,
                            .applied_baseline_sequence = latest_applied_sequence,
                        }
                    );
                    if (!decoded.report.valid) {
                        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Error,
                            "client snapshot decode failed: " + decoded.report.error);
                        return false;
                    }
                    if (!record_received_snapshot(ack_tracker, decoded.report.sequence)) {
                        simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                            "client snapshot ACK sequence tracking failed");
                        return false;
                    }

                    auto const applied = simnet::apply_client_snapshot_patch(world, decoded.patch);
                    if (!applied.valid) {
                        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Error,
                            "client snapshot apply failed: " + applied.error);
                        return false;
                    }
                    latest_applied_sequence = decoded.report.sequence;
                    ack_tracker.value.newest_applied_snapshot = decoded.report.sequence;
                    auto const ack_sent = transport.send_snapshot_ack(ack_tracker.value);
                    if (!ack_sent.ok) {
                        simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                            "client snapshot ACK send failed: " + ack_sent.error.message);
                        return false;
                    }

                    simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
                        "client snapshot applied tick=" + std::to_string(applied.tick)
                            + " sequence=" + std::to_string(decoded.report.sequence)
                            + " kind=" + snapshot_kind_name(decoded.report.snapshot_kind)
                            + " baseline=" + std::to_string(decoded.report.baseline_sequence)
                            + " bytes=" + std::to_string(decoded.report.packet_bytes)
                            + " upserts=" + std::to_string(applied.upsert_count)
                            + " deletes=" + std::to_string(applied.delete_count)
                            + " entities=" + std::to_string(applied.final_entities));
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Info,
                        "client snapshot ACK sent received="
                            + std::to_string(ack_tracker.value.newest_received_snapshot)
                            + " mask=" + std::to_string(ack_tracker.value.received_mask)
                            + " applied=" + std::to_string(ack_tracker.value.newest_applied_snapshot));

                    if (applied.tick == simnet::app::smoke_final_tick) {
                        auto const& clock = world.get<simnet::ClientReplicationClock>();
                        auto const& index = world.get<simnet::ClientReplicationIndex>();
                        auto const final_state_valid =
                            latest_applied_sequence == simnet::app::smoke_final_sequence
                            && clock.latest_tick == simnet::app::smoke_final_tick
                            && index.size() == simnet::app::smoke_boid_count
                            && ack_tracker.value.newest_received_snapshot == simnet::app::smoke_final_sequence
                            && ack_tracker.value.received_mask == simnet::app::smoke_final_received_mask
                            && ack_tracker.value.newest_applied_snapshot == simnet::app::smoke_final_sequence;
                        if (!final_state_valid) {
                            simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Error,
                                "client final replicated world validation failed"
                                    " tick=" + std::to_string(clock.latest_tick)
                                    + " entities=" + std::to_string(index.size()));
                        }
                        return final_state_valid && drain_final_ack(transport);
                    }
                } else if (auto const* value = std::get_if<simnet::PeerDisconnected>(&event)) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                        "client transport disconnected before final snapshot peer=" + std::to_string(value->peer));
                    return false;
                } else if (auto const* value = std::get_if<simnet::TransportErrorEvent>(&event)) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                        "client transport error: " + value->message);
                    return false;
                }
            }
        }

        simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
            "client final snapshot timed out");
        return false;
    }
}

int main()
{
    bool telemetry_started = false;
    try {
        SIMNET_TRACE_SCOPE("Client main");

        // Defaults are copied into the active build tree by simnet_config.
        auto const shared_config = simnet::load_shared_config(simnet::default_shared_config_path());
        auto const client_config = simnet::load_client_config(simnet::default_client_config_path());

        simnet::initialize_telemetry(client_config.telemetry);
        telemetry_started = true;

        auto const network_fingerprint = simnet::fingerprint_network_compatibility(shared_config);
        auto const runtime_fingerprint = simnet::fingerprint_runtime_config(shared_config, client_config);
        auto const transport_backend = simnet::app::transport_backend(client_config.transport);

        // Fingerprints give early confirmation that runtime config is coherent.
        simnet::log(simnet::LogCategory::Config, simnet::LogLevel::Info,
            "client network fingerprint: " + std::to_string(network_fingerprint.value));
        simnet::log(simnet::LogCategory::Config, simnet::LogLevel::Info,
            "client runtime fingerprint: " + std::to_string(runtime_fingerprint.value));

        auto const pipeline = simnet::app::make_smoke_pipeline(shared_config);
        auto const session_identity = simnet::app::make_session_identity(shared_config, pipeline);
        auto transport = simnet::TransportClient {};
        auto const connect = transport.connect({
            .backend = transport_backend,
            .server_address = client_config.transport.host,
            .local_ipc_path = client_config.transport.local_ipc_path,
            .server_port = client_config.transport.port,
            .identity = session_identity,
            .limits = simnet::app::transport_limits(client_config.transport),
        });
        if (!connect.ok) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                "client transport connect failed: " + connect.error.message);
            simnet::shutdown_telemetry();
            return 1;
        }

        if (!wait_for_session(transport)) {
            transport.disconnect(simnet::DisconnectCode::None);
            simnet::shutdown_telemetry();
            return 1;
        }

        auto world = flecs::world {};
        simnet::register_client_game(world);
        auto const final_snapshot_applied = receive_final_snapshot(transport, pipeline, world);
        transport.disconnect(simnet::DisconnectCode::None);
        if (!final_snapshot_applied) {
            simnet::shutdown_telemetry();
            return 1;
        }

        SIMNET_TRACE_PLOT("client.network_fingerprint", static_cast<double>(network_fingerprint.value));
        SIMNET_TRACE_PLOT("client.runtime_fingerprint", static_cast<double>(runtime_fingerprint.value));
        SIMNET_TRACE_FRAME("client");
        simnet::flush_telemetry();
        simnet::shutdown_telemetry();
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "Client failed: " << error.what() << '\n';
        if (telemetry_started) {
            simnet::shutdown_telemetry();
        }
        return 1;
    }
}
