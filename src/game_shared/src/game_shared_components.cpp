module;

#include <flecs.h>

module simnet.game_shared;

import :components;

namespace simnet
{
    void register_game_components(flecs::world& world)
    {
        world.component<NetIdentity>("simnet::NetIdentity");
        world.component<Position>("simnet::Position");
        world.component<Heading>("simnet::Heading");
        world.component<Hue>("simnet::Hue");
        world.component<BoidTag>("simnet::BoidTag");
    }
}
