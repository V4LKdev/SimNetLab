module;

#include <flecs.h>

module simnet.game_shared;

namespace simnet
{
    void register_game_components(flecs::world& world)
    {
        world.component<EntityKindComponent>("simnet::EntityKindComponent");
        world.component<NetIdentity>("simnet::NetIdentity");
        world.component<Position>("simnet::Position");
        world.component<Heading>("simnet::Heading");
        world.component<Hue>("simnet::Hue");
    }
}
