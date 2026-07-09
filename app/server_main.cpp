#include <exception>
#include <cstdint>
#include <flecs.h>
#include <iostream>
#include <string>

#include <simnet/telemetry_trace.hpp>

import simnet.config;
import simnet.core;
import simnet.game_server;
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

            SIMNET_TRACE_PLOT("server.snapshot_entities", static_cast<double>(extraction.entity_count));
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
