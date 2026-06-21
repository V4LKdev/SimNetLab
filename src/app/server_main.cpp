#include <atomic>
#include <csignal>
#include <thread>
#include <flecs.h>

#include "core/core.hpp"
#include "game/server/game_server.hpp"
#include "telemetry/telemetry.hpp"

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
    simnet::config::SimConfig sim_cfg = simnet::config::SimConfig::default_config();
    if (!simnet::config::load_json("config/config.json", sim_cfg)) {
        TELEM_LOG_INFO("No config.json provided - using defaults");
    }
    TELEM_LOG_INFO("Run fingerprint: 0x{:016x}", static_cast<unsigned long long>(sim_cfg.fingerprint()));

    // --- network init ---
    simnet::net::NetManager net;
    simnet::net::NetConfig net_cfg;
    net_cfg.port = 7777;
    net_cfg.max_peers = 10;

    if (!net.initialize(simnet::net::NetRole::server, net_cfg)) {
        TELEM_LOG_ERROR("Failed to initiate server network");
        return EXIT_FAILURE;
    }

    // --- ecs ---
    simnet::game::server::ServerWorld world(sim_cfg, &net);

    simnet::core::utils::TimestepController controller(sim_cfg);
    auto last_time = Clock::now();

    // --- main loop ---
    // TODO: check net server is running
    while (g_running) {
        TELEM_TRACY_ZONE("ServerFrame");

        // 1. Wait until next network event or next sim tick (large packet bursts could potentially starve the sim advancement)
        // int timeout_ms = controller.ms_until_next_tick();
        // ENetEvent ev;
        // if (enet_host_service(server.get_host(), &ev, static_cast<enet_uint32>(timeout_ms)) > 0) {
        //     do {
        //         server.process_event(ev);
        //     } while (enet_host_service(server.get_host(), &ev, 0) > 0);
        // }

        // 2. feed elapsed time
        const auto now = Clock::now();
        const int64_t elapsed_ns = (now - last_time).count();
        last_time = now;

        controller.advance(elapsed_ns);

        // 3. run authoritative simulation ticks
        while (controller.try_step()) {
            world.run_tick(sim_cfg.dt_seconds());
        }

        // 4. periodically query for hotreloading config file.

        TELEM_TRACY_FRAME("ServerFrame");
    }

    TELEM_FLUSH_METRICS();
    TELEM_LOG_INFO("Server shutting down");
    simnet::telemetry::shutdown();
    return 0;
}
