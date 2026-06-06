#include "simulation.hpp"

#include <cmath>

#include "components.hpp"
#include "config.hpp"

namespace simnet::sim {

    constexpr float SIM_DT_SECONDS = static_cast<float>(config::SIM_DT.count()) / 1'000'000'000.0f;

    Simulation::Simulation()
    {
        world_.component<ecs::Position3D>();
        world_.component<ecs::Velocity3D>();
        world_.component<ecs::Acceleration3D>();
        world_.component<ecs::Renderable>();

        world_.set<ecs::BoidConfig>({
            5.0f,
            5.0f,
            3.0f,
            1.5f,
            0.5f,
            0.8f,
            0.4f,
            10.0f
        });

// 1. Create the neighbor query ONCE outside the system definition
// This caches the archetype tracking so it is ultra-fast.
flecs::query<const ecs::Position3D, const ecs::Velocity3D> neighbor_query =
    world_.query<const ecs::Position3D, const ecs::Velocity3D>();

// 2. Define your system signature (Removed BoidConfig from here)
world_.system<ecs::Position3D, ecs::Velocity3D, ecs::Acceleration3D>()
    .kind(flecs::OnUpdate)
    // 3. Capture neighbor_query by reference in the lambda
    .run([neighbor_query](flecs::iter& it) {
        auto world = it.world();

        // try_get returns a pointer (nullptr if the singleton doesn't exist)
        const auto* cfg = world.try_get<ecs::BoidConfig>();
        if (!cfg) return;

        float perception_sq = cfg->perception_radius * cfg->perception_radius;

        while (it.next()) {
            auto pos = it.field<ecs::Position3D>(0);
            auto vel = it.field<ecs::Velocity3D>(1);
            auto acc = it.field<ecs::Acceleration3D>(2);

            for (auto i : it) {
                float sum_px = 0, sum_py = 0, sum_pz = 0;
                int count = 0;

                // 4. Safely and optimally iterate the pre-cached query
                neighbor_query.each([&](const ecs::Position3D& other_pos, const ecs::Velocity3D& other_vel) {
                    float dx = pos[i].x - other_pos.x;
                    float dy = pos[i].y - other_pos.y;
                    float dz = pos[i].z - other_pos.z;
                    float dist_sq = dx * dx + dy * dy + dz * dz;

                    if (dist_sq < 0.0001f) return; // Skip self

                    if (dist_sq < perception_sq) {
                        sum_px += other_pos.x;
                        sum_py += other_pos.y;
                        sum_pz += other_pos.z;
                        ++count;
                    }
                });

                if (count > 0) {
                    float avg_x = sum_px / count;
                    float avg_y = sum_py / count;
                    float avg_z = sum_pz / count;

                    float dx = avg_x - pos[i].x;
                    float dy = avg_y - pos[i].y;
                    float dz = avg_z - pos[i].z;
                    float len = sqrtf(dx*dx + dy*dy + dz*dz);

                    if (len > 0.0001f) {
                        dx /= len; dy /= len; dz /= len;
                        float desired_x = dx * cfg->max_speed;
                        float desired_y = dy * cfg->max_speed;
                        float desired_z = dz * cfg->max_speed;

                        float steer_x = desired_x - vel[i].x;
                        float steer_y = desired_y - vel[i].y;
                        float steer_z = desired_z - vel[i].z;

                        float steer_len = sqrtf(steer_x*steer_x + steer_y*steer_y + steer_z*steer_z);
                        if (steer_len > cfg->max_force) {
                            float scale = cfg->max_force / steer_len;
                            steer_x *= scale;
                            steer_y *= scale;
                            steer_z *= scale;
                        }

                        acc[i].x += steer_x;
                        acc[i].y += steer_y;
                        acc[i].z += steer_z;
                    }
                }
            }
        }
    });


        world_.system<ecs::Position3D, ecs::Velocity3D, ecs::Acceleration3D>()
            .kind(flecs::PostUpdate)
            .run([](flecs::iter& it) {
                float dt = it.delta_time();
                while (it.next()) {
                    auto pos = it.field<ecs::Position3D>(0);
                    auto vel = it.field<ecs::Velocity3D>(1);
                    auto acc = it.field<ecs::Acceleration3D>(2);
                    for (auto i : it) {
                        vel[i].x += acc[i].x * dt;
                        vel[i].y += acc[i].y * dt;
                        vel[i].z += acc[i].z * dt;

                        pos[i].x += vel[i].x * dt;
                        pos[i].y += vel[i].y * dt;
                        pos[i].z += vel[i].z * dt;

                        // Toroidal wrap
                        auto wrap = [](float& v) {
                            if      (v >  config::WORLD_HALF) v -= 2* config::WORLD_HALF;
                            else if (v < - config::WORLD_HALF) v += 2* config::WORLD_HALF;
                        };
                        wrap(pos[i].x); wrap(pos[i].y); wrap(pos[i].z);

                        // Reset acceleration for next tick
                        acc[i] = {0,0,0};
                    }
                }
            });

    }

    void Simulation::step()
    {
        world_.progress(SIM_DT_SECONDS);

        ++tick_;
    }

    uint64_t Simulation::current_tick() const
    {
        return tick_;
    }
}
