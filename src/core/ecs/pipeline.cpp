#include "pipeline.hpp"

#include <memory>
#include "components.hpp"
#include "systems/system_functions.hpp"

namespace simnet::ecs {
    static flecs::entity s_sim_prepare;
    static flecs::entity s_sim_compute;
    static flecs::entity s_sim_apply;
    static flecs::entity s_net_send;

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
        s_net_send = world.entity("NetSend")
                .add(flecs::Phase)
                .depends_on(s_sim_apply);

        // --- Prep Phase ---
        auto clear_accumulator = world.system<SteeringAccumulate>("ClearAccumulate")
                .with<Boid>()
                .kind(s_sim_prepare)
                .multi_threaded(true)
                .run(clear_accumulator_system);

        auto align_heading = world.system<const Velocity, Heading>("AlignHeading")
                .with<Boid>()
                .kind(s_sim_prepare)
                .multi_threaded(true)
                .run(align_heading_system);

        auto build_snapshot = world.system<const Position, const Heading>("BuildNeighborSnapshot")
                .with<Boid>()
                .kind(s_sim_prepare)
                .multi_threaded(false)
                .run(build_neighbor_snapshot_system);

        // --- Compute Phase ---
        auto alignment = world.system<const BoidIdx, SteeringAccumulate>(
                    "Alignment")
                .with<Boid>()
                .kind(s_sim_compute)
                .multi_threaded(true)
                .run(alignment_system);

        auto cohesion = world.system<const BoidIdx, SteeringAccumulate>(
                    "Cohesion")
                .with<Boid>()
                .kind(s_sim_compute)
                .multi_threaded(true)
                .run(cohesion_system);

        auto separation = world.system<const BoidIdx, SteeringAccumulate>(
                    "Separation")
                .with<Boid>()
                .kind(s_sim_compute)
                .multi_threaded(true)
                .run(separation_system);

        // --- Apply Phase ---
        // Ensure they always run in sequence
        auto apply_steering = world.system<const SteeringAccumulate, Velocity>("ApplySteering")
                .with<Boid>()
                .kind(s_sim_apply)
                .multi_threaded(true)
                .run(apply_steering_system);

        auto integrate_position = world.system<const Velocity, Position>("IntegratePosition")
                .with<Boid>()
                .kind(s_sim_apply)
                .multi_threaded(true)
                .run(integrate_position_system);

        integrate_position.depends_on(apply_steering);


#ifdef TELEMETRY_ENABLED
        auto flock_statistics = world.system<const Position, const Velocity, const Heading, const
                    SteeringAccumulate,
                    const BoidIdx>("FlockStatistics")
                .with<Boid>()
                .kind(s_sim_apply)
                .multi_threaded(false) // Writes to singleton
                .rate(60) // run every 60th tic
                .run(flock_statistics_system);

        flock_statistics.depends_on(integrate_position);

#endif

            // --- Net Phase ---
    }

    void run_tick(flecs::world &world, float dt)
    {
        if (world.should_quit()) { return; }
        world.progress(dt);
    }
}
