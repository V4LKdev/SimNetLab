#include <flecs.h>

#include "telemetry.hpp"
#include "ecs/components.hpp"
#include "math/rules_scalar.hpp"

namespace simnet::ecs {
    void apply_steering_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_ApplySteering");

        const BoidConfig &cfg = it.world().get<BoidConfig>();
        const float dt = it.delta_time();

        const float max_accel = cfg.max_speed * cfg.max_accel_frac;

        while (it.next()) {
            auto acc = it.field<const SteeringAccumulate>(0);
            auto vel = it.field<Velocity>(1);

            for (uint64_t i: it) {
                vel[i].value = scalar::apply_steering(
                    acc[i].value,
                    vel[i].value,
                    max_accel,
                    cfg.max_speed,
                    dt);
            }
        }
    }
}
