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
                        { .bytes = packet->payload }
                    );
                    if (!decoded.report.valid) {
                        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Error,
                            "client snapshot decode failed: " + decoded.report.error);
                        return false;
                    }

                    auto const applied = simnet::apply_client_snapshot_patch(world, decoded.patch);
                    if (!applied.valid) {
                        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Error,
                            "client snapshot apply failed: " + applied.error);
                        return false;
                    }

                    simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
                        "client snapshot applied tick=" + std::to_string(applied.tick)
                            + " sequence=" + std::to_string(decoded.report.sequence)
                            + " bytes=" + std::to_string(decoded.report.packet_bytes)
                            + " kind=" + std::to_string(static_cast<std::uint8_t>(applied.kind))
                            + " upserts=" + std::to_string(applied.upsert_count)
                            + " deletes=" + std::to_string(applied.delete_count)
                            + " entities=" + std::to_string(applied.final_entities));

                    if (applied.tick == simnet::app::smoke_final_tick) {
                        auto const& clock = world.get<simnet::ClientReplicationClock>();
                        auto const& index = world.get<simnet::ClientReplicationIndex>();
                        auto const final_state_valid = decoded.patch.kind == simnet::SnapshotKind::FullReplace
                            && clock.latest_tick == simnet::app::smoke_final_tick
                            && index.size() == simnet::app::smoke_boid_count;
                        if (!final_state_valid) {
                            simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Error,
                                "client final replicated world validation failed"
                                    " tick=" + std::to_string(clock.latest_tick)
                                    + " entities=" + std::to_string(index.size()));
                        }
                        return final_state_valid;
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

        // Fingerprints give early confirmation that runtime config is coherent.
        simnet::log(simnet::LogCategory::Config, simnet::LogLevel::Info,
            "client network fingerprint: " + std::to_string(network_fingerprint.value));
        simnet::log(simnet::LogCategory::Config, simnet::LogLevel::Info,
            "client runtime fingerprint: " + std::to_string(runtime_fingerprint.value));

        auto const pipeline = simnet::app::make_smoke_pipeline(shared_config);
        auto const session_identity = simnet::app::make_session_identity(shared_config, pipeline);
        auto transport = simnet::TransportClient {};
        auto const connect = transport.connect({
            .server_address = client_config.transport.host,
            .server_port = client_config.transport.port,
            .identity = session_identity,
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
