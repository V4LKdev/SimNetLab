
#include <flecs.h>

#include "telemetry.hpp"
#include "ecs/components.hpp"

namespace simnet::ecs {
    namespace {
        Vec3 compute_separation_scalar(
            const Vec3 &self_pos,
            const std::vector<uint32_t> &neighbors,
            const std::vector<Vec3> &all_positions)
        {
            if (neighbors.empty()) return Vec3{0.0f, 0.0f, 0.0f};

            Vec3 force{0.0f, 0.0f, 0.0f};
            for (const uint32_t n: neighbors) {
                Vec3 offset = self_pos - all_positions[n];
                const float dist = offset.length();
                if (dist < 1e-8f) continue;

                // Reynolds: repulsive force = normalize(offset) * (1 / dist²)
                float invDist = 1.0f / dist;
                float scale = invDist * invDist * invDist;

                force += offset * scale;
            }

            // Sum of repulsive forces (not averaged)
            return force;
        }
    }


    void separation_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_Separation");

        const PositionCache &pos_cache = it.world().get<PositionCache>();

        while (it.next()) {
            auto pos = it.field<const Position>(0);
            auto nl = it.field<const NeighborList>(1);
            auto acc = it.field<SteeringAccumulate>(2);

            for (const uint64_t i: it) {
                const Vec3 steering = compute_separation_scalar(
                    pos[i].value,
                    nl[i].indices,
                    pos_cache.positions);
                acc[i].value += steering;
            }
        }
    }
}
