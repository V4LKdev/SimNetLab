#include "server_ecs.hpp"

#include "../../game/shared/components.hpp"
#include "../../game/shared/flocking_systems.hpp"
#include "ecs/telemetry_systems.hpp"
#include "systems/system_functions.hpp"

namespace simnet::server::ecs {
    void init_server_ecs(flecs::world &world)
    {
        // 1. Create simulation phases
        const auto prepare = world.entity("SimPrepare").add(flecs::Phase);
        const auto compute = world.entity("SimCompute").add(flecs::Phase).depends_on(prepare);
        const auto apply = world.entity("SimApply").add(flecs::Phase).depends_on(compute);
        const auto simpost = world.entity("SimPost").add(flecs::Phase).depends_on(apply);
        auto netsend = world.entity("NetSend").add(flecs::Phase).depends_on(simpost);

        // 2. Spawn all initial boids before the first tick
        world.system("SpawnBoids")
                .kind(flecs::OnStart)
                .multi_threaded(false) // Cannot multithread entity creation
                .run(spawn_boids_system);

        // 3. Register all shared flocking systems
        simnet::ecs::register_flocking_systems(world, prepare, compute, apply);

#ifdef TELEMETRY_ENABLED
        simnet::ecs::register_telemetry_systems(world, simpost);
#endif

        // 4. Snapshot system
        world.system<const simnet::ecs::Position, const simnet::ecs::Heading, const simnet::ecs::Hue, const
                    simnet::ecs::NetworkId>("SendSnapshot")
                .with<simnet::ecs::Boid>()
                .kind(netsend)
                .multi_threaded(false)
                .run(send_snapshot_system);
    }
}
