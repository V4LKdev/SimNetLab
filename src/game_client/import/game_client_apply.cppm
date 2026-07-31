module;

#include <cstddef>
#include <flecs.h>
#include <optional>

/// @brief Client-side snapshot patch application contract.
export module simnet.game_client:apply;

import :types;
import simnet.game_shared;
import simnet.snapshot;

export namespace simnet
{
    /// Registers shared and client-side replication components with a Flecs world.
    void register_client_game(flecs::world& world);

    /// Returns the number of currently replicated entities without scanning the world.
    [[nodiscard]] std::size_t client_replicated_entity_count(flecs::world const& world) noexcept;

    /// Returns the latest tick accepted by the client replication state.
    [[nodiscard]] Tick client_latest_replicated_tick(flecs::world const& world) noexcept;

    /// Assigns the local Client's Server-owned player ID. Zero clears the assignment.
    void set_client_player_entity_id(flecs::world& world, EntityNetId id);

    /// Returns the stored kind for a replicated entity, if currently present.
    [[nodiscard]] std::optional<EntityKind> client_entity_kind(
        flecs::world const& world,
        EntityNetId id
    ) noexcept;

    /// Applies a decoded snapshot patch to a client-side Flecs world.
    /// Call register_client_game once during setup. Older ticks reject, equal ticks are accepted.
    [[nodiscard]] ApplyPatchReport apply_client_snapshot_patch(
        flecs::world& world,
        SnapshotUpdate const& patch
    );
}
