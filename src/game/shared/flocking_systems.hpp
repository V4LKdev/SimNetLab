#pragma once
#include <flecs.h>

namespace simnet::ecs {
    /// Registers all shared flocking simulation systems into the given world.
    void register_flocking_systems(
        flecs::world &world,
        flecs::entity sim_prepare,
        flecs::entity sim_compute,
        flecs::entity sim_apply
    );
}
