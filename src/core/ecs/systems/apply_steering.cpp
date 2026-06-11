#include <flecs.h>

#include "telemetry.hpp"
#include "ecs/components.hpp"

namespace simnet::ecs {
    namespace {
        Vec3 apply_steering_scalar(
            const Vec3 &steering,
            Vec3 current_vel,
            float max_force,
            float max_speed,
            float dt)
        {
            // Truncate steering to max_force
            float steer_len = steering.length();
            if (steer_len > max_force) {
                current_vel += steering * (max_force / steer_len) * dt;
            } else {
                current_vel += steering * dt;
            }

            // Truncate speed to max_speed
            float speed = current_vel.length();
            if (speed > max_speed) {
                current_vel = current_vel * (max_speed / speed);
            }

            return current_vel;
        }
    }

    void apply_steering_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_ApplySteering");

        const BoidConfig &cfg = it.world().get<BoidConfig>();
        const float dt = it.delta_time();

        while (it.next()) {
            auto acc = it.field<const SteeringAccumulate>(0);
            auto vel = it.field<Velocity>(1);

            for (uint64_t i: it) {
                vel[i].value = apply_steering_scalar(
                    acc[i].value,
                    vel[i].value,
                    cfg.max_accel,
                    cfg.max_speed,
                    dt);
            }
        }
    }
}
