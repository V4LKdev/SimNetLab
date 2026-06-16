#include <atomic>
#include <csignal>


#include "controller.hpp"
#include "network_server.hpp"
#include "telemetry.hpp"
#include "config/ConfigLoader.hpp"
#include "config/SimConfig.hpp"
#include "simulation.hpp"

using Clock = std::chrono::steady_clock;

namespace {
    std::atomic<bool> g_running(true);

    void signal_handler(int /*sig*/)
    {
        g_running = false;
    }
}

int main()
{
    // --- signals and telemetry ---
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    simnet::telemetry::init("SimNetLab_Server", "log/server_telemetry.log");

    // --- configuration ---
    simnet::SimConfig cfg = simnet::SimConfig::default_config();
    if (!simnet::load_json("config/config.json", cfg)) {
        TELEM_LOG_INFO("No config.json provided - using defaults");
    }
    TELEM_LOG_INFO("Run fingerprint: 0x{:016x}", static_cast<unsigned long long>(cfg.fingerprint()));

    // --- network init ---
    simnet::server::network_server server(7777);

    // --- simulation ---
    simnet::sim::Simulation sim(cfg);
    simnet::core::TimestepController controller(sim, cfg);

    // --- main loop ---
    while (g_running && server.is_running()) {
        TELEM_TRACY_ZONE("ServerFrame");

        // 1. Wait until next network event or next sim tick
        int timeout_ms = controller.ms_until_next_tick();
        ENetEvent ev;
        if (enet_host_service(server.get_host(), &ev, static_cast<enet_uint32>(timeout_ms)) > 0) {
            do {
                server.process_event(ev);
            } while (enet_host_service(server.get_host(), &ev, 0) > 0);
        }

        // 2. Run simulation steps
        const int steps = controller.update();

        // 3. Housekeeping
        server.check_timeouts();

        // 4. Send snapshot if steps > 0

        TELEM_TRACY_FRAME("ServerFrame");
    }

    TELEM_LOG_INFO("Server shutting down");
    simnet::telemetry::shutdown();
    return 0;
}
