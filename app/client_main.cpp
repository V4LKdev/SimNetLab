#include <exception>
#include <iostream>
#include <string>

#include <simnet/telemetry_trace.hpp>

import simnet.config;
import simnet.core;
import game_client;
import pipeline;

#if SIMNET_ENABLE_RENDER
import render;
#endif

import simnet.telemetry;
import transport;

int main()
{
    try {
        SIMNET_TRACE_SCOPE("Client main");

        auto const shared_config = simnet::load_shared_config("config/shared_default.json");
        auto const client_config = simnet::load_client_config("config/client_default.json");

        simnet::log(simnet::LogCategory::Telemetry, simnet::LogLevel::Info, "client telemetry pre-init");
        simnet::initialize_telemetry(client_config.telemetry);

        auto const network_fingerprint = simnet::fingerprint_network_compatibility(shared_config);
        auto const runtime_fingerprint = simnet::fingerprint_runtime_config(shared_config, client_config);

        simnet::log(simnet::LogCategory::Config, simnet::LogLevel::Info,
            "client network fingerprint: " + std::to_string(network_fingerprint.value));
        simnet::log(simnet::LogCategory::Config, simnet::LogLevel::Info,
            "client runtime fingerprint: " + std::to_string(runtime_fingerprint.value));
        SIMNET_TRACE_PLOT("client.network_fingerprint", static_cast<double>(network_fingerprint.value));
        SIMNET_TRACE_PLOT("client.runtime_fingerprint", static_cast<double>(runtime_fingerprint.value));
        SIMNET_TRACE_FRAME("client");

        simnet::flush_telemetry();
        simnet::shutdown_telemetry();
        simnet::shutdown_telemetry();
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "Client failed: " << error.what() << '\n';
        simnet::shutdown_telemetry();
        return 1;
    }
}
