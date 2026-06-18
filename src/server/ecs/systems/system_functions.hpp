#pragma once
#include <flecs.h>

namespace simnet::server::ecs {
    void spawn_boids_system(flecs::iter &it);

    void send_snapshot_system(flecs::iter &it);
}
