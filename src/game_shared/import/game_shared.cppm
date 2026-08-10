module;

#include <cstdint>
#include <flecs.h>
#include <optional>

/// @brief Shared Flecs component contracts.
export module simnet.game_shared;

import simnet.core;
import simnet.snapshot;

export namespace simnet
{
    /// Stored semantic kind for replicated game entities.
    enum class EntityKind : std::uint8_t
    {
        Boid,
        Player
    };

    inline constexpr EntityClassification boid_entity_classification{1U};
    inline constexpr EntityClassification player_entity_classification{2U};

    /// Maps a recognized game entity kind to its generic replicated classification.
    [[nodiscard]] constexpr std::optional<EntityClassification>
    classification_from_entity_kind(EntityKind kind) noexcept
    {
        switch (kind)
        {
            case EntityKind::Boid:
                return boid_entity_classification;
            case EntityKind::Player:
                return player_entity_classification;
        }
        return std::nullopt;
    }

    /// Maps a supported replicated classification to its game entity kind.
    [[nodiscard]] constexpr std::optional<EntityKind>
    entity_kind_from_classification(EntityClassification classification) noexcept
    {
        if (classification == boid_entity_classification)
        {
            return EntityKind::Boid;
        }
        if (classification == player_entity_classification)
        {
            return EntityKind::Player;
        }
        return std::nullopt;
    }

    struct EntityKindComponent
    {
        EntityKind value{EntityKind::Boid};
    };

    /// Network identity attached to replicated entities.
    struct NetIdentity
    {
        EntityNetId id{};
    };

    /// World-space entity position.
    struct Position
    {
        Vec3f value{};
    };

    /// Normalized entity heading.
    struct Heading
    {
        Vec3f value{};
    };

    /// Compact display hue.
    struct Hue
    {
        std::uint8_t value{};
    };

    /// Registers shared components with a Flecs world.
    void register_game_components(flecs::world& world);
}
