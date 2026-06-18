#include <flecs.h>

#include "../../../core/config/sim_config.hpp"
#include "telemetry.hpp"
#include "../components.hpp"


namespace simnet::ecs {
    namespace {
        Vec3 integrate_position_scalar(
            Vec3 position,
            const Vec3 &velocity,
            float dt)
        {
            return position + velocity * dt;
        }
    }

    void integrate_position_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_IntegratePosition");

        const float dt = it.delta_time();
        const SimConfig &cfg = it.world().get<SimConfig>();

        while (it.next()) {
            auto vel = it.field<const Velocity>(0);
            auto pos = it.field<Position>(1);

            for (size_t i: it) {
                pos[i].value = integrate_position_scalar(pos[i].value, vel[i].value, dt).wrap(cfg.world_half);
            }
        }
    }
}
