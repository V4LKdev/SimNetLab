#include <exception>
#include <iostream>
#include <string>

#include <simnet/telemetry_trace.hpp>

import simnet.config;
import simnet.core;
import game_client;
import simnet.pipeline;

#if SIMNET_ENABLE_RENDER
import render;
#endif

import simnet.telemetry;
import transport;

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
