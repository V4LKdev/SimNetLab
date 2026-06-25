#include "game/shared/telemetry_systems.hpp"

#include "components.hpp"
#include "game/shared/systems/system_functions.hpp"

namespace simnet::game::shared {
    void register_telemetry_systems(flecs::world &world)
    {
        world.system<const Position, const Velocity, const Heading, const SteeringAccumulate, const BoidIdx>(
                    "FlockStatistics")
                .with<Boid>().in()
                .kind(flecs::PostFrame)
                .multi_threaded(false)
                .rate(60)
                .run(flock_statistics_system);
    }
}
