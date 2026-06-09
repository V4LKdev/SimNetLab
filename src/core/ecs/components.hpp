#pragma once

#include <cstdint>
#include "../boid/boid_math.hpp"

namespace simnet::ecs {
    // Core simulation components

    struct Position {
        Vec3 value;
    };

    struct Velocity {
        Vec3 value;
    };

    struct DesiredVelocity {
        Vec3 value;
    };

    struct Boid {
    };

    // Boid simulation settings (singletons)

    struct BoidConfig {
        float max_speed = 150.0f;
        float max_force = 10.0f;
        float mass = 1.0f;

        float separation_weight = 0.0f;
        float alignment_weight = 0.5f;
        float cohesion_weight = 0.5f;
    };

    struct BoidPerception {
        float separation_radius = 30.0f;
        float alignment_radius = 50.0f;
        float cohesion_radius = 50.0f;

        float fov_cos = 0.0f;
    };
} // namespace simnet::ecs
