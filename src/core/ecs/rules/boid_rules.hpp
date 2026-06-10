#pragma once

#include <flecs.h>
#include <span>
#include <vector>

#include "vec3.hpp"

namespace simnet::ecs {
    void find_neighbors_brute_force(
        flecs::entity_t boid_id,
        std::span<const Vec3> positions, // span provides non-owning view of contiguous data
        float radius,
        std::vector<flecs::entity_t> &out_indices);


    Vec3 compute_alignment(
        std::span<const Vec3> neighbor_velocities);

    Vec3 compute_cohesion(
        Vec3 self_position,
        std::span<const Vec3> neighbor_positions);

    Vec3 compute_separation(
        Vec3 self_position,
        std::span<const Vec3> neighbor_positions);
}
