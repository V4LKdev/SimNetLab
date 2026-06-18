#include <atomic>
#include <csignal>
#include <thread>
#include <flecs.h>


#include "../core/utils/controller.hpp"
#include "../server/network_server.hpp"
#include "telemetry.hpp"
#include "config/config_loader.hpp"
#include "config/sim_config.hpp"
#include "../game/shared/components.hpp"
#include "ecs/world_factory.hpp"
#include "../server/ecs/server_ecs.hpp"

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

    // --- ecs ---
    flecs::world server_world;
    simnet::ecs::set_world_thread_count(server_world);
    simnet::ecs::register_server_components(server_world);

    // *After* components are registered, set specific values to singletons
    server_world.set<simnet::SimConfig>(cfg);
    server_world.set<simnet::ecs::ServerContext>({&server});
   
    simnet::server::ecs::init_server_ecs(server_world);

    // --- timestep controller ---
    simnet::core::TimestepController controller(cfg);
    auto last_time = Clock::now();

    // --- main loop ---
    while (g_running && server.is_running()) {
        TELEM_TRACY_ZONE("ServerFrame");

        // 1. Wait until next network event or next sim tick (large packet bursts could potentially starve the sim advancement)
        int timeout_ms = controller.ms_until_next_tick();
        ENetEvent ev;
        if (enet_host_service(server.get_host(), &ev, static_cast<enet_uint32>(timeout_ms)) > 0) {
            do {
                server.process_event(ev);
            } while (enet_host_service(server.get_host(), &ev, 0) > 0);
        }

        // 2. feed elapsed time
        const auto now = Clock::now();
        const int64_t elapsed_ns = (now - last_time).count();
        last_time = now;

        controller.advance(elapsed_ns);

        // 3. run authoritative simulation ticks
        while (controller.try_step()) {
            server_world.progress(cfg.dt_seconds());
        }

        // 4. Housekeeping
        server.check_timeouts();

        // 5. Send snapshot if steps > 0

        // 6. periodically query for hotreloading config file.

        TELEM_TRACY_FRAME("ServerFrame");
    }

    TELEM_LOG_INFO("Server shutting down");
    simnet::telemetry::shutdown();
    return 0;
}
