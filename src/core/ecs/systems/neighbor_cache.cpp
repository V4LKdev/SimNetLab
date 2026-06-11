#include <flecs.h>

#include "telemetry.hpp"
#include "ecs/components.hpp"

namespace simnet::ecs {
    namespace {
        void build_caches(const flecs::world &world, PositionCache &pos_cache, VelocityCache &vel_cache)
        {
            pos_cache.positions.clear();
            pos_cache.entity_to_index.clear();
            vel_cache.velocities.clear();
            vel_cache.entity_to_index.clear();

            auto q = world.query_builder<const Position, const Velocity>().with<Boid>().build();

            q.each([&](const flecs::entity e, const Position &p, const Velocity &v) {
                uint32_t idx = static_cast<uint32_t>(pos_cache.positions.size());
                pos_cache.positions.push_back(p.value);
                vel_cache.velocities.push_back(v.value);
                pos_cache.entity_to_index[e] = idx;
                vel_cache.entity_to_index[e] = idx;
            });
        }

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

        // Build SoA caches once
        {
            TELEM_TRACY_ZONE("BuildCaches");
            auto &pos_cache = it.world().get_mut<PositionCache>();
            auto &vel_cache = it.world().get_mut<VelocityCache>();
            build_caches(it.world(), pos_cache, vel_cache);
        }

        // Compute neighbors for each boid
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
