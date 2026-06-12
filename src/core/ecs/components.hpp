#pragma once

#include <flecs.h>
#include <vector>
#include "../math/vec3.hpp"

namespace simnet::ecs {
    // --- Boid Components ---
    struct Position {
        Vec3 value;
    };

    struct Velocity {
        Vec3 value;
    };

    struct Heading {
        Vec3 value = {1.0f, 0.0f, 0.0f}; // last non-zero forward
    };

    struct SteeringAccumulate {
        Vec3 value{};
    };

    struct NeighborList {
        std::vector<uint32_t> indices;
    };

    // --- Caches ---
    struct PositionCache {
        std::vector<Vec3> positions;
    };

    struct VelocityCache {
        std::vector<Vec3> velocities;
    };


    // --- Tags ---
    struct Boid {
    };

    // --- Global Configuration ---
    struct BoidConfig {
        float max_speed = 10.0f;
        // fraction of max speed
        float max_accel_frac = 3.0f;

        float separation_strength = 12.0f; // 12
        float alignment_strength = 8.0f; // 8
        float cohesion_strength = 8.0f; // 8

        float separation_radius = 5.0f;
        float alignment_radius = 7.5f;
        float cohesion_radius = 9.0f;

        float separation_fov_cos = -0.707f; // ~135deg
        float alignment_fov_cos = 0.7f; // ~45deg
        float cohesion_fov_cos = 0.15f; // ~100deg
    };

    // --- Telemetry Singletons ---
    struct FlockStats {
        float avg_speed = 0.0f;
        float avg_steer = 0.0f;
        float avg_neighbors = 0.0f;
        float min_nbr_dist = 0.0f; // average distance to closest neighbor
        float polarisation = 0.0f; // 1 = perfectly aligned, 0 = random
        int boid_count = 0;
    };


    inline void register_components(flecs::world &world)
    {
        world.component<Position>();
        world.component<Velocity>();
        world.component<Heading>();
        world.component<SteeringAccumulate>();

        world.component<Boid>();
        world.component<NeighborList>();

        // Singletons
        // world.component<PositionCache>();
        // world.component<VelocityCache>();
        world.component<BoidConfig>();
        world.component<FlockStats>();

        // world.set<PositionCache>({});
        // world.set<VelocityCache>({});
        world.set<BoidConfig>({});
        world.set<FlockStats>({});
    }
}
