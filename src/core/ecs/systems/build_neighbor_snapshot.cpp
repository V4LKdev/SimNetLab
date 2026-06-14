#include <flecs.h>
#include <cassert>

#include "config.hpp"
#include "ecs/components.hpp"
#include "telemetry.hpp"

namespace simnet::ecs {
    namespace {
        void brute_force_neighbors(const NeighborSnapshot &snap,
                                   const BoidConfig &cfg,
                                   std::vector<size_t> &offsets,
                                   std::vector<uint32_t> &entries)
        {
            const float radius = std::max({
                cfg.separation_radius,
                cfg.alignment_radius,
                cfg.cohesion_radius
            });
            const float radius_sq = radius * radius;
            const size_t count = snap.positions.size();

            offsets.resize(count + 1, 0);
            entries.clear();
            // Reserve a worst-case guess to avoid quadratic-realloc
            entries.reserve(count * 64);

            for (size_t i = 0; i < count; ++i) {
                offsets[i] = entries.size();
                const Vec3 &pos_i = snap.positions[i];

                for (size_t j = 0; j < count; ++j) {
                    if (i == j) continue;
                    const Vec3 delta = Vec3::wrap_delta(pos_i, snap.positions[j],
                                                        config::WORLD_HALF);
                    if (delta.length_sq() < radius_sq) {
                        entries.push_back(static_cast<uint32_t>(j));
                    }
                }
            }
            offsets[count] = entries.size();
        }
    }

    void build_neighbor_snapshot_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_BuildNeighborSnapshot");

        flecs::world world = it.world();
        const BoidConfig &cfg = world.get<BoidConfig>();

        // gather all boid data, assign indices
        NeighborSnapshot snap;

        // Collect data and assign BoidIdx
        while (it.next()) {
            auto pos = it.field<const Position>(0);
            auto hd = it.field<const Heading>(1);

            for (size_t i = 0; i < it.count(); ++i) {
                const uint32_t idx = static_cast<uint32_t>(snap.positions.size());
                snap.positions.push_back(pos[i].value);
                snap.headings.push_back(hd[i].value);

                it.entity(i).set<BoidIdx>({idx});
            }
        }

        if (snap.positions.empty()) {
            world.set<NeighborSnapshot>(std::move(snap));
            return;
        }

        // O(N^2) neighbour search
        std::vector<size_t> offsets;
        std::vector<uint32_t> entries;
        brute_force_neighbors(snap, cfg, offsets, entries);

        snap.offsets = std::move(offsets);
        snap.entries = std::move(entries);

        // Store the finished snapshot (replaces previous frame's data)
        world.set<NeighborSnapshot>(std::move(snap));
    }
} // namespace simnet::ecs
