#include "world_client.hpp"

#include "ecs/components.hpp"
#include "config/SimConfig.hpp"

namespace simnet::client {
    ClientWorld::ClientWorld(const SimConfig &cfg)
    {
        world_.component<ecs::Position>();
        world_.component<ecs::Heading>();
        world_.component<ecs::Hue>();
        world_.component<ecs::Boid>();

        world_.set<SimConfig>(cfg);
    }

    ClientWorld::~ClientWorld()
    {
        world_.set_threads(0);
        world_.quit();
    }

    void ClientWorld::update()
    {
    }
}
