module;

#include <flecs.h>

module simnet.game_client;

import :apply;
import simnet.game_shared;

namespace simnet
{
    void register_client_game(flecs::world& world)
    {
        register_game_components(world);
        world.component<ClientReplicationIndex>("simnet::ClientReplicationIndex");
        world.component<ClientReplicationClock>("simnet::ClientReplicationClock");
    }
}
