module;

#include <cstdint>
#include <flecs.h>

/// @brief Shared Flecs component contracts.
export module simnet.game_shared:components;

import simnet.core;

export namespace simnet
{
    /// Stored semantic kind for replicated game entities.
    enum class EntityKind : std::uint8_t
    {
        Boid,
        Player
    };

    struct EntityKindComponent
    {
        EntityKind value { EntityKind::Boid };
    };

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

    /// Registers shared components with a Flecs world.
    void register_game_components(flecs::world& world);
}
