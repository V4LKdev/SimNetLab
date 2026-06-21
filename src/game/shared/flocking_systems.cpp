#include "game/shared/flocking_systems.hpp"

#include "game/shared/components.hpp"
#include "game/shared/systems/system_functions.hpp"


namespace simnet::game::shared {
    void register_flocking_systems(flecs::world &world, flecs::entity prepare, flecs::entity compute,
                                   flecs::entity apply)
    {
        // --- Prep ---
        world.system<SteeringAccumulate>("ClearAccumulate")
                .with<Boid>().in()
                .kind(prepare)
                .multi_threaded(true)
                .each([](SteeringAccumulate &acc) {
                    acc.value = math::Vec3::zero();
                });

        world.system<const Velocity, Heading>("AlignHeading")
                .with<Boid>().in()
                .kind(prepare)
                .multi_threaded(true)
                .run(align_heading_system);

        world.system<const Position, const Heading>("BuildNeighborCache")
                .with<Boid>().in()
                .write<NeighborCache>()
                .kind(prepare)
                .multi_threaded(false) // Writes to singleton
                .run(build_neighbor_cache_system);

        // --- Compute ---
        world.system<const BoidIdx, SteeringAccumulate>("Alignment")
                .with<Boid>().in()
                .read<NeighborCache>()
                .kind(compute)
                .multi_threaded(true)
                .run(alignment_system);

        world.system<const BoidIdx, SteeringAccumulate>("Cohesion")
                .with<Boid>().in()
                .read<NeighborCache>()
                .kind(compute)
                .multi_threaded(true)
                .run(cohesion_system);

        world.system<const BoidIdx, SteeringAccumulate>("Separation")
                .with<Boid>().in()
                .read<NeighborCache>()
                .kind(compute)
                .multi_threaded(true)
                .run(separation_system);

        // --- Apply Phase ---
        world.system<const SteeringAccumulate, Velocity>("ApplySteering")
                .with<Boid>().in()
                .kind(apply)
                .multi_threaded(true)
                .run(apply_steering_system);

        world.system<const Velocity, Position>("IntegratePosition")
                .with<Boid>().in()
                .kind(apply)
                .multi_threaded(true)
                .run(integrate_position_system);
    }
}
