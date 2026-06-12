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
                auto align_heading = world.system<const Velocity, Heading>("AlignHeading")
                                .with<Boid>()
                                .kind(s_sim_prepare)
                                .multi_threaded(true)
                                .run(align_heading_system);

                auto build_caches = world.system("BuildCaches")
                                .kind(s_sim_prepare)
                                .multi_threaded(false) // Writes to singletons
                                .run(build_caches_system);

                auto neighbor_cache = world.system<const Position, NeighborList>("NeighborCache")
                                .with<Boid>()
                                .kind(s_sim_prepare)
                                .multi_threaded(true)
                                .run(neighbor_cache_system);

                neighbor_cache.depends_on(build_caches);

                // --- Compute Phase ---
                auto alignment = world.system<const Heading, const NeighborList, SteeringAccumulate>("Alignment")
                                .with<Boid>()
                                .kind(s_sim_compute)
                                .multi_threaded(true)
                                .run(alignment_system);

                auto cohesion = world.system<const Heading, const NeighborList, SteeringAccumulate>(
                                        "Cohesion")
                                .with<Boid>()
                                .kind(s_sim_compute)
                                .multi_threaded(true)
                                .run(cohesion_system);

                auto separation = world.system<const Heading, const NeighborList, SteeringAccumulate>(
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

                auto clear_accumulator = world.system<SteeringAccumulate>("ClearAccumulate")
                                .with<Boid>()
                                .kind(s_sim_apply)
                                .multi_threaded(true)
                                .run(clear_accumulator_system);

                integrate_position.depends_on(apply_steering);
                clear_accumulator.depends_on(integrate_position);
        }

        void run_tick(flecs::world &world, float dt)
        {
                if (world.should_quit()) { return; }
                world.progress(dt);
        }
}
