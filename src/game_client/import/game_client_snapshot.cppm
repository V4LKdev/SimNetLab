module;

#include <cstdint>
#include <flecs.h>
#include <string>

/// @brief Replicated Flecs world snapshot extraction.
export module simnet.game_client:snapshot;

import simnet.core;
import simnet.snapshot;

export namespace simnet
{
    /// Raw facts reported by replicated world snapshot extraction.
    struct ClientSnapshotExtractionReport
    {
        Tick tick{};
        std::uint32_t entity_count{};
        bool valid{true};
        std::string error{};
    };

    /// Extracts a sorted validated WorldSnapshot without mutating the Flecs world.
    /// On failure, out_snapshot is cleared and its tick is set to the requested tick.
    [[nodiscard]] ClientSnapshotExtractionReport extract_client_world_snapshot(
        flecs::world const& world,
        Tick tick,
        WorldSnapshot& out_snapshot
    );
}
