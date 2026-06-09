#include "boid_neighbor_query.hpp"

namespace simnet::boid {
    std::uint32_t query_bruteforce(
        const ecs::Position &center,
        const ecs::Position *positions,
        const ecs::Velocity *velocities,
        std::uint32_t count,
        std::uint32_t self_index,
        Neighbor *out,
        std::uint32_t max_out,
        float max_dist2)
    {
        std::uint32_t n = 0;

        for (std::uint32_t i = 0; i < count && n < max_out; ++i) {
            if (i == self_index) continue;

            const auto &p = positions[i].value;

            float dx = p.x - center.value.x;
            float dy = p.y - center.value.y;
            float dz = p.z - center.value.z;

            float dist2 = dx * dx + dy * dy + dz * dz;

            if (dist2 < max_dist2) {
                out[n].pos = positions[i];
                out[n].vel = velocities[i];
                ++n;
            }
        }

        return n;
    }
}
