#pragma once
#include <flecs.h>

namespace simnet::ecs {

    void init_systems(flecs::world& world);

    // Update
    void register_boid_steering_system(const flecs::world& world);

    // PostUpdate
    void register_boid_apply_velocity(const flecs::world& world);
    void register_boid_integrate_position(const flecs::world& world);

}
