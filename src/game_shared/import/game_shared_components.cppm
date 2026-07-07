module;

#include <cstdint>
#include <flecs.h>

/// @brief Shared Flecs component contracts.
export module simnet.game_shared:components;

import simnet.core;

export namespace simnet
{
    /// Network identity attached to replicated entities.
    struct NetIdentity
    {
        EntityNetId id {};
    };

    /// World-space entity position.
    struct Position
    {
        Vec3f value {};
    };

    /// Normalized entity heading.
    struct Heading
    {
        Vec3f value {};
    };

    /// Compact display hue.
    struct Hue
    {
        std::uint8_t value {};
    };

    /// Marker for boid entities.
    struct BoidTag {};

    /// Registers shared components and tags with a Flecs world.
    void register_game_components(flecs::world& world);
}
