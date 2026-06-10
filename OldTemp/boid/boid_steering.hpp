#pragma once

#include <cstdint>

#include "boid_neighbor_query.hpp"
#include "../ecs/components.hpp"

namespace simnet::boid {
    void compute_boid_steering(
        const ecs::Position *pos,
        const ecs::Velocity *vel,
        ecs::SteeringAccumulate *out,
        std::uint64_t count,
        const ecs::BoidConfig &config,
        const ecs::BoidPerception &perception,
        const NeighborQuery &query
    );
} // namespace simnet::ecs::boid
