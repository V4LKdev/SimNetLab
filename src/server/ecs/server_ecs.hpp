#pragma once
#include <flecs.h>

namespace simnet::server::ecs {
    /// Set up server-side ECS: creates phases and registers etc.
    void init_server_ecs(flecs::world &world);
}
