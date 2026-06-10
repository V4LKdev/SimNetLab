
#include <flecs.h>

#include "telemetry.hpp"
#include "ecs/components.hpp"

namespace simnet::ecs {
    namespace {
        void compute_alignment_rule_scalar(
            const flecs::world &world,
            const Velocity &self_vel,
            const NeighborList &neighbors,
            SteeringAccumulate &acc)
        {
            Vec3 average_vel{0.0f, 0.0f, 0.0f};
            std::uint32_t count = 0;

            for (const flecs::entity_t &id: neighbors.indices) {
                const flecs::entity other(world, id);
                const Velocity &nv = other.get<Velocity>();

                average_vel += nv.value;
                ++count;
            }

            if (count == 0) {
                return;
            }

            average_vel /= static_cast<float>(count);

            const Vec3 steering = average_vel - self_vel.value;
            acc.value += steering;
        }
    } // namespace

    void alignment_system(flecs::iter &it)
    {
        TELEM_TRACY_ZONE("Sim_Alignment");

        while (it.next()) {
            auto vel = it.field<const Velocity>(0);
            auto nl = it.field<const NeighborList>(1);
            auto acc = it.field<SteeringAccumulate>(2);

            const flecs::world world = it.world();

            for (const uint64_t i: it) {
                compute_alignment_rule_scalar(
                    world,
                    vel[i],
                    nl[i],
                    acc[i]);
            }
        }
    }
}
