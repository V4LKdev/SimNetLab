#include "telemetry.hpp"
#include <flecs.h>

#include "ecs/components.hpp"


namespace simnet::ecs {
    namespace {
        void build_caches(const flecs::world &world, PositionCache &pos_cache, VelocityCache &vel_cache)
        {
            pos_cache.positions.clear();
            pos_cache.entity_to_index.clear();
            vel_cache.velocities.clear();
            vel_cache.entity_to_index.clear();

            const auto q = world.query_builder<const Position, const Velocity>().with<Boid>().build();

            q.each([&](const flecs::entity e, const Position &p, const Velocity &v) {
                uint32_t idx = static_cast<uint32_t>(pos_cache.positions.size());
                pos_cache.positions.push_back(p.value);
                vel_cache.velocities.push_back(v.value);
                pos_cache.entity_to_index[e] = idx;
                vel_cache.entity_to_index[e] = idx;
            });
        }
    }

    void build_caches_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_BuildCaches");

        auto &pos_cache = it.world().get_mut<PositionCache>();
        auto &vel_cache = it.world().get_mut<VelocityCache>();
        build_caches(it.world(), pos_cache, vel_cache);
    }
}
