#pragma once
#include <flecs.h>

namespace simnet::ecs {
    void register_telemetry_systems(flecs::world &world, flecs::entity post_sim);
}
