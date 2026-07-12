#include <algorithm>
#include <chrono>
#include <exception>
#include <cstdint>
#include <flecs.h>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <simnet/telemetry_trace.hpp>

import simnet.config;
import simnet.core;
import simnet.game_server;
import simnet.pipeline;
import simnet.snapshot;
import simnet.telemetry;
import simnet.transport;

#include "app_smoke.hpp"

namespace
{
    constexpr auto handshake_timeout = std::chrono::seconds(2);
    constexpr auto handshake_drain_time = std::chrono::milliseconds(50);
    constexpr auto final_ack_timeout = std::chrono::seconds(2);

    struct ServerHandshakeResult
    {
        bool valid {};
        std::optional<simnet::PeerId> ready_peer {};
    };

    struct OutboundPeerState
    {
        simnet::PeerId peer {};
        simnet::ClientReplicationState pipeline_state {};
        simnet::SequenceId latest_emitted_sequence {};
        simnet::SnapshotAck latest_ack {};
    };

    enum class TransportServiceResult
    {
        Ready,
        Disconnected,
        Failed
    };

    [[nodiscard]] bool valid_ack_history_mask(simnet::SnapshotAck const& ack) noexcept
    {
        if (ack.newest_received_snapshot == 0U) {
            return false;
        }
        if (ack.newest_received_snapshot > 32U) {
            return true;
        }
        auto const history_bits = ack.newest_received_snapshot - 1U;
        auto const allowed_mask = history_bits == 0U ? 0U : (1U << history_bits) - 1U;
        return (ack.received_mask & ~allowed_mask) == 0U;
    }

    [[nodiscard]] simnet::BoidState make_smoke_boid(
        simnet::EntityNetId id,
        std::uint32_t index,
        simnet::Tick tick
    )
    {
        auto const base = static_cast<float>(index);
        auto const tick_offset = static_cast<float>(tick) * 0.25F;
        return {
            .id = id,
            .position = {
                base * 2.0F + tick_offset,
                base * 0.5F,
                0.0F,
            },
            .heading = { 1.0F, 0.0F, 0.0F },
            .hue = static_cast<std::uint8_t>((index * 23U) & 0xFFU),
        };
    }

    void populate_or_update_smoke_world(flecs::world& world, simnet::Tick tick)
    {
        for (std::uint32_t index = 0; index < simnet::app::smoke_boid_count; ++index) {
            auto const id = static_cast<simnet::EntityNetId>(index + 1U);
            static_cast<void>(simnet::upsert_authoritative_boid(world, make_smoke_boid(id, index, tick)));
        }

        if (tick == 2) {
            static_cast<void>(simnet::delete_authoritative_boid(world, simnet::app::smoke_boid_count));
        }
    }

