#include "systems.hpp"

#include <cmath>

#include "components.hpp"
#include "config.hpp"

namespace simnet::ecs {

    inline void compute_steering(
        uint32_t self_index,
        const BoidScratchpad& sp,
        const BoidFeatures& f,
        const BoidConfig& cfg,
        const BoidPerception& per,
        float px, float py, float pz,
        float vx, float vy, float vz,
        float& out_desired_vx, float& out_desired_vy, float& out_desired_vz)
    {
        float sepX = 0, sepY = 0, sepZ = 0;
        float aliX = 0, aliY = 0, aliZ = 0;
        float cohX = 0, cohY = 0, cohZ = 0;
        int sepCount = 0, aliCount = 0, cohCount = 0;

        for (uint32_t j = 0; j < sp.count; ++j)
        {
            if (j == self_index) continue;

            float dx = sp.positions[j].x - px;
            float dy = sp.positions[j].y - py;
            float dz = sp.positions[j].z - pz;
            float dist2 = dx*dx + dy*dy + dz*dz;

            if (f.separation && dist2 < per.separation_radius * per.separation_radius) {
                sepX -= dx; sepY -= dy; sepZ -= dz;
                sepCount++;
            }
            if (f.alignment && dist2 < per.alignment_radius * per.alignment_radius) {
                aliX += sp.velocities[j].x;
                aliY += sp.velocities[j].y;
                aliZ += sp.velocities[j].z;
                aliCount++;
            }
            if (f.cohesion && dist2 < per.cohesion_radius * per.cohesion_radius) {
                cohX += dx; cohY += dy; cohZ += dz;
                cohCount++;
            }
        }

        float steerX = 0, steerY = 0, steerZ = 0;
        if (sepCount > 0) {
            steerX += (sepX / sepCount) * cfg.separation_weight;
            steerY += (sepY / sepCount) * cfg.separation_weight;
           steerZ += (sepZ / sepCount) * cfg.separation_weight;
        }
        if (aliCount > 0) {
            steerX += (aliX / aliCount - vx) * cfg.alignment_weight;
            steerY += (aliY / aliCount - vy) * cfg.alignment_weight;
            steerZ += (aliZ / aliCount - vz) * cfg.alignment_weight;
        }
        if (cohCount > 0) {
            steerX += (cohX / cohCount) * cfg.cohesion_weight;
            steerY += (cohY / cohCount) * cfg.cohesion_weight;
            steerZ += (cohZ / cohCount) * cfg.cohesion_weight;
        }

        float steerLen = sqrtf(steerX*steerX + steerY*steerY + steerZ*steerZ);
        if (steerLen > cfg.max_force) {
            float scale = cfg.max_force / steerLen;
            steerX *= scale; steerY *= scale; steerZ *= scale;
        }

        out_desired_vx = vx + steerX;
        out_desired_vy = vy + steerY;
        out_desired_vz = vz + steerZ;
    }


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
            BoidScratchpad& scratch =
                it.world().get_mut<BoidScratchpad>();

            uint32_t count = 0;

            while (it.next())
            {
                flecs::field<const Position3D> pos = it.field<const Position3D>(0);
                flecs::field<const Velocity3D> vel = it.field<const Velocity3D>(1);
                flecs::field<Boid> boid = it.field<Boid>(2);

                for (const uint64_t i : it)
                {
                    scratch.positions[count].x = pos[i].x;
                    scratch.positions[count].y = pos[i].y;
                    scratch.positions[count].z = pos[i].z;

                    scratch.velocities[count].x = vel[i].x;
                    scratch.velocities[count].y = vel[i].y;
                    scratch.velocities[count].z = vel[i].z;

                    boid[i].scratch_index = count;

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
        .run([](flecs::iter& it) {

            const BoidScratchpad* sp = it.world().try_get<BoidScratchpad>();
            const BoidFeatures*    f  = it.world().try_get<BoidFeatures>();
            const BoidConfig*      cfg = it.world().try_get<BoidConfig>();
            const BoidPerception*  per = it.world().try_get<BoidPerception>();
            if (!sp || !f || !cfg || !per) return;

            while (it.next()) {
                flecs::field<const Position3D>    pos = it.field<const Position3D>(0);
                flecs::field<const Velocity3D>    vel = it.field<const Velocity3D>(1);
                flecs::field<const Boid>          boid = it.field<const Boid>(2);
                flecs::field<DesiredVelocity3D>   out  = it.field<DesiredVelocity3D>(3);

                for (const uint64_t i : it) {
                    const uint32_t self = boid[i].scratch_index;
                    compute_steering(
                        self, *sp, *f, *cfg, *per,
                        pos[i].x, pos[i].y, pos[i].z,
                        vel[i].x, vel[i].y, vel[i].z,
                        out[i].x, out[i].y, out[i].z);
                }
            }
        });
    }


    void register_boid_apply_velocity(const flecs::world &world)
    {
        world.system<Velocity3D, const DesiredVelocity3D>("ApplyVelocity")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {

            const BoidConfig* cfg = it.world().try_get<BoidConfig>();
            if (!cfg) return;

            while (it.next()) {
                flecs::field<Velocity3D> out = it.field<Velocity3D>(0);
                flecs::field<const DesiredVelocity3D> desired = it.field<const DesiredVelocity3D>(1);

                for (const uint64_t i : it) {
                    float vx = desired[i].x;
                    float vy = desired[i].y;
                    float vz = desired[i].z;

                    float speed2 = vx*vx + vy*vy + vz*vz;

                    if (speed2 > cfg->max_speed * cfg->max_speed) {
                        float inv = cfg->max_speed / sqrtf(speed2);
                        vx *= inv; vy *= inv; vz *= inv;
                    }
                    out[i].x = vx; out[i].y = vy; out[i].z = vz;
                }
            }
        });
    }



    void register_boid_integrate_position(const flecs::world &world)
    {
        world.system<Position3D, const Velocity3D>("IntegratePosition")
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {

            const BoidConfig*   cfg = it.world().try_get<BoidConfig>();
            const BoidFeatures* f   = it.world().try_get<BoidFeatures>();
            if (!cfg || !f) return;

            constexpr float dt = static_cast<float>(config::SIM_DT.count()) / 1'000'000'000.0f;

            while (it.next()) {
                flecs::field<Position3D>        out = it.field<Position3D>(0);
                flecs::field<const Velocity3D>  vel = it.field<const Velocity3D>(1);

                for (const uint64_t i : it) {
                    float vx = vel[i].x;
                    float vy = vel[i].y;
                    float vz = vel[i].z;

                    if (f->use_drag) {
                        vx *= (1.0f - cfg->drag_coef * dt);
                        vy *= (1.0f - cfg->drag_coef * dt);
                        vz *= (1.0f - cfg->drag_coef * dt);
                    }

                    out[i].x += vx * dt;
                    out[i].y += vy * dt;
                    out[i].z += vz * dt;

                    const float half = config::WORLD_HALF;
                    auto wrap = [half](float& v) {
                        if      (v >  half) v -= 2.0f * half;
                        else if (v < -half) v += 2.0f * half;
                    };
                    wrap(out[i].x); wrap(out[i].y); wrap(out[i].z);
                }
            }
        });
    }

}