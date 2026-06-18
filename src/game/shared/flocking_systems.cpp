#include "../../core/ecs/flocking_systems.hpp"

#include "components.hpp"
#include "systems/system_functions.hpp"

namespace simnet::ecs {
    void register_flocking_systems(flecs::world &world, flecs::entity sim_prepare, flecs::entity sim_compute,
                                   flecs::entity sim_apply)
    {
        // --- Prep Phase ---
        world.system<SteeringAccumulate>("ClearAccumulate")
                .with<Boid>()
                .kind(sim_prepare)
                .multi_threaded(true)
                .run(clear_accumulator_system);

        world.system<const Velocity, Heading>("AlignHeading")
                .with<Boid>()
                .kind(sim_prepare)
                .multi_threaded(true)
                .run(align_heading_system);

        world.system<const Position, const Heading>("BuildNeighborCache")
                .with<Boid>()
                .kind(sim_prepare)
                .multi_threaded(false) // Writes to singleton
                .run(build_neighbor_cache_system);

        // --- Compute Phase ---
        world.system<const BoidIdx, SteeringAccumulate>("Alignment")
                .with<Boid>()
                .kind(sim_compute)
                .multi_threaded(true)
                .run(alignment_system);

        world.system<const BoidIdx, SteeringAccumulate>("Cohesion")
                .with<Boid>()
                .kind(sim_compute)
                .multi_threaded(true)
                .run(cohesion_system);

        world.system<const BoidIdx, SteeringAccumulate>("Separation")
                .with<Boid>()
                .kind(sim_compute)
                .multi_threaded(true)
                .run(separation_system);

        // --- Apply Phase ---
        auto apply_steering = world.system<const SteeringAccumulate, Velocity>("ApplySteering")
                .with<Boid>()
                .kind(sim_apply)
                .multi_threaded(true)
                .run(apply_steering_system);

        auto integ_position = world.system<const Velocity, Position>("IntegratePosition")
                .with<Boid>()
                .kind(sim_apply)
                .multi_threaded(true)
                .run(integrate_position_system);

        integ_position.depends_on(apply_steering);
    }
}
