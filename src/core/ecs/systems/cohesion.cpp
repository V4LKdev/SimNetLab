
#include <flecs.h>

#include "telemetry.hpp"
#include "ecs/components.hpp"

namespace simnet::ecs {
    namespace {
        Vec3 compute_cohesion_scalar(
            const Vec3 &self_pos,
            const std::vector<uint32_t> &neighbors,

            const std::vector<Vec3> &all_positions)
        {
            if (neighbors.empty()) return Vec3{0, 0, 0};

            Vec3 center{0.0f, 0.0f, 0.0f};
            for (uint32_t n: neighbors) {
                center += all_positions[n];
            }
            center /= static_cast<float>(neighbors.size());

            return center - self_pos;
        }
    }

    void cohesion_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_Cohesion");

        const PositionCache &pos_cache = it.world().get<PositionCache>();

        while (it.next()) {
            auto pos = it.field<const Position>(0);
            auto nl = it.field<const NeighborList>(1);
            auto acc = it.field<SteeringAccumulate>(2);

            for (const uint64_t i: it) {
                const Vec3 steering = compute_cohesion_scalar(
                    pos[i].value,
                    nl[i].indices,
                    pos_cache.positions);
                acc[i].value += steering;
            }
        }
    }
}
