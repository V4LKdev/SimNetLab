#pragma once
#include <flecs.h>


namespace simnet::ecs {
    /// Initialize the Flecs world, and define custom simulation phases
    void init_simulation(flecs::world &world);

    /// Run one simulation
    void run_tick(flecs::world &world, float dt);
}
