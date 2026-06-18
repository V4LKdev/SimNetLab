#pragma once

#include <flecs.h>
#include <vector>
#include "../../core/math/vec3.hpp"

namespace simnet::game::shared {
    // --- Boid Components ---
    /// 3D position of a entity.
    struct Position {
        Vec3 value;
    };

    /// 3D velocity vector of an entity.
    struct Velocity {
        Vec3 value;
    };

    /// Normalized forward direction. Defaults to +X or the last non-zero velocity.
    struct Heading {
        Vec3 value = {1.0f, 0.0f, 0.0f};
    };

    /// Accumulated steering force for this tick.
    struct SteeringAccumulate {
        Vec3 value{};
    };

    /// Hue of an entity
    struct Hue {
        uint8_t value;
    };

    /// Frame persistent unique identifier of the entity.
    struct NetworkId {
        uint32_t value;
    };

    /// Tick-local per-entity index assigned during snapshot construction.
    struct BoidIdx {
        uint32_t index = 0;
    };

    // --- Tags ---
    /// Marking entities that participate in flocking.
    struct Boid {
    };

    // --- Singletons ---
    /// Singleton updated each frame with aggregate flock statistics.
    struct FlockStats {
        float avg_speed = 0.0f;
        float avg_steer = 0.0f;
        float avg_neighbors = 0.0f;
        float min_nbr_dist = 0.0f; // average distance to closest neighbor
        float polarisation = 0.0f; // 1 = perfectly aligned, 0 = random
        int boid_count = 0;
    };

    /// Singleton read-only neighbor cache for current tick.
    struct NeighborCache {
        std::vector<Vec3> positions;
        std::vector<Vec3> headings;
        std::vector<size_t> offsets;
        std::vector<uint32_t> entries;
    };

    /// Singleton for the ServerContext, not registered on clients
    struct ServerContext {
        server::network_server *server = nullptr;
    };

    inline void register_server_components(flecs::world &world)
    {
        world.component<Position>();
        world.component<Velocity>();
        world.component<Heading>();
        world.component<Hue>();
        world.component<SteeringAccumulate>();
        world.component<BoidIdx>();
        world.component<NetworkId>();

        world.component<Boid>();

        // Singletons
        world.component<FlockStats>().add(flecs::Singleton);
        world.component<NeighborCache>().add(flecs::Singleton);
        world.component<ServerContext>().add(flecs::Singleton);
    }

    inline void register_client_components(flecs::world &world)
    {
        world.component<Position>();
        world.component<Velocity>();
        world.component<Heading>();
        world.component<Hue>();
        world.component<SteeringAccumulate>();
        world.component<BoidIdx>();
        world.component<NetworkId>();

        world.component<Boid>();

        // Singletons
        world.component<FlockStats>().add(flecs::Singleton);
        world.component<FlockStats>().add(flecs::Singleton);
    }
}
