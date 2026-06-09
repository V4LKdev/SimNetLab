#include "systems.hpp"

#include <cmath>

#include "components.hpp"
#include "telemetry.hpp"
#include "../config.hpp"
#include "../boid/boid_steering.hpp"

namespace simnet::ecs {
    void init_systems(flecs::world &world)
    {
        register_boid_steering_system(world);
        register_boid_apply_velocity(world);
        register_boid_integrate_position(world);
    }

    // ------------------------------------------------------------------
    //  Steering system (OnUpdate)
    // ------------------------------------------------------------------
    void register_boid_steering_system(const flecs::world &world)
    {
        world.system<const Position, const Velocity, DesiredVelocity>("BoidSteering")
                .with<Boid>()
                .kind(flecs::OnUpdate)
                .multi_threaded(false)
                .term_at(0).in()
                .term_at(1).in()
                .term_at(2).out()
                .run([](flecs::iter &it) {
                    TELEM_TRACY_ZONE("BoidSteeringSystem");

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
                        DesiredVelocity *out = &it.field<DesiredVelocity>(2)[0];

                        TELEM_SCOPED_TIMER("SIM_BoidSteering");
                        boid::compute_boid_steering(
                            pos,
                            vel,
                            out,
                            count,
                            *cfg,
                            *per,
                            query
                        );
                    }
                });
    }

    // ------------------------------------------------------------------
    //  Apply velocity (PostUpdate)
    // ------------------------------------------------------------------
    void register_boid_apply_velocity(const flecs::world &world)
    {
        world.system<Velocity, const DesiredVelocity>("ApplyVelocity")
                .kind(flecs::PostUpdate)
                .multi_threaded()
                .run([](flecs::iter &it) {
                    const BoidConfig *cfg = it.world().try_get<BoidConfig>();
                    if (!cfg) return;

                    while (it.next()) {
                        auto vel_out = it.field<Velocity>(0);
                        auto desired_in = it.field<const DesiredVelocity>(1);

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