    void log_snapshot_report(simnet::ServerSnapshotExtractionReport const& report)
    {
        auto message = "server snapshot tick=" + std::to_string(report.tick)
            + " entities=" + std::to_string(report.entity_count)
            + " valid=" + (report.valid ? std::string { "true" } : std::string { "false" });
        if (!report.valid) {
            message += " error=" + report.error;
        }
        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info, message);
    }

    [[nodiscard]] std::string skip_reason_name(simnet::EncodeSkipReason reason)
    {
        switch (reason) {
        case simnet::EncodeSkipReason::None:
            return "None";
        case simnet::EncodeSkipReason::SendInterval:
            return "SendInterval";
        }
        return "Unknown";
    }

    void log_encode_report(simnet::EncodeOutput const& output)
    {
        auto const& report = output.report;
        if (output.kind == simnet::EncodeResultKind::Skipped) {
            simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
                "server encode tick=" + std::to_string(report.tick)
                    + " entities=" + std::to_string(report.input_entities)
                    + " result=skipped"
                    + " reason=" + skip_reason_name(output.skip_reason));
            return;
        }

        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
            "server encode tick=" + std::to_string(report.tick)
                + " entities=" + std::to_string(report.input_entities)
                + " result=packet"
                + " sequence=" + std::to_string(report.sequence)
                + " bytes=" + std::to_string(report.packet_bytes)
                + " selected=" + std::to_string(report.selected_entities)
                + " upserts=" + std::to_string(report.upsert_count)
                + " deletes=" + std::to_string(report.delete_count)
                + " budget_exceeded=" + (report.budget_exceeded ? std::string { "true" } : std::string { "false" }));
    }

    [[nodiscard]] ServerHandshakeResult run_handshake_window(simnet::TransportServer& transport)
    {
        auto connected = false;
        auto session_ready = false;
        auto ready_peer = std::optional<simnet::PeerId> {};
        auto failed = false;
        auto stop_at = std::chrono::steady_clock::now() + handshake_timeout;
        auto events = std::vector<simnet::TransportEvent> {};

        while (!failed && std::chrono::steady_clock::now() < stop_at) {
            events.clear();
            auto const poll = transport.poll(events, 10);
            if (!poll.ok) {
                simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                    "server transport poll failed: " + poll.error.message);
                return {};
            }

            for (auto const& event : events) {
                if (auto const* value = std::get_if<simnet::PeerConnected>(&event)) {
                    connected = true;
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Info,
                        "server transport peer connected peer=" + std::to_string(value->peer));
                } else if (auto const* value = std::get_if<simnet::PeerSessionReady>(&event)) {
                    session_ready = true;
                    ready_peer = value->peer;
                    auto const drain_deadline = std::chrono::steady_clock::now() + handshake_drain_time;
                    stop_at = std::min(stop_at, drain_deadline);
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Info,
                        "server transport session ready peer=" + std::to_string(value->peer));
                } else if (auto const* value = std::get_if<simnet::PeerDisconnected>(&event)) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Warn,
                        "server transport peer disconnected peer=" + std::to_string(value->peer)
                            + " code=" + std::to_string(static_cast<std::uint16_t>(value->code)));
                    if (!ready_peer.has_value()) {
                        failed = true;
                    } else if (*ready_peer == value->peer) {
                        ready_peer.reset();
                    }
                } else if (auto const* value = std::get_if<simnet::TransportErrorEvent>(&event)) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                        "server transport error: " + value->message);
                    if (!ready_peer.has_value()) {
                        failed = true;
                    }
                } else if (std::holds_alternative<simnet::ReceivedPacket>(event)) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Warn,
                        "server received unexpected app payload during handshake phase");
                }
            }
        }

        if (failed) {
            return {};
        }
        if (!connected) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Info,
                "server transport handshake window ended without a peer");
            return { .valid = true };
        }
        if (!session_ready) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                "server transport peer did not become session ready");
            return {};
        }
        return {
            .valid = true,
            .ready_peer = ready_peer,
        };
    }

    [[nodiscard]] TransportServiceResult service_transport(
        simnet::TransportServer& transport,
        OutboundPeerState& peer_state,
        std::uint32_t timeout_ms
    )
    {
        auto events = std::vector<simnet::TransportEvent> {};
        auto const poll = transport.poll(events, timeout_ms);
        if (!poll.ok) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                "server transport poll failed: " + poll.error.message);
            return TransportServiceResult::Failed;
        }

        for (auto const& event : events) {
            if (auto const* value = std::get_if<simnet::PeerDisconnected>(&event)) {
                simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Info,
                    "server transport peer disconnected peer=" + std::to_string(value->peer)
                        + " code=" + std::to_string(static_cast<std::uint16_t>(value->code)));
                if (value->peer == peer_state.peer) {
                    return TransportServiceResult::Disconnected;
                }
            } else if (auto const* value = std::get_if<simnet::SnapshotAckReceived>(&event)) {
                auto const& ack = value->ack;
                auto const history_does_not_regress =
                    ack.newest_received_snapshot > peer_state.latest_ack.newest_received_snapshot
                    || (ack.received_mask & peer_state.latest_ack.received_mask)
                        == peer_state.latest_ack.received_mask;
                auto const valid = value->peer == peer_state.peer
                    && ack.newest_received_snapshot != 0
                    && ack.newest_applied_snapshot <= ack.newest_received_snapshot
                    && ack.newest_received_snapshot <= peer_state.latest_emitted_sequence
                    && ack.newest_received_snapshot >= peer_state.latest_ack.newest_received_snapshot
                    && ack.newest_applied_snapshot >= peer_state.latest_ack.newest_applied_snapshot
                    && history_does_not_regress
                    && valid_ack_history_mask(ack);
                if (!valid) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                        "server received invalid snapshot ACK");
                    return TransportServiceResult::Failed;
                }
                peer_state.latest_ack = ack;
                simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Info,
                    "server snapshot ACK received peer=" + std::to_string(value->peer)
                        + " received=" + std::to_string(ack.newest_received_snapshot)
                        + " mask=" + std::to_string(ack.received_mask)
                        + " applied=" + std::to_string(ack.newest_applied_snapshot));
            } else if (auto const* value = std::get_if<simnet::TransportErrorEvent>(&event)) {
                simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                    "server transport error: " + value->message);
                return TransportServiceResult::Failed;
            } else if (std::holds_alternative<simnet::ReceivedPacket>(event)) {
                simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                    "server received unexpected app payload");
                return TransportServiceResult::Failed;
            }
        }
        return TransportServiceResult::Ready;
    }

    [[nodiscard]] bool wait_for_final_ack(
        simnet::TransportServer& transport,
        OutboundPeerState& peer_state
    )
    {
        auto const final_sequence = peer_state.latest_emitted_sequence;
        auto const deadline = std::chrono::steady_clock::now() + final_ack_timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (peer_state.latest_ack.newest_received_snapshot == final_sequence
                && peer_state.latest_ack.newest_applied_snapshot == final_sequence) {
                if (peer_state.latest_ack.received_mask != simnet::app::smoke_final_received_mask) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                        "server final snapshot ACK history mask is invalid for smoke");
                    return false;
                }
                return true;
            }
            auto const service = service_transport(transport, peer_state, 10);
            if (peer_state.latest_ack.newest_received_snapshot == final_sequence
                && peer_state.latest_ack.newest_applied_snapshot == final_sequence) {
                if (peer_state.latest_ack.received_mask != simnet::app::smoke_final_received_mask) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                        "server final snapshot ACK history mask is invalid for smoke");
                    return false;
                }
                return true;
            }
            if (service != TransportServiceResult::Ready) {
                return false;
            }
        }
        simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
            "server final snapshot ACK timed out");
        return false;
    }
}

