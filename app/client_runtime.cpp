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
#include <limits>
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

    struct SnapshotAckTracker
    {
        simnet::app::SnapshotAck value{};
    };

    struct RetainedClientSnapshot
    {
        simnet::SequenceId sequence{};
        simnet::WorldSnapshot snapshot{};
        std::uint64_t capacity_bytes{};
    };

    struct PendingCompressionGroup
    {
        simnet::PacketGroupId group_id{};
        simnet::Nanoseconds first_received_time{};
        std::uint64_t transport_bytes{};
    };

    struct ClientCompressionReport
    {
        simnet::app::CompressionMode mode{simnet::app::CompressionMode::None};
        simnet::CompressionEncoding latest_encoding{simnet::CompressionEncoding::Raw};
        std::uint64_t raw_packet_count{};
        std::uint64_t compressed_packet_count{};
        std::uint64_t invalid_payload_count{};
        std::uint32_t latest_input_bytes{};
        std::uint32_t latest_payload_bytes{};
        std::uint32_t latest_envelope_bytes{};
        std::uint32_t latest_output_bytes{};
        std::uint32_t latest_completed_representation_bytes{};
        std::uint32_t latest_completed_transport_bytes{};
        std::uint32_t canonical_entity_count{};
        simnet::Nanoseconds decompression_cpu_time{};
    };

    constexpr std::size_t retained_snapshot_limit = 64;

    enum class ApplyPacketOutcome : std::uint8_t
    {
        Applied,
        Ignored,
        RecoveryRequested,
        Fatal
    };

    void expire_pending_compression_groups(
        std::vector<PendingCompressionGroup>& groups,
        simnet::Nanoseconds now,
        simnet::Nanoseconds timeout
    )
    {
        std::erase_if(groups, [&](PendingCompressionGroup const& group) {
            return now >= group.first_received_time && now - group.first_received_time >= timeout;
        });
    }

    void record_group_transport_bytes(
        std::vector<PendingCompressionGroup>& groups,
        simnet::PacketGroupId group_id,
        std::uint32_t bytes,
        simnet::Nanoseconds now,
        std::uint32_t maximum_groups
    )
    {
        if (group_id == 0U) {
            return;
        }
        auto const found = std::ranges::find(groups, group_id, &PendingCompressionGroup::group_id);
        if (found != groups.end()) {
            found->transport_bytes += bytes;
            return;
        }
        if (groups.size() >= maximum_groups) {
            return;
        }
        groups.push_back({
            .group_id = group_id,
            .first_received_time = now,
            .transport_bytes = bytes,
        });
    }

    [[nodiscard]] std::uint32_t group_transport_bytes(
        std::vector<PendingCompressionGroup> const& groups,
        simnet::PacketGroupId group_id,
        std::uint32_t fallback
    ) noexcept
    {
        auto const found = std::ranges::find(groups, group_id, &PendingCompressionGroup::group_id);
        return found == groups.end()
                || found->transport_bytes > std::numeric_limits<std::uint32_t>::max()
            ? fallback
            : static_cast<std::uint32_t>(found->transport_bytes);
    }

    [[nodiscard]] std::string_view
    client_replication_outcome_name(simnet::ClientReplicationOutcome outcome) noexcept
    {
        using enum simnet::ClientReplicationOutcome;
        switch (outcome) {
            case DecodeFailed:
                return "decode_failed";
            case StaleSequenceIgnored:
                return "stale_sequence_ignored";
            case BaselineUnavailable:
                return "baseline_unavailable";
            case ReconstructionFailed:
                return "reconstruction_failed";
            case SinkApplicationFailed:
                return "sink_application_failed";
            case Applied:
                return "applied";
        }
        return "unknown";
    }

    void
    log_client_replication_measurements(simnet::ClientReplicationMeasurements const& measurements)
    {
        if (!measurements.latest_attempt.has_value()) {
            simnet::log(
                simnet::LogCategory::Telemetry,
                simnet::LogLevel::Info,
                "client replication measurements attempts=0 applied=0"
            );
            return;
        }

        auto const& value = *measurements.latest_attempt;
        simnet::log(
            simnet::LogCategory::Telemetry,
            simnet::LogLevel::Info,
            "client replication measurements attempts=" + std::to_string(measurements.attempt_count)
                + " applied=" + std::to_string(measurements.applied_count) + " latest_outcome="
                + std::string{client_replication_outcome_name(value.outcome)} + " tick="
                + std::to_string(value.tick) + " sequence=" + std::to_string(value.sequence)
                + " baseline_sequence=" + std::to_string(value.baseline_sequence)
                + " snapshot_kind=" + std::to_string(static_cast<unsigned>(value.snapshot_kind))
                + " encoded_update_bytes=" + std::to_string(value.encoded_update_bytes)
                + " application_payload_bytes=" + std::to_string(value.application_payload_bytes)
                + " transport_payload_bytes=" + std::to_string(value.transport_payload_bytes)
                + " upserts=" + std::to_string(value.upsert_count)
                + " deletes=" + std::to_string(value.delete_count) + " reconstructed_entities="
                + std::to_string(value.reconstructed_entity_count) + " final_sink_entities="
                + std::to_string(value.final_sink_entity_count) + " decode_cpu_ns="
                + std::to_string(value.decode_cpu_time.count()) + " baseline_resolution_cpu_ns="
                + std::to_string(value.baseline_resolution_cpu_time.count())
                + " reconstruction_cpu_ns=" + std::to_string(value.reconstruction_cpu_time.count())
                + " sink_preparation_cpu_ns="
                + std::to_string(value.sink_preparation_cpu_time.count())
                + " sink_application_cpu_ns="
                + std::to_string(value.sink_application_cpu_time.count())
                + " canonical_snapshot_commit_cpu_ns="
                + std::to_string(value.canonical_snapshot_commit_cpu_time.count())
                + " total_receive_to_applied_cpu_ns="
                + std::to_string(value.total_receive_to_applied_cpu_time.count())
        );
    }

