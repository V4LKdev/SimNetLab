#include <atomic>
#include <csignal>

#include "controller.hpp"
#include "network_client.hpp"
#include "renderer.hpp"
#include "simulation.hpp"
#include "telemetry.hpp"
#include "config/ConfigLoader.hpp"
#include "ecs/world_client.hpp"


namespace {
    std::atomic<bool> g_running(true);

    void signal_handler(int sig)
    {
        g_running = false;
    }
}


int main()
{
    // --- signals and telemetry ---
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    simnet::telemetry::init("SimNetLab_Client", "log/client_telemetry.log");

    // --- configuration ---
    simnet::SimConfig cfg = simnet::SimConfig::default_config();
    if (!simnet::load_json("config/config.json", cfg)) {
        TELEM_LOG_INFO("No config.json provided - using defaults");
    }
    TELEM_LOG_INFO("Run fingerprint: 0x{:016x}", static_cast<unsigned long long>(cfg.fingerprint()));


    // --- network handshake ---
    simnet::client::network_client net;
    net.start_connect("127.0.0.1", 7777);

    // --- ecs world ---
    simnet::client::ClientWorld client_world(cfg);

    // --- rendering ---
    simnet::client::Renderer renderer(1920, 1080, "SimNetLab_Client", client_world.world());

    // --- main loop ---
    while (g_running && renderer.is_running()) {
        TELEM_TRACY_ZONE("ClientFrame");

        // 1. process network events
        net.service();

        // 2. render
        renderer.render();
        TELEM_TRACY_FRAME("ClientFrame");
    }

    // --- shutdown ---
    simnet::telemetry::shutdown();
    return 0;
}