int main()
{
    bool telemetry_started = false;
    try {
        SIMNET_TRACE_SCOPE("Server main");

        auto const shared_config = simnet::load_shared_config(simnet::default_shared_config_path());
        auto const server_config = simnet::load_server_config(simnet::default_server_config_path());

        simnet::initialize_telemetry(server_config.telemetry);
        telemetry_started = true;

        auto const network_fingerprint = simnet::fingerprint_network_compatibility(shared_config);
        auto const runtime_fingerprint = simnet::fingerprint_runtime_config(shared_config, server_config);

        simnet::log(simnet::LogCategory::Config, simnet::LogLevel::Info,
            "server network fingerprint: " + std::to_string(network_fingerprint.value));
        simnet::log(simnet::LogCategory::Config, simnet::LogLevel::Info,
            "server runtime fingerprint: " + std::to_string(runtime_fingerprint.value));

        auto const pipeline = simnet::app::make_smoke_pipeline(shared_config);
        auto const session_identity = simnet::app::make_session_identity(shared_config, pipeline);
        auto transport = simnet::TransportServer {};
        auto const transport_start = transport.start({
            .bind_address = server_config.transport.host,
            .port = server_config.transport.port,
            .max_peers = server_config.transport.max_clients,
            .expected_identity = session_identity,
        });
        if (!transport_start.ok) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                "server transport start failed: " + transport_start.error.message);
            simnet::shutdown_telemetry();
            return 1;
        }
        simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Info,
            "server transport listening on " + server_config.transport.host
                + ":" + std::to_string(server_config.transport.port));
        auto const handshake = run_handshake_window(transport);
        if (!handshake.valid) {
            transport.stop();
            simnet::shutdown_telemetry();
            return 1;
        }

        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
            "authoritative snapshot producer smoke starting");

        auto world = flecs::world {};
        simnet::register_server_game(world);
        auto candidate_pipeline_state = simnet::ClientReplicationState {};
        auto peer_state = handshake.ready_peer.has_value()
            ? std::optional<OutboundPeerState> { {
                .peer = *handshake.ready_peer,
                .pipeline_state = {},
            } }
            : std::nullopt;
        auto pipeline_scratch = simnet::PipelineScratch {};

        for (simnet::Tick tick = 0; tick < simnet::app::smoke_tick_count; ++tick) {
            populate_or_update_smoke_world(world, tick);

            auto snapshot = simnet::WorldSnapshot {};
            auto const extraction = simnet::extract_world_snapshot(world, tick, snapshot);
            log_snapshot_report(extraction);
            if (!extraction.valid) {
                simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Error,
                    "authoritative snapshot extraction failed: " + extraction.error);
                simnet::flush_telemetry();
                simnet::shutdown_telemetry();
                return 1;
            }

            auto const encode_output = simnet::encode_snapshot(
                pipeline,
                peer_state.has_value() ? peer_state->pipeline_state : candidate_pipeline_state,
                pipeline_scratch,
                { .snapshot = &snapshot }
            );
            log_encode_report(encode_output);

            if (peer_state.has_value() && encode_output.kind == simnet::EncodeResultKind::Packet) {
                auto const sent = transport.send({
                    .peer = peer_state->peer,
                    .lane = simnet::Lane::Snapshot,
                    // Phase 3 uses reliable delivery for deterministic end-to-end validation.
                    .delivery = simnet::Delivery::ReliableSequenced,
                    .payload = encode_output.packet.bytes,
                });
                if (!sent.ok) {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                        "server snapshot send failed: " + sent.error.message);
                    transport.stop();
                    simnet::shutdown_telemetry();
                    return 1;
                }
                simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Info,
                    "server snapshot sent tick=" + std::to_string(encode_output.report.tick)
                        + " sequence=" + std::to_string(encode_output.report.sequence)
                        + " bytes=" + std::to_string(encode_output.report.packet_bytes));
                peer_state->latest_emitted_sequence = encode_output.report.sequence;
            }
            if (peer_state.has_value()) {
                auto const service = service_transport(transport, *peer_state, 10);
                if (service == TransportServiceResult::Failed
                    || (service == TransportServiceResult::Disconnected
                        && peer_state->latest_ack.newest_applied_snapshot
                            != peer_state->latest_emitted_sequence)) {
                    transport.stop();
                    simnet::shutdown_telemetry();
                    return 1;
                }
            }

            SIMNET_TRACE_PLOT("server.snapshot_entities", static_cast<double>(extraction.entity_count));
            SIMNET_TRACE_PLOT("server.encode_packet_bytes", static_cast<double>(encode_output.report.packet_bytes));
            SIMNET_TRACE_FRAME("server");
        }

        if (peer_state.has_value() && !wait_for_final_ack(transport, *peer_state)) {
            transport.stop();
            simnet::shutdown_telemetry();
            return 1;
        }
        transport.stop();

        SIMNET_TRACE_PLOT("server.network_fingerprint", static_cast<double>(network_fingerprint.value));
        SIMNET_TRACE_PLOT("server.runtime_fingerprint", static_cast<double>(runtime_fingerprint.value));
        SIMNET_TRACE_FRAME("server");
        simnet::flush_telemetry();
        simnet::shutdown_telemetry();
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "Server failed: " << error.what() << '\n';
        if (telemetry_started) {
            simnet::shutdown_telemetry();
        }
        return 1;
    }
}
