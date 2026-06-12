#include <flecs.h>

#include "config.hpp"
#include "telemetry.hpp"
#include "ecs/components.hpp"

namespace simnet::ecs {
    namespace {
        void compute_neighbors_bruteforce(
            const uint32_t self_idx,
            const Vec3 self_pos,
            const std::vector<Vec3> &all_positions,
            const float radius_sq,
            std::vector<uint32_t> &out_indices)
        {
        }
    }

    void neighbor_cache_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_NeighborCacheSystem");

        const BoidConfig &cfg = it.world().get<BoidConfig>();
        const float radius = std::max({cfg.separation_radius, cfg.alignment_radius, cfg.cohesion_radius});
        const float radius_sq = radius * radius;

        while (it.next()) {
            // Direct access to the Position component array for this archetype
            const auto pos_comp = it.field<const Position>(0);
            auto nl = it.field<NeighborList>(1);

            for (uint64_t i: it) {
                Vec3 self_pos = pos_comp[i].value; // i is stable row index
                auto &neighbors = nl[i].indices;
                neighbors.clear();

                // TODO: extract out again
                const uint64_t total = it.count(); // number of boids in this chunk
                for (uint64_t j = 0; j < total; ++j) {
                    if (j == i) continue;
                    Vec3 delta = Vec3::wrap_delta(self_pos, pos_comp[j].value, config::WORLD_HALF);
                    if (delta.length_sq() < radius_sq) {
                        neighbors.push_back(static_cast<uint32_t>(j));
                    }
                }
            }
        }
    }
}
