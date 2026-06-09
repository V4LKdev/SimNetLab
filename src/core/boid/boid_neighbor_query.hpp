#pragma once

#include <cstdint>
#include "../ecs/components.hpp"

namespace simnet::boid
{
    struct Neighbor
    {
        ecs::Position pos;
        ecs::Velocity vel;
    };

    struct NeighborQuery
    {
        using QueryFn = std::uint32_t (*)(
            const ecs::Position& center,
            const ecs::Position* positions,
            const ecs::Velocity* velocities,
            std::uint32_t count,
            std::uint32_t self_index,
            Neighbor* out,
            std::uint32_t max_out,
            float max_dist2
        );

        QueryFn fn;
    };

    std::uint32_t query_bruteforce(
        const ecs::Position& center,
        const ecs::Position* positions,
        const ecs::Velocity* velocities,
        std::uint32_t count,
        std::uint32_t self_index,
        Neighbor* out,
        std::uint32_t max_out,
        float max_dist2
    );
}