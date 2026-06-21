#include <flecs.h>

#include "telemetry/telemetry.hpp"
#include "game/shared/game_shared.hpp"


namespace simnet::game::shared {
    void integrate_position_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_IntegratePosition");

        const float dt = it.delta_time();
        const config::SimConfig &cfg = it.world().get<config::SimConfig>();
        const float world_half = cfg.world_half;

        while (it.next()) {
            auto vel = it.field<const Velocity>(0);
            auto pos = it.field<Position>(1);

            for (size_t i: it) {
                pos[i].value = (pos[i].value + vel[i].value * dt).wrap(world_half);
            }
        }
    }
}
