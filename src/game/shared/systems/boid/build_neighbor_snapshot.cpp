#include <flecs.h>
#include <cassert>

#include "telemetry/telemetry.hpp"
#include "game/shared/game_shared.hpp"

namespace simnet::game::shared {
    namespace {
        void brute_force_neighbors(const NeighborCache &snap,
                                   const config::SimConfig &cfg,
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
            entries.clear(); // entries already has capacity from outside

            for (size_t i = 0; i < count; ++i) {
                offsets[i] = entries.size();
                const math::Vec3 &pos_i = snap.positions[i];

                for (size_t j = 0; j < count; ++j) {
                    if (i == j) continue;
                    const math::Vec3 delta = math::Vec3::wrap_delta(pos_i, snap.positions[j], cfg.world_half);
                    if (delta.length_sq() < radius_sq) {
                        entries.push_back(static_cast<uint32_t>(j));
                    }
                }
            }
            offsets[count] = entries.size();
        }
    }

    void build_neighbor_cache_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_BuildNeighborSnapshot");

        flecs::world world = it.world();
        const config::SimConfig &cfg = world.get<config::SimConfig>();

        // Conservative pre‑allocation based on configured upper bound
        NeighborCache cache;
        cache.positions.reserve(cfg.max_boids);
        cache.headings.reserve(cfg.max_boids);
        cache.offsets.reserve(cfg.max_boids + 1); // offsets size = count + 1

        // Reserve a worst‑case guess for entries
        std::vector<size_t> offsets;
        std::vector<uint32_t> entries;
        entries.reserve(cfg.max_boids * 64);

        // Collection loop...
        while (it.next()) {
            auto pos = it.field<const Position>(0);
            auto hd = it.field<const Heading>(1);
            for (size_t i = 0; i < it.count(); ++i) {
                // push_back will never reallocate as long as count <= max_boids
                cache.positions.push_back(pos[i].value);
                cache.headings.push_back(hd[i].value);
                it.entity(i).set<BoidIdx>({static_cast<uint32_t>(cache.positions.size() - 1)});
            }
        }

        if (cache.positions.empty()) {
            world.set<NeighborCache>(std::move(cache));
            return;
        }

        brute_force_neighbors(cache, cfg, offsets, entries);

        cache.offsets = std::move(offsets);
        cache.entries = std::move(entries);

        world.set<NeighborCache>(std::move(cache));
    }
}
