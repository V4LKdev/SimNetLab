#include <exception>
#include <iostream>
#include <string>

#include <simnet/telemetry_trace.hpp>

import simnet.config;
import simnet.core;
import game_server;
import pipeline;
import simnet.telemetry;
import transport;

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
