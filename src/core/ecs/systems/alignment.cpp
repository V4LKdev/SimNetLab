
#include <flecs.h>

#include "telemetry.hpp"
#include "ecs/components.hpp"

namespace simnet::ecs {
    namespace {
        Vec3 compute_alignment_scalar(
            const Vec3 &self_vel,
            const std::vector<uint32_t> &neighbors,
            const std::vector<Vec3> &all_velocities)
        {
            if (neighbors.empty()) return Vec3{0, 0, 0};

            Vec3 average_vel{0, 0, 0};
            for (const uint32_t n: neighbors) {
                average_vel += all_velocities[n];
            }
            average_vel /= static_cast<float>(neighbors.size());

            return average_vel - self_vel;
        }
    } // namespace

    void alignment_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_Alignment");

        const VelocityCache &vel_cache = it.world().get<VelocityCache>();

        while (it.next()) {
            auto vel = it.field<const Velocity>(0);
            auto nl = it.field<const NeighborList>(1);
            auto acc = it.field<SteeringAccumulate>(2);

            for (const uint64_t i: it) {
                const Vec3 steering = compute_alignment_scalar(
                    vel[i].value,
                    nl[i].indices,
                    vel_cache.velocities);
                acc[i].value += steering;
            }
        }
    }
}
