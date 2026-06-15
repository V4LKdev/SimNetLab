#include <atomic>
#include <csignal>

#include "controller.hpp"
#include "network_client.hpp"
#include "renderer.hpp"
#include "simulation.hpp"
#include "telemetry.hpp"
#include "config/ConfigLoader.hpp"


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

    // --- local simulation and rendering ---
    simnet::sim::Simulation sim(cfg);
    simnet::core::TimestepController controller(sim, cfg);

    simnet::client::Renderer renderer(1920, 1080, "SimNetLab_Client", sim.world());

    // --- main loop ---
    while (g_running && renderer.is_running()) {
        TELEM_TRACY_ZONE("ClientFrame");

        // 1. process network events (non-blocking)
        net.service();

        // 2. fixed-step simulation update
        // Currently unused, kept as documentation of the available sim to render API
        const int steps = controller.update();
        const double alpha = controller.get_interpolation_alpha();
        const auto tick = sim.current_tick();

        // 3. render
        renderer.render();
        TELEM_TRACY_FRAME("ClientFrame");
    }

    // --- shutdown ---
    simnet::telemetry::shutdown();
    return 0;
}
