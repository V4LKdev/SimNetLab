#pragma once

namespace simnet::ecs {

    struct Position3D {
         float x, y, z;
    };

    struct Velocity3D {
        float x, y, z;
    };

    struct Acceleration3D {
        float x, y, z;
    };

    struct BoidConfig {
        float max_speed;
        float max_force;
        float perception_radius;
        float separation_radius;
        float separation_weight;
        float alignment_weight;
        float cohesion_weight;
        float boundary_margin;
    };

    // Tag for the renderer
    struct Renderable {};

}
