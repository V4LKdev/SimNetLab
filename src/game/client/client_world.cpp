#include "client_world.hpp"
#include "client_world.hpp"

#include <csignal>

#include "net/wire_types.hpp"
#include "config/sim_config.hpp"
#include "ecs/world_factory.hpp"

namespace simnet::client {
    // Singleton flag set by signal handler
    std::atomic<bool> ClientWorld::s_running{true};

    // Internal struct for snapshot application (in reconciliation later)
    namespace detail {
        struct PendingSnapshot {
            net::ReplicationSnapshot data;
            bool new_data = true;
        };
    }


    ClientWorld::ClientWorld(const SimConfig &cfg)
    {
        // --- Signal handling ---
        std::signal(SIGINT, [](int) { s_running = false; });
        std::signal(SIGTERM, [](int) { s_running = false; });

        // // --- ECS world setup ---
        // world_.set<SimConfig>(cfg);
        //
        // ecs::set_world_thread_count(world_);
        // ecs::register_clients_components(world_);
        //
        // register_snapshot_system();
        //
        // // --- Create rendering system ---
        // renderer_ = std::make_unique<RenderSystem>(1920, 1080, "SimNetLab_Client", world_);
        //
        // // --- Network client (temp) ---
        // network_ = std::make_unique<NetworkClient>();
        // network_->start_connect()
    }

    ClientWorld::~ClientWorld()
    {
    }

    void ClientWorld::Run()
    {
    }

    void ClientWorld::register_snapshot_system()
    {
    }

}
