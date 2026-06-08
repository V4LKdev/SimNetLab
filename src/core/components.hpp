#pragma once
#include <cstdint>
#include <vector>

namespace simnet::ecs {

    struct Position3D {
         float x, y, z;
    };

    struct Velocity3D {
        float x, y, z;
    };

    struct DesiredVelocity3D {
        float x, y, z;
    };

    struct BoidConfig {
        float max_speed;
        float max_force;
        float mass;
        float separation_weight;
        float alignment_weight;
        float cohesion_weight;
        float drag_coef;
    };


    struct BoidPerception {
        float separation_radius;
        float cohesion_radius;
        float alignment_radius;
        float fov_cos;
    };

    struct Boid {
        uint32_t scratch_index;
    };

    struct BoidScratchpad {
        uint32_t count;

        std::vector<Position3D> positions;
        std::vector<Velocity3D> velocities;
    };

    struct BoidFeatures {
        bool use_scratchpad = true;

        bool separation = true;
        bool alignment = true;
        bool cohesion = true;

        bool use_drag = true;
    };

}
