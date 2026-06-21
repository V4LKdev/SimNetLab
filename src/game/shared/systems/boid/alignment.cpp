
#include <flecs.h>

#include "telemetry/telemetry.hpp"
#include "game/shared/game_shared.hpp"

namespace simnet::game::shared {
    void alignment_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_Alignment");

        const config::SimConfig &cfg = it.world().get<config::SimConfig>();
        const float weight = cfg.alignment_strength;
        const float radius = cfg.alignment_radius;
        const float fov_cos = cfg.alignment_fov_cos;

        if (!cfg.enable_alignment || weight <= 0.0f || radius <= 0.0f || fov_cos >= 1.0f) {
            return;
        }

        const NeighborCache &cache = it.world().get<NeighborCache>();

        while (it.next()) {
            auto idx = it.field<const BoidIdx>(0);
            auto acc = it.field<SteeringAccumulate>(1);

            for (size_t i = 0; i < it.count(); ++i) {
                const uint32_t g = idx[i].index;
                const size_t beg = cache.offsets[g];
                const size_t end = cache.offsets[g + 1];
                const uint32_t *neighbors = cache.entries.data() + beg;
                const size_t count = end - beg;

                math::Vec3 steering = compute_alignment(
                    cache.positions[g],
                    cache.headings[g],
                    neighbors,
                    count,
                    cache.positions.data(),
                    cache.headings.data(),
                    radius,
                    fov_cos,
                    cfg.world_half
                );

                acc[i].value += steering * weight;
            }
        }
    }
}
