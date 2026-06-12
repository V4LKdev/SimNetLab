#include <flecs.h>

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
            out_indices.clear();

            for (uint32_t j = 0; j < all_positions.size(); ++j) {
                if (j == self_idx) continue;
                if (all_positions[j].dist2(self_pos) < radius_sq) {
                    out_indices.push_back(j);
                }
            }
        }
    }

    void neighbor_cache_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_NeighborCacheSystem");

        const BoidConfig &cfg = it.world().get<BoidConfig>();
        const float radius = std::max({
            cfg.separation_radius,
            cfg.alignment_radius,
            cfg.cohesion_radius
        });
        const float radius_sq = radius * radius;

        const PositionCache &pos_cache = it.world().get<PositionCache>();
        const auto &positions = pos_cache.positions;

        while (it.next()) {
            auto nl = it.field<NeighborList>(1);

            for (uint64_t i: it) {
                uint32_t self_idx = static_cast<uint32_t>(i);
                Vec3 self_pos = positions[self_idx];

                std::vector<uint32_t> &neighbors = nl[i].indices;
                neighbors.clear();

                compute_neighbors_bruteforce(
                    self_idx,
                    self_pos,
                    positions,
                    radius_sq,
                    neighbors);
            }
        }
    }
}
