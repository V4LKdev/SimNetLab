#include "server_world.hpp"

#include "core.hpp"

#include "components.hpp"
#include "game/shared/flocking_systems.hpp"
#include "game/shared/telemetry_systems.hpp"
#include "game/server/spawn_system.hpp"

#include "telemetry.hpp"

namespace simnet::game::server {
    using namespace simnet::core;
    using namespace simnet::game::shared;

    // ────────────────────────────────────────────────────────────────
    // PIMPL Definition
    // ────────────────────────────────────────────────────────────────
    struct ServerWorld::Impl {
        net::NetManager net_manager;
        flecs::world ecs;
        utils::TimeKeeper timer; // fixed‑step accumulator
        config::SimConfig sim_config; // cached for convenience
        net::NetConfig net_config;
        bool initialized = false;

        Impl() : ecs()
        {
        }
    };

    // ────────────────────────────────────────────────────────────────
    // Construction / Destruction
    // ────────────────────────────────────────────────────────────────
    ServerWorld::ServerWorld()
        : impl_(std::make_unique<Impl>())
    {
    }

    ServerWorld::~ServerWorld()
    {
        shutdown();
    }

    // ────────────────────────────────────────────────────────────────
    // Initialization
    // ────────────────────────────────────────────────────────────────
    bool ServerWorld::initialize(const net::NetConfig &net_cfg,
                                 const config::SimConfig &sim_cfg)
    {
        impl_->net_config = net_cfg;
        impl_->sim_config = sim_cfg;

        // 1. Start the network layer
        if (!impl_->net_manager.initialize(net::NetRole::server, net_cfg)) {
            TELEM_LOG_ERROR("Failed to initialize network");
            return false;
        }

        // 2. Set up the ECS world
        init_ecs(sim_cfg);

        impl_->timer.reset(sim_cfg.dt); // dt is a std::chrono::duration?
        impl_->initialized = true;
        TELEM_LOG_INFO("ServerWorld initialized");
        return true;
    }

    void ServerWorld::init_ecs(const config::SimConfig &cfg)
    {
        auto &world = impl_->ecs;

        // Register components and singletons (from components.hpp)
        register_server_components(world);

        // Set singletons
        world.set<config::SimConfig>(cfg);

        // ── Create simulation phases ──
        auto prepare = world.entity("SimPrepare").add(flecs::Phase);
        auto compute = world.entity("SimCompute").add(flecs::Phase).depends_on(prepare);
        auto apply = world.entity("SimApply").add(flecs::Phase).depends_on(compute);
        auto simpost = world.entity("SimPost").add(flecs::Phase).depends_on(apply);
        auto netsend = world.entity("NetSend").add(flecs::Phase).depends_on(simpost);

        // ── Spawn initial entities (once, on start) ──
        world.system("SpawnBoids")
                .kind(flecs::OnStart)
                .multi_threaded(false)
                .run(spawn_boids_system);

        // ── Shared flocking systems ──
        register_flocking_systems(world, prepare, compute, apply);

        // ── Optional telemetry ──
#ifdef TELEMETRY_ENABLED
        register_telemetry_systems(world, simpost);
#endif

        // ── Snapshot system – captures NetManager via lambda ──
        // The lambda is called each tick during phase "NetSend".
        world.system<const Position, const Heading, const Hue, const NetworkId>("SendSnapshot")
                .with<Boid>()
                .kind(netsend)
                .multi_threaded(false)
                .run([this](flecs::iter &it) {
                    net::ReplicationSnapshot snapshot;
                    snapshot.tick = ++this->impl_->tick_counter; // or use a time source
                    snapshot.sequence = ++this->impl_->seq_counter;

                    while (it.next()) {
                        auto pos = it.field<const Position>(0);
                        auto hdg = it.field<const Heading>(1);
                        auto hue = it.field<const Hue>(2);
                        auto nid = it.field<const NetworkId>(3);

                        for (int i = 0; i < it.count(); ++i) {
                            net::ReplicatedEntity re;
                            re.network_id = nid[i].value;
                            re.position = pos[i].value;
                            re.heading = hdg[i].value;
                            re.hue = hue[i].value;
                            snapshot.entities.push_back(re);
                        }
                    }

                    this->impl_->net_manager.broadcast_snapshot(snapshot);

                    TELEM_LOG_TRACE("Snapshot sent: tick {}, entities {}",
                                    snapshot.tick, snapshot.entities.size());
                });
    }

    // ────────────────────────────────────────────────────────────────
    // Update
    // ────────────────────────────────────────────────────────────────
    void ServerWorld::update(utils::TimePoint now)
    {
        if (!impl_->initialized) return;

        // 1. Service network (process events, timeouts, handshakes)
        impl_->net_manager.update(now);

        // 2. Feed elapsed time to the timer, step the world for each full dt
        impl_->timer.advance(now);
        while (impl_->timer.pop_step()) {
            impl_->ecs.progress(impl_->sim_config.dt_seconds()); // dt as float seconds
        }
    }

    // ────────────────────────────────────────────────────────────────
    // Shutdown
    // ────────────────────────────────────────────────────────────────
    void ServerWorld::shutdown()
    {
        if (!impl_->initialized) return;

        impl_->net_manager.shutdown();
        impl_->initialized = false;
        TELEM_LOG_INFO("ServerWorld shut down");
    }

    const flecs::world &ServerWorld::world() const noexcept
    {
        return impl_->ecs;
    }
}