#if defined(SIMNET_ENABLE_RENDER)
    [[nodiscard]] std::optional<simnet::GameCameraView>
    player_game_camera(simnet::WorldSnapshot const& snapshot, simnet::EntityNetId player_id)
    {
        if (player_id == 0U) {
            return std::nullopt;
        }
        auto const found = std::ranges::find(snapshot.ids, player_id);
        if (found == snapshot.ids.end()) {
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
        return {.buttons = buttons};
    }

    struct ClientPresentationState
    {
        simnet::Tick observed_tick{};
        simnet::Nanoseconds elapsed{};
        simnet::WorldSnapshot interpolated{};
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
            state.elapsed += std::max(frame_delta, simnet::Nanoseconds{});
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
        switch (cause) {
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
        if (role == "stationary_observer") {
            return simnet::app::ClientRole::StationaryObserver;
        }
        if (role == "player") {
            return simnet::app::ClientRole::Player;
        }
        throw std::runtime_error("unsupported gameplay role: " + std::string{role});
    }

#if defined(SIMNET_ENABLE_RENDER)
    [[nodiscard]] std::string_view
    client_connection_state_name(ClientConnectionState state) noexcept
    {
        switch (state) {
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
            .stationary_observer_vertical_fov_degrees
            = config.stationary_observer_vertical_fov_degrees,
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
        simnet::app::SnapshotAck const& ack,
        std::optional<simnet::TransportDelivery> effective_delivery,
        std::uint64_t sequence_gap_count,
        std::uint64_t recovery_request_count,
        std::uint64_t missing_baseline_rejection_count,
        std::uint64_t reliable_promotion_count,
        simnet::ReassemblyReport const& packet_report,
        ClientCompressionReport const& compression_report,
        std::optional<std::uint32_t> reconstructed_entity_count,
        std::string_view role,
        std::optional<simnet::StationaryObserverView> stationary_observer,
        std::optional<simnet::GameCameraView> game_camera,
        simnet::RunSetupView setup
    )
    {
        auto replication = std::optional<simnet::RenderReplicationInfo>{};
        if (session_ready || ack.newest_received_snapshot != 0
            || ack.newest_applied_snapshot != 0) {
            replication = simnet::RenderReplicationInfo{
                .latest_received_sequence = ack.newest_received_snapshot != 0
                    ? std::optional<simnet::SequenceId>{ack.newest_received_snapshot}
                    : std::optional<simnet::SequenceId>{},
                .latest_applied_sequence = ack.newest_applied_snapshot != 0
                    ? std::optional<simnet::SequenceId>{ack.newest_applied_snapshot}
                    : std::optional<simnet::SequenceId>{},
                .configured_delivery = config.snapshot_delivery.mode,
                .effective_delivery = effective_delivery.has_value()
                    ? std::optional<std::string_view>{simnet::app::transport_delivery_name(
                          *effective_delivery
                      )}
                    : std::optional<std::string_view>{},
                .recovery_request_count = recovery_request_count,
                .missing_baseline_rejection_count = missing_baseline_rejection_count,
                .sequence_gap_count = sequence_gap_count,
                .reliable_promotion_count = reliable_promotion_count,
                .latest_snapshot_tick = snapshot.tick,
                .area_of_interest_mode = config.pipeline.area_of_interest.mode,
                .interest_source_status = config.pipeline.area_of_interest.mode == "none"
                    ? std::optional<std::string_view>{"not required"}
                    : role == "player"
                    ? std::optional<std::string_view>{"Server authoritative Player"}
                    : std::optional<std::string_view>{"local pose configured"},
                .reconstructed_entity_count = reconstructed_entity_count,
                .packetization_enabled = config.packetization.enabled,
                .received_packet_chunks = packet_report.received_chunks,
                .unique_packet_chunks = packet_report.unique_chunks,
                .duplicate_packet_chunks = packet_report.duplicate_chunks,
                .incomplete_packet_groups = packet_report.incomplete_groups,
                .retained_incomplete_bytes = packet_report.retained_incomplete_bytes,
                .latest_completed_group_bytes = packet_report.latest_completed_group_bytes,
                .completed_packet_groups = packet_report.completed_groups,
                .expired_packet_groups = packet_report.expired_groups,
                .invalid_packet_groups = packet_report.invalid_groups,
                .stale_packet_groups = packet_report.stale_groups,
                .compression_mode = simnet::app::compression_mode_name(compression_report.mode),
                .compression_outcome = compression_report.mode == simnet::app::CompressionMode::None
                    ? std::optional<std::string_view>{"Disabled"}
                    : compression_report.mode == simnet::app::CompressionMode::WholeUpdate
                    ? std::optional<
                          std::
                              string_view>{compression_report.latest_encoding == simnet::CompressionEncoding::Zstd ? "Zstd" : "Raw"}
                    : compression_report.compressed_packet_count == 0U
                    ? std::optional<std::string_view>{"Raw fallback"}
                    : compression_report.raw_packet_count == 0U
                    ? std::optional<std::string_view>{"Zstd"}
                    : std::optional<std::string_view>{"Mixed"},
                .representation_bytes = compression_report.latest_completed_representation_bytes,
                .compression_input_bytes = compression_report.latest_input_bytes,
                .compression_payload_bytes = compression_report.latest_payload_bytes,
                .compression_envelope_bytes = compression_report.latest_envelope_bytes,
                .compression_output_bytes = compression_report.latest_output_bytes,
                .final_transport_bytes = compression_report.latest_completed_transport_bytes,
                .compressed_packet_count = compression_report.compressed_packet_count,
                .raw_packet_count = compression_report.raw_packet_count,
                .invalid_compressed_payloads = compression_report.invalid_payload_count,
                .decompression_cpu_ns = static_cast<std::uint64_t>(
                    std::max(compression_report.decompression_cpu_time, simnet::Nanoseconds{})
                        .count()
                ),
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
        auto options = ClientOptions{};
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
            } else if (option == "--run-id") {
                options.run_id = simnet::app::next_option_value(index, argc, argv, option);
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
                throw std::runtime_error("unknown client option: " + std::string{option});
            }
        }
        return options;
    }

    void observe_client_measurement(
        simnet::ClientReplicationMeasurements& measurements,
        simnet::ClientReplicationCsvWriter& csv,
        simnet::ClientReplicationMeasurement const& measurement
    )
    {
        measurements.observe(measurement);
        if (!csv.submit(measurement)) {
            throw std::runtime_error(
                "client replication CSV submission failed: " + std::string{csv.error()}
            );
        }
    }

    [[nodiscard]] bool
    record_received_snapshot(SnapshotAckTracker& tracker, simnet::SequenceId sequence) noexcept
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
            auto const shifted_history = shift == 32U ? 0U : tracker.value.received_mask << shift;
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
        auto const found = std::ranges::find(history, sequence, &RetainedClientSnapshot::sequence);
        return found == history.end() ? nullptr : &found->snapshot;
    }

    void retain_snapshot(
        std::deque<RetainedClientSnapshot>& history,
        simnet::SequenceId sequence,
        simnet::WorldSnapshot snapshot
    )
    {
        auto const bytes = simnet::app::snapshot_capacity_bytes(snapshot);
        auto retained_bytes = std::uint64_t{};
        for (auto const& retained : history) {
            retained_bytes += retained.capacity_bytes;
        }
        while (!history.empty()
               && (history.size() >= retained_snapshot_limit
                   || retained_bytes > simnet::app::maximum_retained_capacity_bytes - bytes)) {
            retained_bytes -= history.front().capacity_bytes;
            history.pop_front();
        }
        history.push_back({
            .sequence = sequence,
            .snapshot = std::move(snapshot),
            .capacity_bytes = bytes,
        });
    }

    [[nodiscard]] simnet::SnapshotUpdate
    make_full_replace_patch(simnet::WorldSnapshot const& snapshot)
    {
        auto patch = simnet::SnapshotUpdate{
            .tick = snapshot.tick,
            .kind = simnet::SnapshotKind::FullReplace,
        };
        patch.upserts.reserve(snapshot.size());
        for (std::size_t index = 0; index < snapshot.size(); ++index) {
            patch.upserts.push_back({
                .id = snapshot.ids[index],
                .classification = snapshot.classifications[index],
                .position = snapshot.positions[index],
                .heading = snapshot.headings[index],
                .hue = snapshot.hues[index],
            });
        }
        return patch;
    }

    [[nodiscard]] ApplyPacketOutcome apply_packet(
        simnet::CompletedByteGroup const& group,
        simnet::ByteSpan encoded_bytes,
        std::uint32_t transport_payload_bytes,
        bool packetization_enabled,
        simnet::ReassemblyState& reassembly_state,
        simnet::PipelineDefinition const& pipeline,
        simnet::ClientReplicationState& decode_state,
        simnet::SequenceId& latest_applied_sequence,
        SnapshotAckTracker& ack_tracker,
        std::deque<RetainedClientSnapshot>& snapshot_history,
        flecs::world& world,
        simnet::TransportClient& transport,
        simnet::RuntimeStats& stats,
        simnet::ClientReplicationMeasurements& measurements,
        simnet::ClientReplicationCsvWriter& csv,
        bool& logged_multi_packet_application,
        simnet::app::ClientRecoveryRequestState& recovery_request_state,
        std::uint64_t& sequence_gap_count
    )
    {
        auto measurement = simnet::ClientReplicationMeasurement{
            .application_payload_bytes = transport_payload_bytes,
            .transport_payload_bytes = transport_payload_bytes,
        };
        auto const total_start = simnet::steady_now_ns();
        auto decoded = simnet::DecodeOutput{};
        auto candidate_decode_state = decode_state;
        auto const decode_start = simnet::steady_now_ns();
        {
            SIMNET_TRACE_SCOPE_CATEGORY("client.snapshot_decode", simnet::LogCategory::Pipeline);
            decoded = simnet::decode_update(
                pipeline,
                candidate_decode_state,
                {
                    .bytes = encoded_bytes,
                }
            );
        }
        measurement.decode_cpu_time = simnet::steady_now_ns() - decode_start;
        measurement.tick = decoded.report.tick;
        measurement.sequence = decoded.report.sequence;
        measurement.baseline_sequence = decoded.report.baseline_sequence;
        measurement.snapshot_kind = decoded.report.snapshot_kind;
        measurement.encoded_update_bytes = static_cast<std::uint32_t>(encoded_bytes.size());
        measurement.upsert_count = static_cast<std::uint32_t>(decoded.update.upserts.size());
        measurement.delete_count = static_cast<std::uint32_t>(decoded.update.deletes.size());
        if (packetization_enabled && decoded.report.sequence != group.group_id) {
            measurement.outcome = simnet::ClientReplicationOutcome::DecodeFailed;
            observe_client_measurement(measurements, csv, measurement);
            simnet::log(
                simnet::LogCategory::Pipeline,
                simnet::LogLevel::Error,
                "client packet group id does not match decoded sequence"
            );
            return ApplyPacketOutcome::Fatal;
        }
        if (!decoded.report.valid) {
            if (decoded.report.sequence != 0U
                && decoded.report.sequence <= decode_state.latest_remote_sequence) {
                measurement.outcome = simnet::ClientReplicationOutcome::StaleSequenceIgnored;
                observe_client_measurement(measurements, csv, measurement);
                return ApplyPacketOutcome::Ignored;
            }
            measurement.outcome = simnet::ClientReplicationOutcome::DecodeFailed;
            observe_client_measurement(measurements, csv, measurement);
            simnet::log(
                simnet::LogCategory::Pipeline,
                simnet::LogLevel::Error,
                "client snapshot decode failed: " + decoded.report.error
            );
            return ApplyPacketOutcome::Fatal;
        }
        auto candidate_ack_tracker = ack_tracker;
        if (!record_received_snapshot(candidate_ack_tracker, decoded.report.sequence)) {
            measurement.outcome = simnet::ClientReplicationOutcome::StaleSequenceIgnored;
            observe_client_measurement(measurements, csv, measurement);
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Warn,
                "client ignored stale snapshot sequence=" + std::to_string(decoded.report.sequence)
            );
            return ApplyPacketOutcome::Ignored;
        }

        if (latest_applied_sequence != 0U
            && decoded.report.sequence > latest_applied_sequence + 1U) {
            ++sequence_gap_count;
        }

        auto const baseline_start = simnet::steady_now_ns();
        auto const* baseline = static_cast<simnet::WorldSnapshot const*>(nullptr);
        if (decoded.report.baseline_sequence != 0U) {
            baseline = find_retained_snapshot(snapshot_history, decoded.report.baseline_sequence);
            if (baseline == nullptr) {
                measurement.baseline_resolution_cpu_time = simnet::steady_now_ns() - baseline_start;
                measurement.outcome = simnet::ClientReplicationOutcome::BaselineUnavailable;
                observe_client_measurement(measurements, csv, measurement);
                simnet::log(
                    simnet::LogCategory::Snapshot,
                    simnet::LogLevel::Warn,
                    "client Patch baseline is not retained sequence="
                        + std::to_string(decoded.report.baseline_sequence)
                );
                simnet::app::record_missing_baseline_rejection(recovery_request_state);
                if (simnet::app::recovery_request_needed(
                        recovery_request_state,
                        decoded.report.baseline_sequence
                    )) {
                    auto const request = simnet::app::encode_snapshot_recovery_request({
                        .rejected_update_sequence = decoded.report.sequence,
                        .missing_baseline_sequence = decoded.report.baseline_sequence,
                    });
                    auto const sent = transport.send(
                        simnet::app::input_lane,
                        simnet::TransportDelivery::ReliableSequenced,
                        request
                    );
                    if (!sent.ok) {
                        simnet::log(
                            simnet::LogCategory::Transport,
                            simnet::LogLevel::Error,
                            "client recovery request send failed: " + sent.error.message
                        );
                        return ApplyPacketOutcome::Fatal;
                    }
                    simnet::app::record_recovery_request(
                        recovery_request_state,
                        decoded.report.baseline_sequence
                    );
                }
                return ApplyPacketOutcome::RecoveryRequested;
            }
        }
        measurement.baseline_resolution_cpu_time = simnet::steady_now_ns() - baseline_start;

        auto reconstructed = simnet::WorldSnapshot{};
        // Decode validated the update. The baseline is locally empty or a retained reconstruction.
        auto const reconstruction_start = simnet::steady_now_ns();
        auto const reconstruction
            = simnet::reconstruct_world_snapshot_unchecked(baseline, decoded.update, reconstructed);
        measurement.reconstruction_cpu_time = simnet::steady_now_ns() - reconstruction_start;
        if (!reconstruction.valid) {
            measurement.outcome = simnet::ClientReplicationOutcome::ReconstructionFailed;
            observe_client_measurement(measurements, csv, measurement);
            simnet::log(
                simnet::LogCategory::Snapshot,
                simnet::LogLevel::Error,
                "client snapshot reconstruction failed: " + reconstruction.message
            );
            return ApplyPacketOutcome::Fatal;
        }
        if (simnet::app::snapshot_capacity_bytes(reconstructed)
            > simnet::app::maximum_retained_capacity_bytes) {
            measurement.outcome = simnet::ClientReplicationOutcome::ReconstructionFailed;
            observe_client_measurement(measurements, csv, measurement);
            simnet::log(
                simnet::LogCategory::Snapshot,
                simnet::LogLevel::Error,
                "client reconstructed snapshot exceeds the retained capacity limit"
            );
            return ApplyPacketOutcome::Fatal;
        }
        measurement.reconstructed_entity_count = static_cast<std::uint32_t>(reconstructed.size());

        auto const sink_preparation_start = simnet::steady_now_ns();
        auto const baseline_is_current = baseline != nullptr && !snapshot_history.empty()
            && baseline == &snapshot_history.back().snapshot;
        auto replacement = simnet::SnapshotUpdate{};
        auto const* patch_to_apply = &decoded.update;
        if (decoded.update.kind == simnet::SnapshotKind::Patch && !baseline_is_current) {
            replacement = make_full_replace_patch(reconstructed);
            patch_to_apply = &replacement;
        }
        measurement.sink_preparation_cpu_time = simnet::steady_now_ns() - sink_preparation_start;

        auto applied = simnet::ApplyPatchReport{};
        auto const sink_application_start = simnet::steady_now_ns();
        {
            SIMNET_TRACE_SCOPE_CATEGORY("client.snapshot_apply", simnet::LogCategory::Simulation);
            // The update passed decode validation or was built from successful reconstruction.
            applied = simnet::apply_client_snapshot_patch_unchecked(world, *patch_to_apply);
        }
        measurement.sink_application_cpu_time = simnet::steady_now_ns() - sink_application_start;
        measurement.final_sink_entity_count = applied.final_entities;
        if (!applied.valid) {
            measurement.outcome = simnet::ClientReplicationOutcome::SinkApplicationFailed;
            observe_client_measurement(measurements, csv, measurement);
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Error,
                "client snapshot apply failed: " + applied.error
            );
            return ApplyPacketOutcome::Fatal;
        }

        auto const commit_start = simnet::steady_now_ns();
        decode_state = candidate_decode_state;
        candidate_ack_tracker.value.newest_applied_snapshot = decoded.report.sequence;
        ack_tracker = candidate_ack_tracker;
        latest_applied_sequence = decoded.report.sequence;
        retain_snapshot(snapshot_history, decoded.report.sequence, std::move(reconstructed));
        simnet::app::record_snapshot_progress(recovery_request_state);
        stats.ticks = applied.tick;
        simnet::commit_reassembled_group(reassembly_state, decoded.report.sequence);
        measurement.canonical_snapshot_commit_cpu_time = simnet::steady_now_ns() - commit_start;
        measurement.outcome = simnet::ClientReplicationOutcome::Applied;
        measurement.total_receive_to_applied_cpu_time = simnet::steady_now_ns() - total_start;
        observe_client_measurement(measurements, csv, measurement);
        auto sent = simnet::TransportResult{};
        {
            SIMNET_TRACE_SCOPE_CATEGORY("client.snapshot_ack", simnet::LogCategory::Transport);
            auto const bytes = simnet::app::encode_snapshot_ack(ack_tracker.value);
            sent = transport.send(
                simnet::app::input_lane,
                simnet::TransportDelivery::ReliableSequenced,
                bytes
            );
        }
        if (!sent.ok) {
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Error,
                "client snapshot ACK send failed: " + sent.error.message
            );
            return ApplyPacketOutcome::Fatal;
        }
        if (group.chunk_count > 1U && !logged_multi_packet_application) {
            logged_multi_packet_application = true;
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Info,
                "client packet group applied group_id=" + std::to_string(group.group_id)
                    + " encoded_bytes=" + std::to_string(encoded_bytes.size())
                    + " chunks=" + std::to_string(group.chunk_count)
                    + " application_packet_bytes=" + std::to_string(group.total_packet_bytes)
                    + " canonical_entities=" + std::to_string(applied.final_entities)
                    + " ack_sequence=" + std::to_string(ack_tracker.value.newest_applied_snapshot)
            );
        }

        if (simnet::log_enabled(simnet::LogLevel::Debug)) {
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Debug,
                "client snapshot applied tick=" + std::to_string(applied.tick)
                    + " sequence=" + std::to_string(decoded.report.sequence)
                    + " entities=" + std::to_string(applied.final_entities)
            );
        }
        return ApplyPacketOutcome::Applied;
    }

    [[nodiscard]] bool apply_application_control(
        simnet::ReceivedPacket const& control,
        simnet::app::ClientRole requested_role,
        bool& simulation_paused,
        bool& pause_state_received,
        bool& join_accepted,
        simnet::EntityNetId& player_id,
        simnet::ClientReplicationCsvWriter& csv
    )
    {
        auto message = simnet::app::AppMessage{};
        if (!simnet::app::decode_app_message(control.payload, message)) {
            simnet::log(
                simnet::LogCategory::Transport,
                simnet::LogLevel::Error,
                "client received invalid application-control message"
            );
            return false;
        }

        if (message.kind == simnet::app::AppMessageKind::JoinAccepted && !join_accepted
            && message.role == requested_role) {
            auto const accepted_role = message.role == simnet::app::ClientRole::Player
                ? std::string_view{"player"}
                : std::string_view{"stationary_observer"};
            if (!csv.set_accepted_gameplay_role(accepted_role)) {
                throw std::runtime_error(
                    "client replication CSV role assignment failed: " + std::string{csv.error()}
                );
            }
            join_accepted = true;
            player_id = message.player_id;
            simnet::log(
                simnet::LogCategory::Simulation,
                simnet::LogLevel::Info,
                "client join accepted role="
                    + std::
                        string{requested_role == simnet::app::ClientRole::Player ? "player" : "stationary_observer"}
                    + " player_id=" + std::to_string(player_id)
            );
            return true;
        }
        if (message.kind != simnet::app::AppMessageKind::PauseState) {
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
        if (changed) {
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
        try {
            auto const options = parse_options(argc, argv);
            auto const run_context = make_evidence_run_context(
                EvidenceProcessRole::Client,
                options.run_id.has_value() ? std::optional<std::string_view>{*options.run_id}
                                           : std::nullopt
            );
            auto const shared_config_source
                = options.shared_config_path.value_or(default_shared_config_path());
            auto const local_config_source
                = options.config_path.value_or(default_client_config_path());
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
            if (packetization.max_payload_bytes > local.transport.max_payload_bytes
                || (packetization.enabled && local.transport.send_size_policy != "enforce_limit")) {
                throw std::runtime_error(
                    "packetization payload limit must fit the hard transport payload limit"
                );
            }
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
                .identity = make_session_identity(shared, pipeline),
                .limits = {
                    .max_payload_bytes = local.transport.max_payload_bytes,
                    .size_policy = transport_send_size_policy(local.transport),
                },
            });
            if (!connected.ok) {
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
            if (replication_csv->enabled()) {
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
            if (requested_role == app::ClientRole::StationaryObserver) {
                stationary_observer = StationaryObserverState{
                    .position = {
                        local.gameplay.stationary_observer_position[0],
                        local.gameplay.stationary_observer_position[1],
                        local.gameplay.stationary_observer_position[2],
                    },
                    .interest_radius = local.visualization.stationary_observer_interest_radius,
                    .vertical_fov_degrees
                    = local.visualization.stationary_observer_vertical_fov_degrees,
                };
            }
#if defined(SIMNET_ENABLE_RENDER)
            auto viewer = std::optional<Viewer>{};
            auto presentation = ClientPresentationState{};
            auto empty_presentation = WorldSnapshot{};
            if (local.visualization.enabled) {
                viewer.emplace(viewer_config(local.visualization), local.telemetry.log_directory);
            }
#endif

            auto stats = RuntimeStats{};
            auto timer = RuntimeFrameTimer{};
            reset_frame_timer(timer);
            auto stop = StopRequest{};
            auto events = std::vector<TransportEvent>{};
            auto decode_state = ClientReplicationState{};
            auto latest_applied_sequence = SequenceId{};
            auto ack_tracker = SnapshotAckTracker{};
            auto recovery_request_state = app::ClientRecoveryRequestState{};
            auto sequence_gap_count = std::uint64_t{};
            auto reliable_promotion_count = std::uint64_t{};
            auto effective_snapshot_delivery = std::optional<TransportDelivery>{};
            auto reassembly_state = ReassemblyState{};
            auto decompressor = ZstdDecompressor{};
            auto decompression_scratch = std::vector<Byte>{};
            auto pending_compression_groups = std::vector<PendingCompressionGroup>{};
            pending_compression_groups.reserve(packetization.max_in_flight_groups);
            auto compression_report = ClientCompressionReport{.mode = compression.mode};
            auto snapshot_history = std::deque<RetainedClientSnapshot>{};
            auto logged_multi_packet_application = false;
            auto logged_compression_application = false;
            auto replication_measurements = ClientReplicationMeasurements{};
            auto connection_state = ClientConnectionState::Connecting;
            auto stop_cause = ClientStopCause::None;
            auto server_peer = std::optional<PeerId>{};
            auto authoritative_pause_state = false;
            auto pause_state_received = false;
            auto join_accepted = false;
            auto player_id = EntityNetId{};
            auto last_observer_interest_send_time = std::optional<Nanoseconds>{};
            auto last_observer_interest_forward = Vec3f{};
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
                auto polled = TransportResult{};
                {
                    SIMNET_TRACE_SCOPE_CATEGORY("client.transport_poll", LogCategory::Transport);
                    polled = transport.poll(events, 1);
                }
                if (!polled.ok) {
                    log(LogCategory::Transport,
                        LogLevel::Error,
                        "client transport poll failed: " + polled.error.message);
                    stop_cause = ClientStopCause::ProtocolError;
                    static_cast<void>(stop.request(ShutdownReason::FatalError));
                    break;
                }

                expire_incomplete_groups(packetization, reassembly_state, stats.raw_time);
                expire_pending_compression_groups(
                    pending_compression_groups,
                    stats.raw_time,
                    packetization.reassembly_timeout
                );

                for (auto& event : events) {
                    if (auto const* ready = std::get_if<PeerSessionReady>(&event)) {
                        connection_state = ClientConnectionState::SessionReady;
                        server_peer = ready->peer;
                        pause_state_received = false;
                        join_accepted = false;
                        player_id = 0U;
                        last_observer_interest_send_time.reset();
                        last_observer_interest_forward = {};
                        app::record_snapshot_progress(recovery_request_state);
                        effective_snapshot_delivery.reset();
                        clear_reassembly_state(reassembly_state);
                        pending_compression_groups.clear();
#if defined(SIMNET_ENABLE_RENDER)
                        game_view_initialized = false;
                        player_input_was_active = false;
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
                        if (!joined.ok) {
                            log(LogCategory::Transport,
                                LogLevel::Error,
                                "client join request send failed: " + joined.error.message);
                            stop_cause = ClientStopCause::ProtocolError;
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                            break;
                        }
                    } else if (auto const* packet = std::get_if<ReceivedPacket>(&event)) {
                        auto valid = connection_state == ClientConnectionState::SessionReady;
                        if (valid && packet->lane == app::control_lane) {
                            valid = packet->delivery == TransportDelivery::ReliableSequenced
                                && apply_application_control(
                                        *packet,
                                        requested_role,
                                        authoritative_pause_state,
                                        pause_state_received,
                                        join_accepted,
                                        player_id,
                                        *replication_csv
                                );
                        } else if (valid && packet->lane == app::snapshot_lane) {
                            effective_snapshot_delivery = packet->delivery;
                            if (delivery == TransportDelivery::ReliableSequenced) {
                                valid = packet->delivery == TransportDelivery::ReliableSequenced;
                            } else if (packet->delivery == TransportDelivery::ReliableSequenced) {
                                ++reliable_promotion_count;
                            }
                            auto application_packet = ByteSpan{packet->payload};
                            if (valid && compression.mode == app::CompressionMode::PerPacket) {
                                if (has_compression_envelope(application_packet)) {
                                    auto const decompressed = decompress_bytes(
                                        decompressor,
                                        application_packet,
                                        {
                                            .max_uncompressed_bytes
                                            = packetization.max_payload_bytes,
                                            .max_output_bytes = local.transport.max_payload_bytes,
                                        },
                                        decompression_scratch
                                    );
                                    compression_report.latest_input_bytes
                                        = decompressed.input_bytes;
                                    compression_report.latest_encoding = decompressed.encoding;
                                    compression_report.latest_payload_bytes
                                        = decompressed.encoded_payload_bytes;
                                    compression_report.latest_envelope_bytes
                                        = decompressed.envelope_bytes;
                                    compression_report.latest_output_bytes
                                        = decompressed.output_bytes;
                                    compression_report.decompression_cpu_time
                                        += decompressed.decompression_cpu_time;
                                    if (!decompressed.valid
                                        || decompressed.encoding != CompressionEncoding::Zstd) {
                                        ++compression_report.invalid_payload_count;
                                        log(LogCategory::Transport,
                                            LogLevel::Warn,
                                            "client rejected compressed snapshot packet: "
                                                + decompressed.error);
                                        continue;
                                    }
                                    ++compression_report.compressed_packet_count;
                                    application_packet = decompression_scratch;
                                } else {
                                    ++compression_report.raw_packet_count;
                                }
                            }
                            auto reassembled = valid
                                ? accept_group_packet(
                                      packetization,
                                      reassembly_state,
                                      application_packet,
                                      stats.raw_time
                                  )
                                : ReassemblyResult{
                                      .kind = ReassemblyResultKind::Invalid,
                                      .error = "snapshot delivery does not match configuration",
                                  };
                            if (compression.mode == app::CompressionMode::PerPacket
                                && reassembled.group_id != 0U
                                && reassembled.kind != ReassemblyResultKind::Invalid
                                && reassembled.kind != ReassemblyResultKind::LimitExceeded
                                && reassembled.kind != ReassemblyResultKind::Stale) {
                                record_group_transport_bytes(
                                    pending_compression_groups,
                                    reassembled.group_id,
                                    static_cast<std::uint32_t>(packet->payload.size()),
                                    stats.raw_time,
                                    packetization.max_in_flight_groups
                                );
                            }
                            if (reassembled.kind == ReassemblyResultKind::Complete) {
                                auto encoded_bytes = ByteSpan{reassembled.completed.bytes};
                                if (compression.mode == app::CompressionMode::WholeUpdate) {
                                    auto const decompressed = decompress_bytes(
                                        decompressor,
                                        encoded_bytes,
                                        {
                                            .max_uncompressed_bytes = packetization.max_group_bytes,
                                            .max_output_bytes = packetization.max_group_bytes,
                                        },
                                        decompression_scratch
                                    );
                                    compression_report.latest_input_bytes
                                        = decompressed.input_bytes;
                                    compression_report.latest_encoding = decompressed.encoding;
                                    compression_report.latest_payload_bytes
                                        = decompressed.encoded_payload_bytes;
                                    compression_report.latest_envelope_bytes
                                        = decompressed.envelope_bytes;
                                    compression_report.latest_output_bytes
                                        = decompressed.output_bytes;
                                    compression_report.decompression_cpu_time
                                        += decompressed.decompression_cpu_time;
                                    if (!decompressed.valid) {
                                        ++compression_report.invalid_payload_count;
                                        log(LogCategory::Transport,
                                            LogLevel::Warn,
                                            "client rejected compressed snapshot group: "
                                                + decompressed.error);
                                        continue;
                                    }
                                    encoded_bytes = decompression_scratch;
                                }
                                auto const outer_bytes
                                    = compression.mode == app::CompressionMode::PerPacket
                                    ? reassembled.completed.group_id == 0U
                                        ? static_cast<std::uint32_t>(packet->payload.size())
                                        : group_transport_bytes(
                                              pending_compression_groups,
                                              reassembled.completed.group_id,
                                              reassembled.completed.total_packet_bytes
                                          )
                                    : reassembled.completed.total_packet_bytes;
                                auto const apply_outcome = apply_packet(
                                    reassembled.completed,
                                    encoded_bytes,
                                    outer_bytes,
                                    packetization.enabled,
                                    reassembly_state,
                                    pipeline,
                                    decode_state,
                                    latest_applied_sequence,
                                    ack_tracker,
                                    snapshot_history,
                                    world,
                                    transport,
                                    stats,
                                    replication_measurements,
                                    *replication_csv,
                                    logged_multi_packet_application,
                                    recovery_request_state,
                                    sequence_gap_count
                                );
                                valid = apply_outcome != ApplyPacketOutcome::Fatal;
                                if (apply_outcome == ApplyPacketOutcome::Applied) {
                                    compression_report.latest_completed_representation_bytes
                                        = static_cast<std::uint32_t>(encoded_bytes.size());
                                    compression_report.latest_completed_transport_bytes
                                        = outer_bytes;
                                    compression_report.canonical_entity_count
                                        = snapshot_history.empty()
                                        ? 0U
                                        : static_cast<std::uint32_t>(
                                              snapshot_history.back().snapshot.size()
                                          );
                                    std::erase_if(
                                        pending_compression_groups,
                                        [&](PendingCompressionGroup const& group) {
                                            return group.group_id <= reassembled.completed.group_id;
                                        }
                                    );
                                    if (compression.mode != app::CompressionMode::None
                                        && !logged_compression_application) {
                                        logged_compression_application = true;
                                        log(LogCategory::Transport,
                                            LogLevel::Info,
                                            "client compressed group applied group_id="
                                                + std::to_string(reassembled.completed.group_id)
                                                + " compression_mode="
                                                + std::string{app::compression_mode_name(
                                                    compression.mode
                                                )}
                                                + " representation_bytes="
                                                + std::to_string(encoded_bytes.size())
                                                + " transport_bytes=" + std::to_string(outer_bytes)
                                                + " compressed_packets="
                                                + std::to_string(
                                                    compression_report.compressed_packet_count
                                                )
                                                + " raw_packets="
                                                + std::to_string(
                                                    compression_report.raw_packet_count
                                                )
                                                + " canonical_entities="
                                                + std::to_string(
                                                    compression_report.canonical_entity_count
                                                )
                                                + " ack_sequence="
                                                + std::to_string(
                                                    ack_tracker.value.newest_applied_snapshot
                                                ));
                                    }
                                }
                            } else if (
                                reassembled.kind == ReassemblyResultKind::Invalid
                                || reassembled.kind == ReassemblyResultKind::LimitExceeded
                            ) {
                                if (compression.mode == app::CompressionMode::PerPacket
                                    && reassembled.group_id != 0U) {
                                    std::erase_if(
                                        pending_compression_groups,
                                        [&](PendingCompressionGroup const& group) {
                                            return group.group_id == reassembled.group_id;
                                        }
                                    );
                                }
                                log(LogCategory::Transport,
                                    LogLevel::Warn,
                                    "client rejected snapshot chunk: " + reassembled.error);
                            }
                        } else if (valid) {
                            valid = false;
                        }
                        if (!valid) {
                            stop_cause = ClientStopCause::ProtocolError;
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                            break;
                        }
                    } else if (auto const* disconnected = std::get_if<PeerDisconnected>(&event)) {
                        auto const was_session_ready
                            = connection_state == ClientConnectionState::SessionReady;
                        connection_state = ClientConnectionState::Disconnected;
                        clear_reassembly_state(reassembly_state);
                        pending_compression_groups.clear();
                        if (!was_session_ready && disconnected->code == DisconnectCode::Timeout) {
                            stop_cause = ClientStopCause::ConnectionTimeout;
                            log(LogCategory::Transport,
                                LogLevel::Error,
                                "client connection or session handshake timed out peer="
                                    + std::to_string(disconnected->peer));
                        } else if (
                            was_session_ready && disconnected->code == DisconnectCode::None
                        ) {
                            stop_cause = ClientStopCause::RemoteShutdown;
                            log(LogCategory::Transport,
                                LogLevel::Info,
                                "client received remote shutdown peer="
                                    + std::to_string(disconnected->peer));
                        } else if (was_session_ready) {
                            stop_cause = ClientStopCause::ConnectionLost;
                            log(LogCategory::Transport,
                                LogLevel::Error,
                                "client connection lost peer="
                                    + std::to_string(disconnected->peer));
                        } else {
                            stop_cause = ClientStopCause::ProtocolError;
                            log(LogCategory::Transport,
                                LogLevel::Error,
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
                        log(LogCategory::Transport,
                            LogLevel::Error,
                            "client transport error: " + error->message);
                        stop_cause = ClientStopCause::ProtocolError;
                        static_cast<void>(stop.request(ShutdownReason::FatalError));
                        break;
                    }
                }

                if (replication_csv->needs_drain() && !replication_csv->drain()) {
                    throw std::runtime_error(
                        "client replication CSV drain failed: "
                        + std::string{replication_csv->error()}
                    );
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
                        log(LogCategory::Render,
                            LogLevel::Error,
                            "client presentation interpolation failed");
                        stop_cause = ClientStopCause::ProtocolError;
                        static_cast<void>(stop.request(ShutdownReason::FatalError));
                    } else if (displayed_snapshot != nullptr) {
                        auto const has_pair = snapshot_history.size() >= 2U
                            && snapshot_history[snapshot_history.size() - 2U].snapshot.tick
                                < snapshot_history.back().snapshot.tick;
                        auto const interpolation_active = local.visualization.interpolation_enabled
                            && !(pause_state_received && authoritative_pause_state) && has_pair;
                        auto const interpolation = RenderInterpolationInfo{
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
                        SIMNET_TRACE_PLOT("client.render.interpolation_alpha", interpolation.alpha);
                        auto const sequence = latest_applied_sequence == 0
                            ? std::optional<SequenceId>{}
                            : std::optional<SequenceId>{latest_applied_sequence};
                        auto viewer_result = ViewerResult{};
                        auto game_camera = requested_role == app::ClientRole::Player
                            ? player_game_camera(*displayed_snapshot, player_id)
                            : std::optional<GameCameraView>{};
                        auto stationary_observer_view = std::optional<StationaryObserverView>{};
                        if (stationary_observer.has_value()) {
                            stationary_observer_view = StationaryObserverView{
                                .position = stationary_observer->position,
                                .forward = app::stationary_observer_forward(*stationary_observer),
                                .interest_radius = stationary_observer->interest_radius,
                                .vertical_fov_degrees = stationary_observer->vertical_fov_degrees,
                            };
                        }
                        if (requested_role == app::ClientRole::Player && join_accepted
                            && game_camera.has_value() && !game_view_initialized) {
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
                                server_peer,
                                ack_tracker.value,
                                effective_snapshot_delivery,
                                sequence_gap_count,
                                recovery_request_state.sent_count,
                                recovery_request_state.missing_baseline_rejection_count,
                                reliable_promotion_count,
                                reassembly_state.report,
                                compression_report,
                                snapshot_history.empty()
                                    ? std::optional<std::uint32_t>{}
                                    : std::optional<std::uint32_t>{static_cast<std::uint32_t>(
                                          snapshot_history.back().snapshot.size()
                                      )},
                                requested_role == app::ClientRole::Player
                                    ? std::string_view{"player"}
                                    : std::string_view{"stationary observer"},
                                std::move(stationary_observer_view),
                                std::move(game_camera),
                                run_setup.view()
                            ));
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
                        if (viewer_result.toggle_simulation_pause_requested
                            && pause_state_received) {
                            auto const bytes = app::encode_app_message({
                                .kind = app::AppMessageKind::PauseSetRequest,
                                .paused = !authoritative_pause_state,
                            });
                            auto const sent = transport.send(
                                app::control_lane,
                                TransportDelivery::ReliableSequenced,
                                bytes
                            );
                            if (!sent.ok) {
                                log(LogCategory::Transport,
                                    LogLevel::Error,
                                    "client pause request send failed: " + sent.error.message);
                                stop_cause = ClientStopCause::ProtocolError;
                                static_cast<void>(stop.request(ShutdownReason::FatalError));
                            }
                        }
                        if (!stop.requested() && requested_role == app::ClientRole::Player
                            && join_accepted) {
                            auto const input_active = viewer_result.camera_mode == CameraMode::Game;
                            auto const input = input_active
                                ? player_input_message(viewer_result.player_input)
                                : app::PlayerInputMessage{};
                            if (input_active || player_input_was_active) {
                                auto const bytes = app::encode_player_input(input);
                                auto const sent = transport.send(
                                    app::input_lane,
                                    TransportDelivery::UnreliableSequenced,
                                    bytes
                                );
                                if (!sent.ok) {
                                    log(LogCategory::Transport,
                                        LogLevel::Error,
                                        "client player-input send failed: " + sent.error.message);
                                    stop_cause = ClientStopCause::ProtocolError;
                                    static_cast<void>(stop.request(ShutdownReason::FatalError));
                                }
                            }
                            player_input_was_active = input_active;
                        }
                    }
                }
#endif

                if (!stop.requested() && stationary_observer.has_value() && join_accepted
                    && pipeline.area_of_interest.mode != AreaOfInterestMode::None) {
                    auto const forward = app::stationary_observer_forward(*stationary_observer);
                    auto const direction_changed = forward.x != last_observer_interest_forward.x
                        || forward.y != last_observer_interest_forward.y
                        || forward.z != last_observer_interest_forward.z;
                    auto const since_last = last_observer_interest_send_time.has_value()
                        ? stats.raw_time - *last_observer_interest_send_time
                        : app::stationary_observer_interest_heartbeat_interval;
                    auto const update_due = !last_observer_interest_send_time.has_value()
                        || (direction_changed
                            && since_last >= app::stationary_observer_interest_min_interval)
                        || since_last >= app::stationary_observer_interest_heartbeat_interval;
                    if (update_due) {
                        auto const bytes = app::encode_stationary_observer_interest({
                            .position = stationary_observer->position,
                            .forward = forward,
                        });
                        auto const sent = transport.send(
                            app::input_lane,
                            TransportDelivery::UnreliableSequenced,
                            bytes
                        );
                        if (!sent.ok) {
                            log(LogCategory::Transport,
                                LogLevel::Error,
                                "client observer-interest send failed: " + sent.error.message);
                            stop_cause = ClientStopCause::ProtocolError;
                            static_cast<void>(stop.request(ShutdownReason::FatalError));
                        } else {
                            last_observer_interest_send_time = stats.raw_time;
                            last_observer_interest_forward = forward;
                        }
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
            if (!replication_csv->close()) {
                throw std::runtime_error(
                    "client replication CSV close failed: " + std::string{replication_csv->error()}
                );
            }
            log_client_replication_measurements(replication_measurements);
            auto const canonical_entities
                = snapshot_history.empty() ? 0U : snapshot_history.back().snapshot.size();
            auto const canonical_fingerprint = snapshot_history.empty()
                ? 0U
                : app::snapshot_diagnostic_fingerprint(snapshot_history.back().snapshot);
            log(LogCategory::Transport,
                LogLevel::Info,
                "client snapshot delivery mode=" + shared.snapshot_delivery.mode + " effective="
                    + std::
                        string{effective_snapshot_delivery.has_value() ? app::transport_delivery_name(*effective_snapshot_delivery) : "unavailable"}
                    + " applied_sequence=" + std::to_string(latest_applied_sequence)
                    + " ack_sequence=" + std::to_string(ack_tracker.value.newest_applied_snapshot)
                    + " canonical_entities=" + std::to_string(canonical_entities)
                    + " canonical_fingerprint=" + std::to_string(canonical_fingerprint)
                    + " sequence_gaps=" + std::to_string(sequence_gap_count)
                    + " recovery_requests=" + std::to_string(recovery_request_state.sent_count)
                    + " reliable_promotions=" + std::to_string(reliable_promotion_count));
            log(LogCategory::Simulation,
                LogLevel::Info,
                "client runtime stopped reason=" + std::string{shutdown_reason_name(stop.reason())}
                    + " cause=" + std::string{client_stop_cause_name(stop_cause)} + " frames="
                    + std::to_string(stats.frames) + " ticks=" + std::to_string(stats.ticks)
                    + " runtime_ns=" + std::to_string(stats.raw_time.count()));
            return stop.reason() == ShutdownReason::FatalError ? 1 : 0;
        } catch (std::exception const& error) {
            auto close_error = std::string{};
            if (replication_csv.has_value() && !replication_csv->close()) {
                close_error = std::string{replication_csv->error()};
            }
            std::cerr << "Client failed: " << error.what();
            if (!close_error.empty()) {
                std::cerr << ". Evidence close failed: " << close_error;
            }
            std::cerr << '\n';
            return 1;
        }
    }
}
