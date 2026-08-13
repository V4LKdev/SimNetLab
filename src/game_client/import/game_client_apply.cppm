module;

#include <flecs.h>

/// @brief Client-side snapshot patch application contract.
export module simnet.game_client:apply;

import :types;
import simnet.snapshot;

export namespace simnet
{
    /// Registers shared and client-side replication components with a Flecs world.
    void register_client_game(flecs::world& world);

    /// Applies a caller-owned patch whose snapshot contract has already been validated.
    /// `patch` must have passed validate_client_snapshot_patch at the decode boundary or be a
    /// FullReplace built from a successfully reconstructed WorldSnapshot. The caller must not
    /// mutate it between that proof and this call. Call register_client_game once during setup.
    /// This path rejects stale ticks and unsupported game classifications before changing Flecs
    /// state. Equal ticks are accepted.
    [[nodiscard]] ApplyPatchReport
    apply_client_snapshot_patch_unchecked(flecs::world& world, SnapshotUpdate const& patch);
}
