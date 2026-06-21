#include <atomic>
#include <csignal>
#include <flecs.h>

#include "core/core.hpp"
#include "telemetry.hpp"

using Clock = std::chrono::steady_clock;

namespace {
    std::atomic<bool> g_running(true);

    void signal_handler(int sig)
    {
        g_running = false;
    }
}


/**
 *
 * Cleanup pass:
 *
 * specifically track client and server paths and lifecycles
 * change log from raw pointers to basic id
 * build the test file
 * check disconnect vs reject reasons and how and where they are used
 *
 * Named channels
 * Reject as disconnect reason
 */
int main()
{
    // --- signals and telemetry ---
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    simnet::telemetry::init("SimNetLab_Client", "log/client_telemetry.log");

    TELEM_LOG_TRACE("Trace check");

    // --- configuration ---
    // simnet::SimConfig cfg = simnet::SimConfig::default_config();
    // if (!simnet::load_json("config/config.json", cfg)) {
    //     TELEM_LOG_INFO("No config.json provided - using defaults");
    // }
    // TELEM_LOG_INFO("Run fingerprint: 0x{:016x}", static_cast<unsigned long long>(cfg.fingerprint()));

    // --- network init ---
    simnet::net::NetManager net;
    simnet::net::NetConfig net_cfg;

    if (!net.initialize(simnet::net::NetRole::client, net_cfg)) {
        TELEM_LOG_ERROR("Failed to initialize the client network");
        return EXIT_FAILURE;
    }

    // --- connect to server ---
    auto peer_id = net.connect("127.0.0.1", 7777);
    if (peer_id == 0) {
        TELEM_LOG_ERROR("Failed to initiate connection");
        return EXIT_FAILURE;
    }

    net.set_on_connected([](simnet::net::PeerID /*id*/) {
        TELEM_LOG_INFO("Connected to server!");
    });
    net.set_on_disconnected([](simnet::net::PeerID /*id*/, simnet::net::internal::DisconnectReason reason) {
        TELEM_LOG_INFO("Disconnected (reason={})", static_cast<uint8_t>(reason));
    });


    while (g_running) {
        auto now = Clock::now();
        net.update(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // // --- ecs world ---
    // flecs::world client_world;
    // client_world.set<simnet::SimConfig>(cfg);
    // simnet::ecs::set_world_thread_count(client_world);
    //
    // simnet::ecs::register_client_components(client_world);
    // simnet::client::ecs::init_client_ecs(client_world);
    //
    //
    // // --- timestep controller ---
    // simnet::core::TimestepController controller(cfg);
    // auto last_time = Clock::now();
    //
    // // --- rendering ---
    // simnet::client::Renderer renderer(1920, 1080, "SimNetLab_Client", client_world);
    //
    // // --- main loop ---
    // while (g_running && renderer.is_running()) {
    //     TELEM_TRACY_ZONE("ClientFrame");
    //
    //     // 1. process network events
    //     net.service();
    //
    //     // 2. feed elapsed time
    //     const auto now = Clock::now();
    //     const int64_t elapsed_ns = (now - last_time).count();
    //     last_time = now;
    //
    //     controller.advance(elapsed_ns);
    //
    //     // 3. run fixed tps simulation
    //     while (controller.try_step()) {
    //         // tick the simulation
    //     }
    //
    //     // 4. render every frame
    //     renderer.render();
    //     TELEM_TRACY_FRAME("ClientFrame");
    // }
    //
    // --- shutdown ---
    TELEM_FLUSH_METRICS();
    simnet::telemetry::shutdown();
    return 0;
}
