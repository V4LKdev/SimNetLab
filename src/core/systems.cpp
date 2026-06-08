#include "systems.hpp"

#include <cmath>

#include "components.hpp"
#include "config.hpp"
#include "boid_steering.hpp"

namespace simnet::ecs {

    void init_systems(flecs::world& world)
     {
         register_scratchpad_builder(world);
         register_boid_steering_system(world);
         register_boid_apply_velocity(world);
         register_boid_integrate_position(world);
     }

    void register_scratchpad_builder(const flecs::world &world)
    {
        world.system<const Position3D, const Velocity3D, Boid>("BuildScratchpad")
            .kind(flecs::PreUpdate)
            .run([](flecs::iter& it)
        {
            BoidScratchpadSoA& scratch = it.world().get_mut<BoidScratchpadSoA>();

            uint32_t count = 0;

            const uint32_t estimated =
                static_cast<uint32_t>(it.world().count<Boid>());

            scratch.posX.resize(estimated);
            scratch.posY.resize(estimated);
            scratch.posZ.resize(estimated);

            scratch.velX.resize(estimated);
            scratch.velY.resize(estimated);
            scratch.velZ.resize(estimated);

            while (it.next())
            {
                auto pos_field  = it.field<const Position3D>(0);
                auto vel_field  = it.field<const Velocity3D>(1);
                auto boid_field = it.field<Boid>(2);

                for (const uint64_t i : it)
                {
                    const auto& p = pos_field[i].pos;
                    const auto& v = vel_field[i].vel;

                    scratch.posX[count] = p.x;
                    scratch.posY[count] = p.y;
                    scratch.posZ[count] = p.z;

                    scratch.velX[count] = v.x;
                    scratch.velY[count] = v.y;
                    scratch.velZ[count] = v.z;

                    boid_field[i].scratch_index = count;

                    ++count;
                }
            }

            scratch.count = count;
        });
    }

    void register_boid_steering_system(const flecs::world &world)
    {
        world.system<
            const Position3D,
            const Velocity3D,
            const Boid,
            DesiredVelocity3D>
        ("BoidSteering")
        .kind(flecs::OnUpdate)
        .multi_threaded()
        .run([](flecs::iter& it) {

            const BoidConfig*      cfg = it.world().try_get<BoidConfig>();
            const BoidPerception*  per = it.world().try_get<BoidPerception>();
            const BoidFeatures*    f   = it.world().try_get<BoidFeatures>();
            const BoidScratchpadSoA*  sp  = it.world().try_get<BoidScratchpadSoA>();
            if (!cfg || !per || !f || !sp) return;

            while (it.next()) {
                auto pos_field = it.field<const Position3D>(0);
                auto vel_field = it.field<const Velocity3D>(1);
                auto out_field = it.field<DesiredVelocity3D>(3);

                for (const uint64_t i : it) {
                    // Wrap input
                    boid::SteeringInput in{
                        pos_field[i].pos,
                        vel_field[i].vel,
                        *cfg,
                        *per,
                        *f,
                        *sp,
                        it.field<const Boid>(2)[i].scratch_index
                    };

                    auto forces = boid::compute_steering_forces(in, boid::EvalMode::SIMD);

                    Vec3 total_steer = boid::combine_steering(
                        { forces.separation, forces.alignment, forces.cohesion },
                        cfg->max_force
                    );

                    out_field[i].desired = boid::desired_velocity(vel_field[i].vel, total_steer);
                }
            }
        });
    }


    void register_boid_apply_velocity(const flecs::world &world)
    {
        world.system<Velocity3D, const DesiredVelocity3D>("ApplyVelocity")
            .kind(flecs::PostUpdate)
            .multi_threaded()
            .run([](flecs::iter& it) {

                const BoidConfig* cfg = it.world().try_get<BoidConfig>();
                if (!cfg) return;

                while (it.next()) {
                    auto vel_out      = it.field<Velocity3D>(0);
                    auto desired_in   = it.field<const DesiredVelocity3D>(1);

                    for (const uint64_t i : it) {
                        Vec3 desired = desired_in[i].desired;
                        const float len2 = desired.length_sq();

                        if (len2 > cfg->max_speed * cfg->max_speed) {
                            // Clamp to max_speed
                            desired = desired.normalized() * cfg->max_speed;
                        } else if (len2 < 1e-10f) {
                            continue;
                        }

                        vel_out[i].vel = desired;
                    }
                }
            });
    }

    void register_boid_integrate_position(const flecs::world &world)
    {
        world.system<Position3D, const Velocity3D>("IntegratePosition")
            .kind(flecs::PostUpdate)
            .multi_threaded()
            .run([](flecs::iter& it) {

                constexpr float dt   = static_cast<float>(config::SIM_DT.count()) / 1'000'000'000.0f;

                while (it.next()) {
                    auto pos_out = it.field<Position3D>(0);
                    auto vel_in  = it.field<const Velocity3D>(1);

                    for (const uint64_t i : it) {
                        pos_out[i].pos += vel_in[i].vel * dt;
                        Vec3::wrap_position(pos_out[i].pos, config::WORLD_HALF);
                    }
                }
            });
    }

}