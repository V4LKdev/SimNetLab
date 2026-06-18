#include <atomic>
#include <csignal>
#include <flecs.h>

#include "../core/utils/controller.hpp"
#include "../client/network_client.hpp"
#include "../client/renderer.hpp"
#include "telemetry.hpp"
#include "config/config_loader.hpp"
#include "../client/ecs/client_ecs.hpp"
#include "ecs/world_factory.hpp"

using Clock = std::chrono::steady_clock;

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
    flecs::world client_world;
    client_world.set<simnet::SimConfig>(cfg);
    simnet::ecs::set_world_thread_count(client_world);

    simnet::ecs::register_client_components(client_world);
    simnet::client::ecs::init_client_ecs(client_world);


    // --- timestep controller ---
    simnet::core::TimestepController controller(cfg);
    auto last_time = Clock::now();

    // --- rendering ---
    simnet::client::Renderer renderer(1920, 1080, "SimNetLab_Client", client_world);

    // --- main loop ---
    while (g_running && renderer.is_running()) {
        TELEM_TRACY_ZONE("ClientFrame");

        // 1. process network events
        net.service();

        // 2. feed elapsed time
        const auto now = Clock::now();
        const int64_t elapsed_ns = (now - last_time).count();
        last_time = now;

        controller.advance(elapsed_ns);

        // 3. run fixed tps simulation
        while (controller.try_step()) {
            // tick the simulation
        }

        // 4. render every frame
        renderer.render();
        TELEM_TRACY_FRAME("ClientFrame");
    }

    // --- shutdown ---
    simnet::telemetry::shutdown();
    return 0;
}
