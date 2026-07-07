#include <algorithm>
#include <exception>
#include <flecs.h>
#include <iostream>
#include <string>

#include <simnet/telemetry_trace.hpp>

import simnet.config;
import simnet.core;
import simnet.game_client;
import simnet.pipeline;
import simnet.snapshot;
import simnet.synthetic;
import simnet.telemetry;

namespace
{
    [[nodiscard]] simnet::Aabb3f summarize_bounds(simnet::WorldSnapshot const& snapshot)
    {
        if (snapshot.positions.empty()) {
            return {};
        }

        auto bounds = simnet::Aabb3f {
            .min = snapshot.positions.front(),
            .max = snapshot.positions.front(),
        };

        for (auto const position : snapshot.positions) {
            bounds.min.x = std::min(bounds.min.x, position.x);
            bounds.min.y = std::min(bounds.min.y, position.y);
            bounds.min.z = std::min(bounds.min.z, position.z);
            bounds.max.x = std::max(bounds.max.x, position.x);
            bounds.max.y = std::max(bounds.max.y, position.y);
            bounds.max.z = std::max(bounds.max.z, position.z);
        }

        return bounds;
    }

    void log_encode_report(simnet::EncodeReport const& report)
    {
        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
            "pipeline tick: " + std::to_string(report.tick));
        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
            report.emitted ? "pipeline emitted: true" : "pipeline emitted: false");
        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
            report.skipped ? "pipeline skipped: true" : "pipeline skipped: false");
        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
            "pipeline packet bytes: " + std::to_string(report.packet_bytes));
        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
            "pipeline selected entities: " + std::to_string(report.selected_entities));
        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
            "pipeline upserts: " + std::to_string(report.upsert_count));
        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
            report.budget_exceeded ? "pipeline packet budget exceeded: true"
                                   : "pipeline packet budget exceeded: false");
    }

    [[nodiscard]] std::string snapshot_kind_name(simnet::SnapshotKind kind)
    {
        return kind == simnet::SnapshotKind::FullReplace ? "FullReplace" : "Patch";
    }

    [[nodiscard]] std::uint32_t client_entity_count(flecs::world const& world)
    {
        auto const* index = world.try_get<simnet::ClientReplicationIndex>();
        return index == nullptr ? 0U : static_cast<std::uint32_t>(index->size());
    }

    void log_apply_report(simnet::ApplyPatchReport const& report, flecs::world const& world)
    {
        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
            "client apply tick: " + std::to_string(report.tick));
        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
            "client apply kind: " + snapshot_kind_name(report.kind));
        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
            "client apply upserts: " + std::to_string(report.upsert_count));
        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
            "client apply deletes: " + std::to_string(report.delete_count));
        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
            "client entity count: " + std::to_string(client_entity_count(world)));
        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
            report.valid ? "client apply: valid" : "client apply: " + report.error);
    }

    void run_local_replication_smoke(
        std::string const& label,
        simnet::PipelineDefinition const& pipeline,
        simnet::SyntheticSnapshotSettings const& settings,
        simnet::Tick tick_count
    )
    {
        simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
            "local replication smoke: " + label);

        auto client_world = flecs::world {};
        simnet::register_client_game(client_world);

        auto replication_state = simnet::ClientReplicationState {};
        auto decode_state = simnet::ClientReplicationState {};
        auto pipeline_scratch = simnet::PipelineScratch {};

        for (simnet::Tick tick = 0; tick < tick_count; ++tick) {
            auto const tick_snapshot = simnet::make_synthetic_world_snapshot(settings, tick);
            auto encode_output = simnet::encode_snapshot(
                pipeline,
                replication_state,
                pipeline_scratch,
                { .snapshot = &tick_snapshot }
            );
            log_encode_report(encode_output.report);

            if (encode_output.kind == simnet::EncodeResultKind::Skipped) {
                simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
                    "client entity count unchanged: " + std::to_string(client_entity_count(client_world)));
                continue;
            }

            auto decode_output = simnet::decode_packet(
                pipeline,
                decode_state,
                pipeline_scratch,
                { .packet = &encode_output.packet }
            );
            simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
                decode_output.report.valid ? "pipeline decode: valid"
                                           : "pipeline decode: " + decode_output.report.error);
            if (!decode_output.report.valid) {
                continue;
            }

            auto apply_report = simnet::apply_client_snapshot_patch(client_world, decode_output.patch);
            log_apply_report(apply_report, client_world);
        }
    }
}

