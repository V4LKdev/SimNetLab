#include "../src/core/ecs/systems.hpp"

#include <cmath>

#include "../src/core/ecs/components.hpp"
#include "telemetry.hpp"
#include "../src/core/config.hpp"
#include "../src/core/boid/boid_steering.hpp"


namespace {
    using namespace simnet::ecs;

    void register_boid_neighbor_query(const flecs::world &world)
    {
    }

    void register_boid_rule_alignment(const flecs::world &world)
    {
    }

    void register_boid_rule_cohesion(const flecs::world &world)
    {
    }

    void register_boid_rule_separation(const flecs::world &world)
    {
    }

    void register_boid_accumulate_steering(const flecs::world &world)
    {
    }


    void register_boid_apply_steering(const flecs::world &world)
    {
    }

    void register_boid_integrate_position(const flecs::world &world)
    {
    }

    void register_boid_clear_accumulators(const flecs::world &world)
    {
    }
}


namespace simnet::ecs {
    void init_systems(flecs::world &world)
    {
        // OnUpdate
        register_boid_neighbor_query(world);

        register_boid_rule_alignment(world);
        register_boid_rule_cohesion(world);
        register_boid_rule_separation(world);

        register_boid_accumulate_steering(world);

        // PostUpdate
        register_boid_apply_steering(world);
        register_boid_integrate_position(world);
        register_boid_clear_accumulators(world);
    }

    // ------------------------------------------------------------------
    //  Steering system (OnUpdate)
    // ------------------------------------------------------------------
    void register_boid_steering_system(const flecs::world &world)
    {
        world.system<const Position, const Velocity, SteeringAccumulate>("BoidSteering")
                .with<Boid>()
                .kind(flecs::OnUpdate)
                .multi_threaded(true)
                .term_at(0).in()
                .term_at(1).in()
                .term_at(2).out()
                .run([](flecs::iter &it) {
                    TELEM_TRACY_ZONE_C("BoidSteeringSystem", TELEM_COLOR_BOID);

                    // Access singletons
                    flecs::world w = it.world();
                    const BoidConfig *cfg = w.try_get<BoidConfig>();
                    const BoidPerception *per = w.try_get<BoidPerception>();

                    if (cfg == nullptr || per == nullptr) return;

                    const boid::NeighborQuery query{boid::query_bruteforce};

                    // Iterate over ECS tables
                    while (it.next()) {
                        TELEM_TRACY_ZONE("SteeringChunk");

                        const uint64_t count = it.count();
                        if (count == 0)
                            continue;

                        // Direct access to contiguous component arrays for this chunk
                        const Position *pos = &it.field<const Position>(0)[0];
                        const Velocity *vel = &it.field<const Velocity>(1)[0];
                        SteeringAccumulate *out = &it.field<SteeringAccumulate>(2)[0];


                        boid::compute_boid_steering(
                            pos,
                            vel,
                            out,
                            count,
                            *cfg,
                            *per,
                            query
                        );

                        TELEM_TRACY_PLOT("NumBoids", static_cast<int64_t>(count));
                    }
                });
    }

    // ------------------------------------------------------------------
    //  Apply velocity (PostUpdate)
    // ------------------------------------------------------------------
    void register_boid_apply_velocity(const flecs::world &world)
    {
        world.system<Velocity, const SteeringAccumulate>("ApplyVelocity")
                .kind(flecs::PostUpdate)
                .multi_threaded()
                .run([](flecs::iter &it) {
                    TELEM_TRACY_ZONE("ApplyVelocity");

                    const BoidConfig *cfg = it.world().try_get<BoidConfig>();
                    if (!cfg) return;

                    while (it.next()) {
                        auto vel_out = it.field<Velocity>(0);
                        auto desired_in = it.field<const SteeringAccumulate>(1);

                        for (const uint64_t i: it) {
                            Vec3 desired = desired_in[i].value;
                            const float len2 = desired.length_sq();

                            if (len2 > cfg->max_speed * cfg->max_speed) {
                                desired = desired.normalized() * cfg->max_speed;
                            } else if (len2 < 1e-10f) {
                                continue;
                            }

                            vel_out[i].value = desired;
                        }
                    }
                });
    }

    // ------------------------------------------------------------------
    //  Integrate position (PostUpdate)
    // ------------------------------------------------------------------
    void register_boid_integrate_position(const flecs::world &world)
    {
        world.system<Position, const Velocity>("IntegratePosition")
                .kind(flecs::PostUpdate)
                .multi_threaded()
                .run([](flecs::iter &it) {
                    TELEM_TRACY_ZONE("IntegratePosition");


                    constexpr float dt = static_cast<float>(config::SIM_DT.count()) / 1'000'000'000.0f;

                    while (it.next()) {
                        auto pos_out = it.field<Position>(0);
                        auto vel_in = it.field<const Velocity>(1);

                        for (const uint64_t i: it) {
                            pos_out[i].value += vel_in[i].value * dt;
                            Vec3::wrap_position(pos_out[i].value, config::WORLD_HALF);
                        }
                    }
                });
    }
} // namespace simnet::ecs
