#pragma once
#include <flecs.h>

namespace simnet::game::shared {
    void register_telemetry_systems(flecs::world &world, flecs::entity post_sim);
}
