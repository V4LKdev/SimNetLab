#pragma once

#include <flecs.h>
#include <vector>
#include "core/core.hpp"

namespace simnet::game::shared {
    // --- Boid Components ---
    /// 3D position of a entity.
    struct Position {
        math::Vec3 value = math::Vec3::zero();
    };

    /// 3D velocity vector of an entity.
    struct Velocity {
        math::Vec3 value = math::Vec3::zero();
    };

    /// Normalized forward direction. Defaults to +X or the last non-zero velocity.
    struct Heading {
        math::Vec3 value = {1.0f, 0.0f, 0.0f};
    };

    /// Accumulated steering force for this tick.
    struct SteeringAccumulate {
        math::Vec3 value = math::Vec3::zero();
    };

    /// Hue of an entity
    struct Hue {
        uint8_t value = 0;
    };

    /// Frame persistent unique identifier of the entity.
    struct NetworkId {
        uint32_t value = 0;
    };

    /// Tick-local per-entity index assigned during snapshot construction.
    struct BoidIdx {
        uint32_t index = 0;
    };

    // --- Tags ---
    /// Marking entities that participate in flocking.
    struct Boid {
    };

    // --- Singletons ---
    /// Singleton updated each frame with aggregate flock statistics.
    struct FlockStats {
        float avg_speed = 0.0f;
        float avg_steer = 0.0f;
        float avg_neighbors = 0.0f;
        float min_nbr_dist = 0.0f; // average distance to closest neighbor
        float polarisation = 0.0f; // 1 = perfectly aligned, 0 = random
        int boid_count = 0;
    };

    /// Singleton read-only neighbor cache for current tick.
    struct NeighborCache {
        std::vector<math::Vec3> positions;
        std::vector<math::Vec3> headings;
        std::vector<size_t> offsets;
        std::vector<uint32_t> entries;
    };
}
