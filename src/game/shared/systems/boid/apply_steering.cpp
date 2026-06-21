#include <flecs.h>

#include "telemetry/telemetry.hpp"
#include "game/shared/game_shared.hpp"

namespace simnet::game::shared {
    void apply_steering_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_ApplySteering");

        const config::SimConfig &cfg = it.world().get<config::SimConfig>();
        const float dt = it.delta_time();

        const float max_accel = cfg.max_speed * cfg.max_accel_frac;

        while (it.next()) {
            auto acc = it.field<const SteeringAccumulate>(0);
            auto vel = it.field<Velocity>(1);

            for (size_t i: it) {
                vel[i].value = apply_steering(
                    acc[i].value,
                    vel[i].value,
                    max_accel,
                    cfg.max_speed,
                    dt);
            }
        }
    }
}
