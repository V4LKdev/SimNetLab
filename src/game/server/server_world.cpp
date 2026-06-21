#include "server_world.hpp"

#include <utility>
#include <thread>

#include "game/shared/game_shared.hpp"
#include "../shared/systems/system_functions.hpp"

namespace simnet::game::server {
    ServerWorld::ServerWorld(config::SimConfig config, net::NetManager *net)
        : net_(net),
          config_(std::move(config))
    {
        // 1. Set Threads
        configure_threads();

        // 2. Register all resources
        register_components();
        register_server_systems();

        auto e = world_.entity();

        e.set<shared::Position>({math::Vec3::zero()});
        e.set<shared::Velocity>({math::Vec3::zero()});
        e.set<shared::Heading>({math::Vec3::forward()});
        e.set<shared::Hue>({1});
        e.set<shared::SteeringAccumulate>({math::Vec3::zero()});
        e.set<shared::BoidIdx>({});
        e.set<shared::NetworkId>({1});
        e.add<shared::Boid>();
    }

    ServerWorld::~ServerWorld()
    {
        world_.quit();
    }

    void ServerWorld::run_tick(float dt)
    {
        world_.progress(dt);
    }

    void ServerWorld::register_components()
    {
        using namespace shared;

        world_.component<Position>();
        world_.component<Heading>();
        world_.component<Velocity>();
        world_.component<SteeringAccumulate>();
        world_.component<NetworkId>();
        world_.component<Hue>();
        world_.component<BoidIdx>();
        // Tags
        world_.component<Boid>();
        // Singletons
        world_.component<NeighborCache>().add(flecs::Singleton);
        world_.component<config::SimConfig>().add(flecs::Singleton);
        world_.set<config::SimConfig>(config_);

        TELEM_LOG_DEBUG("ServerWorld: Components registered");
    }

    void ServerWorld::register_server_systems()
    {
        using namespace shared;

        const auto prepare = world_.entity("SimPrepare").add(flecs::Phase);
        const auto compute = world_.entity("SimCompute").add(flecs::Phase).depends_on(prepare);
        const auto apply = world_.entity("SimApply").add(flecs::Phase).depends_on(compute);
        const auto simpost = world_.entity("SimPost").add(flecs::Phase).depends_on(apply);
        auto netsend = world_.entity("NetSend").add(flecs::Phase).depends_on(simpost);

        // --- Init ---


        // --- Simulation ---
        register_flocking_systems(world_, prepare, compute, apply);

#ifdef TELEMETRY_ENABLED
        register_telemetry_systems(world_, simpost);
#endif

        // --- Netsend ---

        TELEM_LOG_DEBUG("ServerWorld: Systems registered");
    }

    void ServerWorld::configure_threads()
    {
        if (!config_.multithreaded_ecs) {
            world_.set_threads(0);
            TELEM_LOG_DEBUG("ServerWorld: Threads configured: {}", 0);
            return;
        }

        if (config_.ecs_thread_count > 0) {
            world_.set_threads(config_.ecs_thread_count);
            TELEM_LOG_DEBUG("ServerWorld: Threads configured: {}", config_.ecs_thread_count);
            return;
        }

        unsigned int num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) {
            num_threads = 4;
            TELEM_LOG_WARN("Could not determine hardware concurrency; defaulting to 4 threads");
        }

        // Leave one core free for better game‑loop latency
        if (num_threads > 4) {
            num_threads -= 1;
        }

        world_.set_threads(static_cast<int32_t>(num_threads));
        TELEM_LOG_DEBUG("ServerWorld: Threads configured: {}", num_threads);
    }
}
