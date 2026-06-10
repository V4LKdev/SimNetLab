#pragma once

#include <vector>
#include "vec3.hpp"

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
        std::vector<flecs::entity_t> indices;
    };

    // --- Tags ---
    struct Boid {
    };

    // --- Global Configuration ---
    struct BoidConfig {
        float max_speed = 50.0f;
        float max_accel = 30.f;

        float separation_weight = 1.0f;
        float alignment_weight = 1.0f;
        float cohesion_weight = 1.0f;
    };

    struct BoidPerception {
        float separation_radius = 20.0f;
        float alignment_radius = 20.0f;
        float cohesion_radius = 20.0f;

        float separation_fov_cos = -0.707f; // ~135deg
        float alignment_fov_cos = 0.7f; // ~45deg
        float cohesion_fov_cos = -0.15f; // ~100deg
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
        world.component<BoidConfig>();
        world.component<BoidPerception>();

        world.set<BoidConfig>({});
        world.set<BoidPerception>({});
    }
}
