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

    // Dynamic path resolution
    std::string log_path = simnet::core::utils::resolve_runtime_path("log/server_telemetry.log");
    std::string config_path = simnet::core::utils::resolve_runtime_path("config/config.json");

    TELEM_INIT("SimNetLab_Server", log_path);

    // --- configuration ---
    simnet::config::SimConfig sim_cfg = simnet::config::SimConfig::default_config();
    if (!simnet::config::load_json(config_path, sim_cfg)) {
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

        // 1. Wait until a network event arrives OR the next simulation tick is due (whichever comes first)
        int timeout_ms = controller.ms_until_next_tick();
        timeout_ms = std::min(timeout_ms, 100);
        // just clamp to wait atmost 100ms (should be catched by the controller anyways)
        net.update(Clock::now(), timeout_ms);

        // 2. Measure how much real time has actually passed
        auto now = Clock::now();
        int64_t elapsed_ns = (now - last_time).count();
        last_time = now;

        // 3. Advance the fixed‑step accumulator
        controller.advance(elapsed_ns);

        // 4. Run as many simulation ticks as the accumulator permits
        while (controller.try_step()) {
            world.run_tick(sim_cfg.dt_seconds());
        }

        // 4. Periodically query for hotreloading config file.

        TELEM_TRACY_FRAME("ServerFrame");
    }

    TELEM_FLUSH_METRICS();
    TELEM_LOG_INFO("Server shutting down");
    TELEM_SHUTDOWN();
    return 0;
}
