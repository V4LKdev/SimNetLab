#pragma once
#include "components.hpp"
#include "config.hpp"
#include <flecs.h>

namespace simnet::ecs {

    inline void init_singletons(flecs::world& world)
    {
        world.component<BoidConfig>();
        world.component<BoidPerception>();
        world.component<BoidScratchpad>();
        world.component<BoidFeatures>();


        BoidConfig cfg{};
        cfg.max_speed = 150.0f;
        cfg.max_force = 8.0f;
        cfg.mass = 1.0f;
        cfg.separation_weight = 2.5f;
        cfg.alignment_weight = 1.2f;
        cfg.cohesion_weight = 0.5f;
        cfg.drag_coef = 0.2f;
        world.set<BoidConfig>(cfg);


        BoidPerception per{};
        per.separation_radius = 15.0f;
        per.cohesion_radius = 50.0f;
        per.alignment_radius = 40.0f;
        per.fov_cos = 0.707f;
        world.set<BoidPerception>(per);


        BoidFeatures feat{};
        feat.use_scratchpad = true;
        feat.separation = true;
        feat.cohesion = true;
        feat.alignment = true;
        feat.use_drag = false;
        world.set<BoidFeatures>(feat);


        BoidScratchpad scratch{};
        scratch.count = 0;
        scratch.positions.resize(config::MAX_BOIDS);
        scratch.velocities.resize(config::MAX_BOIDS);
        world.set<BoidScratchpad>(scratch);
    }

}
