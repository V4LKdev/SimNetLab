module;

#include <flecs.h>

/// @brief Client-side snapshot patch application contract.
export module simnet.game_client:apply;

import :types;
import simnet.game_shared;
import simnet.snapshot;

export namespace simnet
{
    /// Registers shared and client-side replication components with a Flecs world.
    void register_client_game(flecs::world& world);

    /// Applies a decoded snapshot patch to a client-side Flecs world.
    /// Call register_client_game once during setup; older ticks reject, equal ticks are accepted.
    [[nodiscard]] ApplyPatchReport apply_client_snapshot_patch(
        flecs::world& world,
        ClientSnapshotPatch const& patch
    );
}
