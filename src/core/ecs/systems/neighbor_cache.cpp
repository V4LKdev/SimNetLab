#include <flecs.h>

#include "telemetry.hpp"
#include "ecs/components.hpp"

namespace simnet::ecs {
    namespace {
        void build_neighbor_cache_bruteforce(
            flecs::entity self,
            const Position &self_pos,
            const flecs::query<const Position> &q,
            float radius_sq)
        {
            TELEM_TRACY_ZONE("Sim_Query_Bruteforce");

            NeighborList &list = self.get_mut<NeighborList>();
            list.indices.clear();

            q.each([&](flecs::entity other, const Position &p) {
                if (other == self) return;

                const float dist2 = p.value.dist2(self_pos.value);

                if (dist2 < radius_sq) {
                    list.indices.push_back(other);
                }
            });
        }
    }


    void neighbor_cache_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_NeighborCacheSystem");

        const BoidPerception &p = it.world().get<BoidPerception>();
        flecs::query<const Position> q = it.world().query<const Position>();

        const float radius = std::max({
            p.separation_radius,
            p.alignment_radius,
            p.cohesion_radius
        });

        const float radius_sq = radius * radius;
        while (it.next()) {
            for (uint64_t i: it) {
                flecs::entity e = it.entity(i);
                const Position &e_pos = e.get<Position>();

                build_neighbor_cache_bruteforce(
                    e,
                    e_pos,
                    q,
                    radius_sq
                );
            }
        }
    }
}
