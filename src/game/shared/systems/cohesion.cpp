
#include <flecs.h>

#include "../../../core/config/sim_config.hpp"
#include "telemetry.hpp"
#include "../components.hpp"
#include "math/rules_scalar.hpp"

namespace simnet::ecs {
    void cohesion_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_Cohesion");

        const SimConfig &cfg = it.world().get<SimConfig>();
        const float weight = cfg.cohesion_strength;
        const float radius = cfg.cohesion_radius;
        const float fov_cos = cfg.cohesion_fov_cos;

        const NeighborCache &snap = it.world().get<NeighborCache>();

        while (it.next()) {
            // Early return when the rule is turned off or ineffective
            if (!cfg.enable_cohesion || weight <= 0.0f || radius <= 0.0f || fov_cos >= 1.0f) {
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
                    fov_cos,
                    cfg.world_half);

                steering *= weight;
                acc[i].value += steering;
            }
        }
    }
}
