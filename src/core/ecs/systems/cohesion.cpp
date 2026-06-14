
#include <flecs.h>

#include "telemetry.hpp"
#include "ecs/components.hpp"
#include "math/rules_scalar.hpp"

namespace simnet::ecs {
    void cohesion_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_Cohesion");

        const BoidConfig &cfg = it.world().get<BoidConfig>();
        const float weight = cfg.cohesion_strength;
        const float radius = cfg.cohesion_radius;
        const float fov_cos = cfg.cohesion_fov_cos;

        const NeighborSnapshot &snap = it.world().get<NeighborSnapshot>();

        while (it.next()) {
            // Early return when the rule is turned off or ineffective
            if (weight <= 0.0f || radius <= 0.0f || fov_cos >= 1.0f) {
                continue;
            }

            auto idx = it.field<const BoidIdx>(0);
            auto acc = it.field<SteeringAccumulate>(1);

            for (size_t i = 0; i < it.count(); ++i) {
                const uint32_t g = idx[i].index;
                const size_t beg = snap.offsets[g];
                const size_t end = snap.offsets[g + 1];

                const uint32_t *neighbors = snap.entries.data() + beg;
                const size_t count = end - beg;

                Vec3 steering = scalar::compute_cohesion(
                    snap.positions[g],
                    snap.headings[g],
                    neighbors,
                    count,
                    snap.positions.data(),
                    snap.headings.data(),
                    radius,
                    fov_cos);

                steering *= weight;
                acc[i].value += steering;
            }
        }
    }
}
