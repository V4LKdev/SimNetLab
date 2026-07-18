#include "server_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <flecs.h>
#include <iostream>
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
import simnet.core;
import simnet.game_server;
import simnet.game_shared;
import simnet.pipeline;
import simnet.runtime;
import simnet.snapshot;
import simnet.telemetry;
import simnet.transport;

namespace
{
    constexpr std::size_t retained_snapshot_limit = 64;

    struct ServerOptions
    {
        std::uint64_t max_frames {};
        simnet::Tick max_ticks {};
        simnet::NS max_runtime {};
        simnet::NS max_frame_time { std::chrono::milliseconds(250) };
        std::uint16_t max_steps_per_frame { 5 };
    };

    struct RetainedSnapshot
    {
        simnet::SequenceId sequence {};
        simnet::WorldSnapshot snapshot {};
    };

    struct PeerRuntimeState
    {
        simnet::PeerId peer {};
        simnet::ClientReplicationState pipeline_state {};
        simnet::SnapshotAck latest_ack {};
        simnet::SequenceId newest_emitted_sequence {};
        std::optional<simnet::SequenceId> acknowledged_baseline_sequence {};
        std::deque<RetainedSnapshot> retained_snapshots {};
    };

    [[nodiscard]] ServerOptions parse_options(int argc, char** argv)
    {
        auto options = ServerOptions {};
        for (auto index = 1; index < argc; ++index) {
            auto const option = std::string_view { argv[index] };
            if (option == "--max-frames") {
                options.max_frames =
                    simnet::app::parse_unsigned<std::uint64_t>(
                        simnet::app::next_option_value(index, argc, argv, option),
                        option
                    );
            } else if (option == "--max-ticks") {
                options.max_ticks =
                    simnet::app::parse_unsigned<simnet::Tick>(
                        simnet::app::next_option_value(index, argc, argv, option),
                        option
                    );
            } else if (option == "--max-runtime-ms") {
                options.max_runtime = simnet::app::milliseconds_option(index, argc, argv, option);
            } else if (option == "--max-frame-delta-ms") {
                options.max_frame_time = simnet::app::milliseconds_option(index, argc, argv, option);
            } else if (option == "--max-steps-per-frame") {
                options.max_steps_per_frame =
                    simnet::app::parse_unsigned<std::uint16_t>(
                        simnet::app::next_option_value(index, argc, argv, option),
                        option
                    );
            } else {
                throw std::runtime_error("unknown server option: " + std::string { option });
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

    [[nodiscard]] simnet::BoidState initial_boid(std::uint32_t index, float world_half)
    {
        auto const span = std::max(1.0F, world_half * 2.0F);
        auto const x = static_cast<float>(index % 100U) / 99.0F * span - world_half;
        auto const y = static_cast<float>((index / 100U) % 100U) / 99.0F * span - world_half;
        auto const z = static_cast<float>((index / 10000U) % 100U) / 99.0F * span - world_half;
        return {
            .id = static_cast<simnet::EntityNetId>(index + 1U),
            .position = { x, y, z },
            .heading = { 1.0F, 0.0F, 0.0F },
            .hue = static_cast<std::uint8_t>((index * 23U) & 0xFFU),
        };
    }

    void initialize_world(flecs::world& world, simnet::SharedConfig const& config)
    {
        for (std::uint32_t index = 0; index < config.simulation.initial_boid_count; ++index) {
            static_cast<void>(
                simnet::upsert_authoritative_boid(world, initial_boid(index, config.simulation.world_half))
            );
        }
    }

    void advance_world(flecs::world& world, simnet::NS fixed_dt, float world_half)
    {
        auto const seconds = std::chrono::duration<float>(fixed_dt).count();
        world.each(
            [seconds, world_half](simnet::Position& position, simnet::Heading const& heading) {
                position.value = position.value + heading.value * seconds;
                if (position.value.x > world_half) {
                    position.value.x = -world_half;
                }
            }
        );
    }

    [[nodiscard]] bool valid_ack(PeerRuntimeState const& peer, simnet::SnapshotAck const& ack)
    {
        return ack.newest_received_snapshot != 0
            && ack.newest_applied_snapshot <= ack.newest_received_snapshot
            && ack.newest_received_snapshot >= peer.latest_ack.newest_received_snapshot
            && ack.newest_applied_snapshot >= peer.latest_ack.newest_applied_snapshot
            && ack.newest_received_snapshot <= peer.newest_emitted_sequence;
    }

    [[nodiscard]] auto find_retained_snapshot(
        PeerRuntimeState& peer,
        simnet::SequenceId sequence
    )
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
        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Warn,
            "acknowledged snapshot no longer retained; next packet uses FullReplace");
        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
            "delta unavailable; using FullReplace");
    }

