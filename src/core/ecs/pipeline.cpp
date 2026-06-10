#include "pipeline.hpp"

#include "components.hpp"
#include "systems/system_functions.hpp"

namespace simnet::ecs {
    static flecs::entity s_sim_prepare;
    static flecs::entity s_sim_compute;
    static flecs::entity s_sim_apply;

    void init_simulation(flecs::world &world)
    {
        register_components(world);

        s_sim_prepare = world.entity("SimPrepare")
                .add(flecs::Phase);
        s_sim_compute = world.entity("SimCompute")
                .add(flecs::Phase)
                .depends_on(s_sim_prepare);
        s_sim_apply = world.entity("SimApply")
                .add(flecs::Phase)
                .depends_on(s_sim_compute);

        // --- Prep Phase ---
        auto neighbor_cache = world.system<const Position, NeighborList>("NeighborCache")
                .with<Boid>()
                .kind(s_sim_prepare)
                .multi_threaded(true)
                .run(neighbor_cache_system);

        // --- Compute Phase ---
        auto alignment = world.system<const Position, const Velocity, const NeighborList, SteeringAccumulate>(
                    "Alignment")
                .with<Boid>()
                .kind(s_sim_compute)
                .run(alignment_system);

        auto cohesion = world.system<const Position, const Velocity, const NeighborList, SteeringAccumulate>(
                    "Cohesion")
                .with<Boid>()
                .kind(s_sim_compute)
                .run(cohesion_system);

        auto separation = world.system<const Position, const Velocity, const NeighborList, SteeringAccumulate>(
                    "Separation")
                .with<Boid>()
                .kind(s_sim_compute)
                .run(separation_system);

        // --- Apply Phase ---
        auto apply_steering = world.system<const SteeringAccumulate, Velocity>("ApplySteering")
                .with<Boid>()
                .kind(s_sim_apply)
                .run(apply_steering_system);

        auto integrate_position = world.system<const Velocity, Position>("IntegratePosition")
                .with<Boid>()
                .kind(s_sim_apply)
                .run(integrate_position_system);

        auto clear_accumulator = world.system<SteeringAccumulate>("ClearAccumulate")
                .with<Boid>()
                .kind(s_sim_apply)
                .run(clear_accumulator_system);
    }

    void run_tick(flecs::world &world, float dt)
    {
        if (world.should_quit()) { return; }
        world.progress(dt);
    }
}
