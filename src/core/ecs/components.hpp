#pragma once

#include <flecs.h>
#include <vector>
#include "../math/vec3.hpp"

namespace simnet::ecs {
    // --- Boid Components ---
    /// 3D position of a entity.
    struct Position {
        Vec3 value;
    };

    /// 3D velocity vector of an entity.
    struct Velocity {
        Vec3 value;
    };

    /// Normalized forward direction. Defaults to +X or the last non-zero velocity.
    struct Heading {
        Vec3 value = {1.0f, 0.0f, 0.0f};
    };

    /// Accumulated steering force for this tick.
    struct SteeringAccumulate {
        Vec3 value{};
    };

    /// Tick-local per-entity index assigned during snapshot construction.
    struct BoidIdx {
        uint32_t index = 0;
    };

    // --- Tags ---
    /// Marking entities that participate in flocking.
    struct Boid {
    };

    // --- Global Configuration ---
    /// Singleton holding flock-level tuning parameters.
    struct BoidConfig {
        float max_speed = 10.0f;
        // fraction of max speed
        float max_accel_frac = 3.0f;

        float separation_strength = 12.0f;
        float alignment_strength = 8.0f;
        float cohesion_strength = 8.0f;

        float separation_radius = 5.0f;
        float alignment_radius = 7.5f;
        float cohesion_radius = 9.0f;

        float separation_fov_cos = -0.707f; // ~135deg
        float alignment_fov_cos = 0.7f; // ~45deg
        float cohesion_fov_cos = 0.15f; // ~100deg
    };

    // --- Telemetry Singletons ---
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
    struct NeighborSnapshot {
        std::vector<Vec3> positions;
        std::vector<Vec3> headings;
        std::vector<size_t> offsets;
        std::vector<uint32_t> entries;
    };

    inline void register_components(flecs::world &world)
    {
        world.component<Position>();
        world.component<Velocity>();
        world.component<Heading>();
        world.component<SteeringAccumulate>();
        world.component<BoidIdx>();

        world.component<Boid>();

        // Singletons
        world.component<BoidConfig>().add(flecs::Singleton);
        world.component<FlockStats>().add(flecs::Singleton);
        world.component<NeighborSnapshot>().add(flecs::Singleton);
        world.set<BoidConfig>({});
        world.set<FlockStats>({});
        world.set<NeighborSnapshot>({});
    }
}
