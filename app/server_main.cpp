#include <exception>
#include <cstdint>
#include <flecs.h>
#include <iostream>
#include <string>

#include <simnet/telemetry_trace.hpp>

import simnet.config;
import simnet.core;
import simnet.game_server;
import simnet.pipeline;
import simnet.snapshot;
import simnet.telemetry;

namespace
{
    constexpr std::uint32_t smoke_boid_count = 10;
    constexpr simnet::Tick smoke_tick_count = 5;

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
        for (std::uint32_t index = 0; index < smoke_boid_count; ++index) {
            auto const id = static_cast<simnet::EntityNetId>(index + 1U);
            static_cast<void>(simnet::upsert_authoritative_boid(world, make_smoke_boid(id, index, tick)));
        }

        if (tick == 2) {
            static_cast<void>(simnet::delete_authoritative_boid(world, smoke_boid_count));
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

    [[nodiscard]] simnet::PipelineDefinition make_server_smoke_pipeline(
        simnet::SharedConfig const& shared_config
    )
    {
        auto pipeline = simnet::make_raw_snapshot_pipeline();
        pipeline.codec = simnet::CodecKind::BitPacked;
        pipeline.techniques |= simnet::PipelineTechniqueFlags::SendInterval;
        pipeline.techniques |= simnet::PipelineTechniqueFlags::Quantization;
        pipeline.techniques |= simnet::PipelineTechniqueFlags::OctHeading;
        pipeline.send_interval.interval_ticks = 2;
        pipeline.quantization.position_bounds = simnet::make_centered_bounds(shared_config.simulation.world_half);
        return pipeline;
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
        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
            "authoritative snapshot producer smoke starting");

        auto world = flecs::world {};
        simnet::register_server_game(world);
        auto const pipeline = make_server_smoke_pipeline(shared_config);
        auto replication_state = simnet::ClientReplicationState {};
        auto pipeline_scratch = simnet::PipelineScratch {};

        for (simnet::Tick tick = 0; tick < smoke_tick_count; ++tick) {
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
                replication_state,
                pipeline_scratch,
                { .snapshot = &snapshot }
            );
            log_encode_report(encode_output);

            SIMNET_TRACE_PLOT("server.snapshot_entities", static_cast<double>(extraction.entity_count));
            SIMNET_TRACE_PLOT("server.encode_packet_bytes", static_cast<double>(encode_output.report.packet_bytes));
            SIMNET_TRACE_FRAME("server");
        }

        simnet::log(simnet::LogCategory::Simulation, simnet::LogLevel::Info,
            "authoritative runtime networking is not implemented yet");

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
