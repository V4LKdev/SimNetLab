module;

#include <cstdint>
#include <flecs.h>
#include <string>

/// @brief Authoritative Flecs world snapshot extraction.
export module simnet.game_server:snapshot;

import simnet.core;
import simnet.game_shared;
import simnet.snapshot;

export namespace simnet
{
    /// Raw facts reported by authoritative world snapshot extraction.
    struct ServerSnapshotExtractionReport
    {
        Tick tick {};
        std::uint32_t entity_count {};
        bool valid { true };
        std::string error {};
    };

    /// Registers shared authoritative game components with a Flecs world.
    void register_server_game(flecs::world& world);

    /// Creates or updates one authoritative boid entity by EntityNetId.
    [[nodiscard]] flecs::entity upsert_authoritative_boid(
        flecs::world& world,
        BoidState const& boid
    );

    /// Deletes one authoritative boid entity by EntityNetId.
    [[nodiscard]] bool delete_authoritative_boid(
        flecs::world& world,
        EntityNetId id
    );

    /// Extracts a sorted validated WorldSnapshot without mutating the Flecs world.
    /// On failure, out_snapshot is cleared and its tick is set to the requested tick.
    [[nodiscard]] ServerSnapshotExtractionReport extract_world_snapshot(
        flecs::world const& world,
        Tick tick,
        WorldSnapshot& out_snapshot
    );
}
