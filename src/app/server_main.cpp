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
    simnet::net::NetManager net(simnet::net::NetRole::server, sim_cfg);

    if (!net.is_initialized()) {
        TELEM_LOG_ERROR("Failed to initiate server network");
        return EXIT_FAILURE;
    }

    // Temp callback test
    net.set_on_connected([&](simnet::net::PeerID id) {
        TELEM_LOG_INFO("Client {} connected", id);
        // TODO: send config, spawn player entity
    });
    net.set_on_disconnected([&](simnet::net::PeerID id, simnet::net::DisconnectReason reason) {
        TELEM_LOG_INFO("Client {} disconnected, reason {}", id, static_cast<int>(reason));
        // TODO: remove player entity
    });
    net.set_on_rejected([&](simnet::net::PeerID id, simnet::net::RejectReason reason) {
        TELEM_LOG_WARN("Client {} rejected, reason {}", id, static_cast<int>(reason));
    });


    // --- ecs ---
    simnet::game::server::ServerWorld world(sim_cfg, &net);

    simnet::core::utils::TimestepController controller(sim_cfg);
    auto last_time = Clock::now();

    // --- main loop ---
    while (g_running) {
        try {
            TELEM_TRACY_ZONE("ServerFrame");

            // 1. Wait until a network event arrives OR the next simulation tick is due
            int timeout_ms = controller.ms_until_next_tick();
            timeout_ms = std::min(timeout_ms, 100);
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

            // 5. Periodically query for hotreloading config file

            TELEM_TRACY_FRAME("ServerFrame");
        } catch (const std::exception &e) {
            TELEM_LOG_ERROR("Fatal exception in main loop: {}", e.what());
            break;
        } catch (...) {
            TELEM_LOG_ERROR("Unknown fatal exception in main loop");
            break;
        }
    }

    TELEM_FLUSH_METRICS();
    TELEM_LOG_INFO("Server shutting down");
    TELEM_SHUTDOWN();
    return 0;
}
