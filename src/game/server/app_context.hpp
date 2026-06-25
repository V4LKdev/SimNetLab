#pragma once
#include "core/core.hpp"
#include "game/shared/components.hpp"

namespace simnet::game::server {
    struct AppContext {
        net::NetManager *net = nullptr;

        /// Cached Queries
        flecs::query<const shared::Position, const shared::Heading,
            const shared::Hue, const shared::NetworkId> snapshot_query;

        flecs::query<const shared::NetworkId> boid_destroy_query;
    };
}
