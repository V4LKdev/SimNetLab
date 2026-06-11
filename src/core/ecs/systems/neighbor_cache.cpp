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

                const float dist2 = all_positions[j].dist2(self_pos);

                if (dist2 < radius_sq) {
                    out_indices.push_back(j);
                }
            }
        }
    }

    // Flecs system callback
    void neighbor_cache_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_NeighborCacheSystem");

        const BoidPerception &p = it.world().get<BoidPerception>();
        const float radius = std::max({
            p.separation_radius,
            p.alignment_radius,
            p.cohesion_radius
        });

        const float radius_sq = radius * radius;

        {
            TELEM_TRACY_ZONE("BruteforceNeighbors");
            const auto &pos_cache = it.world().get<PositionCache>();

            while (it.next()) {
                auto nl = it.field<NeighborList>(1);

                for (const uint64_t i: it) {
                    flecs::entity e = it.entity(i);
                    auto self_it = pos_cache.entity_to_index.find(e);
                    if (self_it == pos_cache.entity_to_index.end()) continue;

                    const uint32_t self_idx = self_it->second;
                    const Vec3 self_pos = pos_cache.positions[self_idx];

                    compute_neighbors_bruteforce(
                        self_idx,
                        self_pos,
                        pos_cache.positions,
                        radius_sq,
                        nl[i].indices);
                }
            }
        }
    }
}
