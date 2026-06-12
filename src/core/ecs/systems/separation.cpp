
#include <flecs.h>

#include "telemetry.hpp"
#include "ecs/components.hpp"
#include "math/rules_scalar.hpp"

namespace simnet::ecs {
    void separation_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_Separation");

        const BoidConfig &cfg = it.world().get<BoidConfig>();
        const PositionCache &pos_cache = it.world().get<PositionCache>();

        const float weight = cfg.separation_weight;
        const float radius = cfg.separation_radius;
        const float fov_cos = cfg.separation_fov_cos;

        // Early return when the rule is turned off
        if (weight <= 0.0f) {
            return;
        }

        while (it.next()) {
            auto hd = it.field<const Heading>(0);
            auto nl = it.field<const NeighborList>(1);
            auto acc = it.field<SteeringAccumulate>(2);

            for (int64_t i = 0; i < it.count(); ++i) {
                const uint32_t self_idx = it.entity(i);

                Vec3 steering =
                        scalar::compute_separation(
                            hd[i].value,
                            self_idx,
                            nl[i].indices,
                            pos_cache.positions,
                            radius,
                            fov_cos);


                // Apply strength mod
                steering = steering * weight;

                acc[i].value += steering;
            }
        }
    }
}