    void trim_retained_snapshots(PeerRuntimeState& peer)
    {
        while (peer.retained_snapshots.size() > retained_snapshot_limit) {
            auto const evicted_sequence = peer.retained_snapshots.front().sequence;
            peer.retained_snapshots.pop_front();
            if (peer.acknowledged_baseline_sequence == evicted_sequence) {
                invalidate_baseline(peer);
            }
        }
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
                simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
                    "baseline promoted sequence=" + std::to_string(ack.newest_applied_snapshot));
            }
        } else {
            invalidate_baseline(peer);
        }
    }

    [[nodiscard]] bool poll_transport(
        simnet::TransportServer& transport,
        std::optional<PeerRuntimeState>& peer,
        std::vector<simnet::TransportEvent>& events,
        std::uint32_t timeout_ms,
        bool delta_enabled
    )
    {
        events.clear();
        auto const result = transport.poll(events, timeout_ms);
        if (!result.ok) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                "server transport poll failed: " + result.error.message);
            return false;
        }

        for (auto const& event : events) {
            if (auto const* ready = std::get_if<simnet::PeerSessionReady>(&event)) {
                if (peer.has_value() && peer->peer != ready->peer) {
                    transport.disconnect(ready->peer, simnet::DisconnectCode::ServerFull);
                } else {
                    peer = PeerRuntimeState { .peer = ready->peer };
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Info,
                        "server session ready peer=" + std::to_string(ready->peer));
                }
            } else if (auto const* disconnected = std::get_if<simnet::PeerDisconnected>(&event)) {
                if (peer.has_value() && peer->peer == disconnected->peer) {
                    peer.reset();
                }
            } else if (auto const* received = std::get_if<simnet::SnapshotAckReceived>(&event)) {
                if (peer.has_value() && received->peer == peer->peer && valid_ack(*peer, received->ack)) {
                    if (delta_enabled) {
                        apply_ack(*peer, received->ack);
                    } else {
                        peer->latest_ack = received->ack;
                    }
                } else {
                    simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Warn,
                        "server ignored invalid snapshot ACK");
                }
            } else if (auto const* error = std::get_if<simnet::TransportErrorEvent>(&event)) {
                simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                    "server transport error: " + error->message);
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool run_tick(
        flecs::world& world,
        simnet::Tick tick,
        simnet::NS fixed_dt,
        float world_half,
        simnet::PipelineDefinition const& pipeline,
        simnet::Delivery delivery,
        simnet::TransportServer& transport,
        std::optional<PeerRuntimeState>& peer,
        simnet::PipelineScratch& scratch
    )
    {
        advance_world(world, fixed_dt, world_half);
        auto snapshot = simnet::WorldSnapshot {};
        auto const extraction = simnet::extract_world_snapshot(world, tick, snapshot);
        if (!extraction.valid) {
            simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Error,
                "snapshot extraction failed: " + extraction.error);
            return false;
        }
        if (!peer.has_value()) {
            return true;
        }

        auto const delta_enabled = simnet::has_all_flags(
            pipeline.techniques,
            simnet::PipelineTechniqueFlags::Delta
        );
        auto baseline = peer->retained_snapshots.end();
        if (delta_enabled && peer->acknowledged_baseline_sequence.has_value()) {
            baseline = find_retained_snapshot(*peer, *peer->acknowledged_baseline_sequence);
            if (baseline == peer->retained_snapshots.end()) {
                invalidate_baseline(*peer);
            }
        }
        auto const use_baseline = baseline != peer->retained_snapshots.end();
        auto const encoded = simnet::encode_snapshot(
            pipeline,
            peer->pipeline_state,
            scratch,
            {
                .snapshot = &snapshot,
                .baseline_snapshot = use_baseline
                    ? &baseline->snapshot
                    : nullptr,
                .baseline_sequence = use_baseline
                    ? baseline->sequence
                    : 0U,
            }
        );
        if (encoded.kind == simnet::EncodeResultKind::Skipped) {
            return true;
        }
        auto const sent = transport.send({
            .peer = peer->peer,
            .lane = simnet::Lane::Snapshot,
            .delivery = delivery,
            .payload = encoded.packet.bytes,
        });
        if (!sent.ok) {
            simnet::log(simnet::LogCategory::Transport, simnet::LogLevel::Error,
                "snapshot send failed: " + sent.error.message);
            return false;
        }

        peer->retained_snapshots.push_back({
            .sequence = encoded.packet.sequence,
            .snapshot = std::move(snapshot),
        });
        peer->newest_emitted_sequence = encoded.packet.sequence;
        trim_retained_snapshots(*peer);
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
        auto events = std::vector<simnet::TransportEvent> {};
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
            auto const shared = load_shared_config(default_shared_config_path());
            auto const local = load_server_config(default_server_config_path());
            auto telemetry = TelemetryLifetime { local.telemetry };
            auto signals = SignalHandlers {};
            auto const pipeline = make_snapshot_pipeline(shared, local.transport);

            auto transport = TransportServer {};
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
                log(LogCategory::Transport, LogLevel::Error,
                    "server transport start failed: " + started.error.message);
                return 1;
            }

            auto const settings = RuntimeSettings {
                .fixed_step = {
                    .tick_rate_hz = shared.simulation.tick_rate_hz,
                    .max_frame_time = options.max_frame_time,
                    .max_steps_per_frame = options.max_steps_per_frame,
                },
                .max_frames = options.max_frames,
                .max_ticks = options.max_ticks,
                .max_runtime = options.max_runtime,
            };
            auto clock = make_clock(settings.fixed_step);
            if (clock.fixed_dt <= NS {} || settings.fixed_step.max_steps_per_frame == 0) {
                throw std::runtime_error("invalid fixed-step runtime settings");
            }

            auto world = flecs::world {};
            register_server_game(world);
            initialize_world(world, shared);

            auto stats = RuntimeStats {};
            auto timer = RuntimeFrameTimer {};
            reset_frame_timer(timer);
            auto stop = StopRequest {};
            auto peer = std::optional<PeerRuntimeState> {};
            auto events = std::vector<TransportEvent> {};
            auto scratch = PipelineScratch {};
            auto const delivery = snapshot_delivery(local.transport);
            auto const delta_enabled = has_all_flags(
                pipeline.techniques,
                PipelineTechniqueFlags::Delta
            );

            log(LogCategory::Simulation, LogLevel::Info,
                "server runtime started entities=" + std::to_string(shared.simulation.initial_boid_count));

            while (!stop.requested()) {
                if (signal_stop_requested()) {
                    static_cast<void>(stop.request(ShutdownReason::Signal));
                    break;
                }
                if (!poll_transport(transport, peer, events, 1, delta_enabled)) {
                    static_cast<void>(stop.request(ShutdownReason::FatalError));
                    break;
                }

                auto const frame = plan_runtime_frame(clock, stats, sample_frame_delta(timer), settings);
                for (std::uint16_t offset = 0; offset < frame.step_count; ++offset) {
                    if (!run_tick(
                            world,
                            frame.first_tick + offset,
                            clock.fixed_dt,
                            shared.simulation.world_half,
                            pipeline,
                            delivery,
                            transport,
                            peer,
                            scratch
                        )) {
                        static_cast<void>(stop.request(ShutdownReason::FatalError));
                        break;
                    }
                }

                if (frame.step_limit_reached) {
                    log(LogCategory::Core, LogLevel::Warn,
                        "server dropped simulation time ns=" + std::to_string(frame.dropped_time.count()));
                }
                auto const limit = reached_runtime_limit(settings, stats);
                if (limit != ShutdownReason::None) {
                    static_cast<void>(stop.request(limit));
                }
                SIMNET_TRACE_PLOT("server.runtime.steps", static_cast<double>(frame.step_count));
                SIMNET_TRACE_FRAME("server");
            }

            disconnect_before_stop(transport, peer);
            transport.stop();
            log(LogCategory::Simulation, LogLevel::Info,
                "server runtime stopped reason=" + std::string { shutdown_reason_name(stop.reason()) }
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
