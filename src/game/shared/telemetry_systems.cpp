#include "../../core/ecs/telemetry_systems.hpp"

#include "components.hpp"
#include "systems/system_functions.hpp"

namespace simnet::ecs {
    void register_telemetry_systems(flecs::world &world, flecs::entity post_sim)
    {
        world.system<const Position, const Velocity, const Heading, const SteeringAccumulate, const BoidIdx>(
                    "FlockStatistics")
                .with<Boid>()
                .kind(post_sim)
                .multi_threaded(false)
                .rate(60)
                .run(flock_statistics_system);
    }
}
