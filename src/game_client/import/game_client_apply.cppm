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
    [[nodiscard]] std::optional<EntityKind>
    client_entity_kind(flecs::world const& world, EntityNetId id) noexcept;

    /// Applies an arbitrary snapshot patch to a client-side Flecs world after validating it.
    /// Call register_client_game once during setup. Older ticks reject, equal ticks are accepted.
    /// Malformed or external updates must use this checked entry point.
    [[nodiscard]] ApplyPatchReport
    apply_client_snapshot_patch(flecs::world& world, SnapshotUpdate const& patch);

    /// Advanced application path for a caller-owned patch with proven validity.
    ///
    /// `patch` must have passed validate_client_snapshot_patch at the decode boundary or be a
    /// FullReplace built from a successfully reconstructed WorldSnapshot. The caller must not
    /// mutate it between that proof and this call. Call register_client_game once during setup.
    /// This path rejects stale ticks before changing Flecs state. Equal ticks are accepted.
    /// Arbitrary or external updates must use apply_client_snapshot_patch.
    [[nodiscard]] ApplyPatchReport
    apply_client_snapshot_patch_unchecked(flecs::world& world, SnapshotUpdate const& patch);
}