int main()
{
    bool telemetry_started = false;
    try {
        SIMNET_TRACE_SCOPE("Server main");

        // Defaults are copied into the active build tree by simnet_config.
        auto const shared_config = simnet::load_shared_config(simnet::default_shared_config_path());
        auto const server_config = simnet::load_server_config(simnet::default_server_config_path());

        simnet::initialize_telemetry(server_config.telemetry);
        telemetry_started = true;

        auto const network_fingerprint = simnet::fingerprint_network_compatibility(shared_config);
        auto const runtime_fingerprint = simnet::fingerprint_runtime_config(shared_config, server_config);

        // Fingerprints give early confirmation that runtime config is coherent.
        simnet::log(simnet::LogCategory::Config, simnet::LogLevel::Info,
            "server network fingerprint: " + std::to_string(network_fingerprint.value));
        simnet::log(simnet::LogCategory::Config, simnet::LogLevel::Info,
            "server runtime fingerprint: " + std::to_string(runtime_fingerprint.value));

        auto const synthetic_settings = simnet::SyntheticSnapshotSettings {
            .seed = shared_config.run.seed,
            .entity_count = shared_config.simulation.initial_boid_count,
            .bounds = simnet::make_centered_bounds(shared_config.simulation.world_half),
            .pattern = simnet::SyntheticPattern::RandomUniform,
        };
        auto const snapshot = simnet::make_synthetic_world_snapshot(synthetic_settings, 0);
        auto const validation = simnet::validate_world_snapshot(snapshot);
        auto const bounds = summarize_bounds(snapshot);

        simnet::log(simnet::LogCategory::Snapshot, simnet::LogLevel::Info,
            "synthetic snapshot tick: " + std::to_string(snapshot.tick));
        simnet::log(simnet::LogCategory::Snapshot, simnet::LogLevel::Info,
            "synthetic snapshot entities: " + std::to_string(snapshot.size()));
        if (!snapshot.empty()) {
            simnet::log(simnet::LogCategory::Snapshot, simnet::LogLevel::Info,
                "synthetic snapshot id range: " + std::to_string(snapshot.ids.front())
                    + ".." + std::to_string(snapshot.ids.back()));
        }
        simnet::log(simnet::LogCategory::Snapshot, simnet::LogLevel::Info,
            "synthetic snapshot bounds min: "
                + std::to_string(bounds.min.x) + ", "
                + std::to_string(bounds.min.y) + ", "
                + std::to_string(bounds.min.z));
        simnet::log(simnet::LogCategory::Snapshot, simnet::LogLevel::Info,
            "synthetic snapshot bounds max: "
                + std::to_string(bounds.max.x) + ", "
                + std::to_string(bounds.max.y) + ", "
                + std::to_string(bounds.max.z));
        simnet::log(simnet::LogCategory::Snapshot, simnet::LogLevel::Info,
            validation.valid ? "synthetic snapshot validation: valid"
                             : "synthetic snapshot validation: " + validation.message);

        auto full_pipeline = simnet::make_raw_snapshot_pipeline();
        full_pipeline.codec = simnet::CodecKind::BitPacked;
        full_pipeline.techniques |= simnet::PipelineTechniqueFlags::SendInterval
            | simnet::PipelineTechniqueFlags::Quantization
            | simnet::PipelineTechniqueFlags::OctHeading;
        full_pipeline.send_interval.interval_ticks = 2;
        full_pipeline.quantization.position_bounds = simnet::make_centered_bounds(shared_config.simulation.world_half);
        run_local_replication_smoke("full snapshot bitpacked quantized oct", full_pipeline, synthetic_settings, 3);

        auto incremental_settings = synthetic_settings;
        incremental_settings.entity_count = 10;
        auto incremental_pipeline = simnet::make_raw_snapshot_pipeline();
        incremental_pipeline.techniques |= simnet::PipelineTechniqueFlags::SendInterval
            | simnet::PipelineTechniqueFlags::Incremental;
        incremental_pipeline.send_interval.interval_ticks = 2;
        incremental_pipeline.incremental.max_entities_per_packet = 4;
        run_local_replication_smoke("incremental patch round-robin", incremental_pipeline, incremental_settings, 5);

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
