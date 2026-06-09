#pragma once
#include <flecs.h>
#include "components.hpp"
#include "config.hpp"

namespace simnet::ecs {

    inline void init_singletons(flecs::world& world)
    {
        // Register component types
        world.component<BoidConfig>();
        world.component<BoidPerception>();
        world.component<NeighbourList>();
        world.component<BoidScratchpadSoA>();

        // Default boid configuration
        BoidConfig cfg{};
        cfg.max_speed        = 150.0f;
        cfg.max_force        = 8.0f;
        cfg.mass             = 1.0f;
        cfg.separation_weight = 2.5f;
        cfg.alignment_weight  = 1.2f;
        cfg.cohesion_weight   = 0.5f;
        world.set<BoidConfig>(cfg);

        // Default perception radii and FOV
        BoidPerception per{};
        per.separation_radius = 25.0f;
        per.cohesion_radius   = 50.0f;
        per.alignment_radius  = 40.0f;
        per.fov_cos           = -1.0f;
        world.set<BoidPerception>(per);

        // Shared neighbour list (reused each frame)
        NeighbourList neighbours{};
        neighbours.neighbours.reserve(config::MAX_BOIDS);
        world.set<NeighbourList>(neighbours);

        // SoA scratchpad
        BoidScratchpadSoA soa{};
        soa.pos_x.reserve(config::MAX_BOIDS);
        soa.pos_y.reserve(config::MAX_BOIDS);
        soa.pos_z.reserve(config::MAX_BOIDS);
        soa.vel_x.reserve(config::MAX_BOIDS);
        soa.vel_y.reserve(config::MAX_BOIDS);
        soa.vel_z.reserve(config::MAX_BOIDS);
        soa.config_sep_weight.reserve(config::MAX_BOIDS);
        soa.config_ali_weight.reserve(config::MAX_BOIDS);
        soa.config_coh_weight.reserve(config::MAX_BOIDS);
        soa.perception_sep_radius_sq.reserve(config::MAX_BOIDS);
        soa.perception_ali_radius_sq.reserve(config::MAX_BOIDS);
        soa.perception_coh_radius_sq.reserve(config::MAX_BOIDS);
        soa.perception_fov_cos.reserve(config::MAX_BOIDS);
        soa.boid_indices.reserve(config::MAX_BOIDS);
        world.set<BoidScratchpadSoA>(soa);
    }

}
