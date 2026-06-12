
#include <flecs.h>

#include "telemetry.hpp"
#include "ecs/components.hpp"
#include "math/rules_scalar.hpp"

namespace simnet::ecs {
    void alignment_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_Alignment");

        const BoidConfig &cfg = it.world().get<BoidConfig>();
        const PositionCache &pos_cache = it.world().get<PositionCache>();
        const VelocityCache &vel_cache = it.world().get<VelocityCache>();

        const float weight = cfg.alignment_weight;
        const float radius = cfg.alignment_radius;
        const float fov_cos = cfg.alignment_fov_cos;

        // Early return on no rule impact
        if (weight <= 0.0f)
            return;

        while (it.next()) {
            auto hd = it.field<const Heading>(0);
            auto nl = it.field<const NeighborList>(1);
            auto acc = it.field<SteeringAccumulate>(2);

            for (int64_t i = 0; i < it.count(); ++i) {
                const uint32_t self_idx = it.entity(i);

                Vec3 steering =
                        scalar::compute_alignment(
                            hd[i].value,
                            self_idx,
                            nl[i].indices,
                            pos_cache.positions,
                            vel_cache.velocities,
                            radius,
                            fov_cos
                        );

                steering = steering * weight;

                acc[i].value += steering;
            }
        }
    }
}
