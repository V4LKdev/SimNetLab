#pragma once

#include <flecs.h>

namespace simnet::ecs {
    void neighbor_cache_system(flecs::iter &it);

    void alignment_system(flecs::iter &it);

    void cohesion_system(flecs::iter &it);

    void separation_system(flecs::iter &it);

    void apply_steering_system(flecs::iter &it);

    void integrate_position_system(flecs::iter &it);

    void clear_accumulator_system(flecs::iter &it);
} // namespace simnet::ecs
