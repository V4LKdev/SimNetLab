#pragma once
#include <flecs.h>

namespace simnet::game::shared {
    /// Registers all shared flocking simulation systems into the given world.
    void register_flocking_systems(
        flecs::world &world,
        flecs::entity prepare,
        flecs::entity compute,
        flecs::entity apply
    );
}
