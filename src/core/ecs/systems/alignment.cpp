
#include <flecs.h>

#include "telemetry.hpp"
#include "ecs/components.hpp"
#include "math/rules_scalar.hpp"

namespace simnet::ecs {
    void alignment_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_Alignment");

        const BoidConfig &cfg = it.world().get<BoidConfig>();
        const float weight = cfg.alignment_strength;
        const float radius = cfg.alignment_radius;
        const float fov_cos = cfg.alignment_fov_cos;

        if (weight <= 0.0f) return;

        while (it.next()) {
            auto hd = it.field<const Heading>(0);
            auto nl = it.field<const NeighborList>(1);
            auto acc = it.field<SteeringAccumulate>(2);
            auto pos = it.field<const Position>(3);

            for (int64_t i = 0; i < it.count(); ++i) {
                Vec3 steering = scalar::compute_alignment(
                    hd[i].value,
                    i,
                    nl[i].indices,
                    &pos[0],
                    &hd[0],
                    radius,
                    fov_cos);

                steering *= weight;

                acc[i].value += steering;
            }
        }
    }
}
