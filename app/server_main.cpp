#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include <simnet/telemetry_trace.hpp>

import simnet.config;
import simnet.core;
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

        auto pipeline = simnet::make_raw_full_replace_pipeline();
        pipeline.techniques |= simnet::PipelineTechniqueFlags::SendInterval;
        pipeline.send_interval.interval_ticks = 2;

        auto replication_state = simnet::ClientReplicationState {};
        auto decode_state = simnet::ClientReplicationState {};
        auto pipeline_scratch = simnet::PipelineScratch {};

        for (simnet::Tick tick = 0; tick < 3; ++tick) {
            auto const tick_snapshot = simnet::make_synthetic_world_snapshot(synthetic_settings, tick);
            auto encode_output = simnet::encode_snapshot(
                pipeline,
                replication_state,
                pipeline_scratch,
                { .snapshot = &tick_snapshot }
            );
            log_encode_report(encode_output.report);

            if (encode_output.kind == simnet::EncodeResultKind::Skipped) {
                continue;
            }

            auto decode_output = simnet::decode_packet(
                pipeline,
                decode_state,
                pipeline_scratch,
                { .packet = &encode_output.packet }
            );
            auto const patch_validation = simnet::validate_client_snapshot_patch(decode_output.patch);

            simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
                decode_output.report.valid ? "pipeline decode: valid"
                                           : "pipeline decode: " + decode_output.report.error);
            simnet::log(simnet::LogCategory::Pipeline, simnet::LogLevel::Info,
                patch_validation.valid ? "pipeline decoded patch validation: valid"
                                       : "pipeline decoded patch validation: " + patch_validation.message);
        }

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
