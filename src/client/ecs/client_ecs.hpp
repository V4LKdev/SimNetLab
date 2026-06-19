#pragma once
#include <flecs.h>

#include "../../core/net/old/wire_types.hpp"

namespace simnet::client::ecs {
    struct PendingSnapshot {
        net::ReplicationSnapshot data;
        bool new_data = true;
    };

    /// Setup client-side ECS
    void init_client_ecs(flecs::world &world);
}
