#include "server_world.hpp"

#include <utility>
#include <thread>

#include "app_context.hpp"
#include "components.hpp"
#include "game/shared/game_shared.hpp"
#include "../shared/systems/system_functions.hpp"
#include "core/net/pipeline/serializer/full_snapshot_serializer.hpp"
#include "systems/system_functions.hpp"

namespace simnet::game::server {
    ServerWorld::ServerWorld(config::SimConfig config, net::NetManager *net)
        : net_(net),
          config_(std::move(config))
    {
        // 1. Set Threads and context
        configure_threads();

        ctx_ = std::make_unique<AppContext>();
        ctx_->net = net_;
        world_.set_ctx(ctx_.get());

        // 2. Register all resources
        register_components();

        // 3. Build cached queries
        ctx_->snapshot_query = world_.query_builder<
                    const shared::Position,
                    const shared::Heading,
                    const shared::Hue,
                    const shared::NetworkId>()
                .with<shared::Boid>().in()
                .cached()
                .build();

        ctx_->boid_destroy_query = world_.query_builder<const shared::NetworkId>()
                .cached()
                .build();

        // 4. Build the global network pipeline
        build_network_pipeline();

        // 5. Register all systems
        register_server_systems();
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
        world_.set<NeighborCache>({});
        world_.component<config::SimConfig>().add(flecs::Singleton);
        world_.set<config::SimConfig>(config_);
        world_.component<SimTick>().add(flecs::Singleton);
        world_.set<SimTick>({0});
        world_.component<SnapshotSequence>().add(flecs::Singleton);
        world_.set<SnapshotSequence>({0});

        world_.component<CurrentSnapshot>().add(flecs::Singleton);
        world_.set<CurrentSnapshot>({});

        world_.component<NetPipelineChain>().add(flecs::Singleton);

        TELEM_LOG_DEBUG("ServerWorld: Components registered");
    }

    void ServerWorld::build_network_pipeline()
    {
        using namespace net::internal;

        PipelineChain chain;

        // Set serializer
        chain.set_serializer(std::make_unique<FullSnapshotSerializer>(config_));

        // add filters and encoders

        NetPipelineChain component{std::move(chain)};
        world_.set<NetPipelineChain>(std::move(component));

        TELEM_LOG_DEBUG("ServerWorld: Network pipeline built");
    }

    void ServerWorld::register_server_systems()
    {
        using namespace shared;

        world_.system("IncrementSimTick")
                .kind(flecs::PreFrame)
                .multi_threaded(false)
                .each([](SimTick &tick) {
                    tick.value++;
                });

        world_.system("BoidPopulationManager")
                .kind(flecs::OnLoad)
                .multi_threaded(false) // structural changes
                .run(boid_population_manager_system);

        // --- Simulation ---
        register_flocking_systems(world_);

        // --- Netsend ---
        world_.system("NetPrepareSnapshot")
                .kind(flecs::OnStore)
                .multi_threaded(false) // writes to singleton
                .run(net_prepare_snapshot_system);

        world_.system("NetComputePeerVisibility")
                .kind(flecs::OnStore)
                .multi_threaded(false) // writes to PeerState via NetManager
                .run(net_compute_peer_visibility_system);

        world_.system("NetPipeline")
                .kind(flecs::OnStore)
                .multi_threaded(false) // will manually dispatch peers if parallelised
                .run(net_pipeline_system);

#ifdef TELEMETRY_ENABLED
        register_telemetry_systems(world_);
#endif

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
