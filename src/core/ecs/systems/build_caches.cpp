#include "telemetry.hpp"
#include <flecs.h>

#include "ecs/components.hpp"


namespace simnet::ecs {
    namespace {
        void build_caches(const flecs::world &world, PositionCache &pos_cache, VelocityCache &vel_cache)
        {
            pos_cache.positions.clear();
            vel_cache.velocities.clear();

            auto q = world.query_builder<const Position, const Velocity>()
                    .with<Boid>()
                    .build();

            // Reserve to avoid reallocations
            size_t count = q.count();
            pos_cache.positions.reserve(count);
            vel_cache.velocities.reserve(count);

            q.each([&](const Position &p, const Velocity &v) {
                pos_cache.positions.push_back(p.value);
                vel_cache.velocities.push_back(v.value);
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
