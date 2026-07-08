module;

#include <cstddef>
#include <cstdint>
#include <flecs.h>
#include <string>
#include <vector>

/// @brief Client-side replicated world state contracts.
export module simnet.game_client:types;

import simnet.core;
import simnet.game_shared;
import simnet.snapshot;

export namespace simnet
{
    /// Sorted lookup from replicated IDs to Flecs entities.
    struct ClientReplicationIndex
    {
        std::vector<EntityNetId> ids;
        std::vector<flecs::entity_t> entities;

        /// Returns the number of indexed replicated entities.
        [[nodiscard]] std::size_t size() const noexcept
        {
            return ids.size();
        }

        /// Returns true when no replicated entities are indexed.
        [[nodiscard]] bool empty() const noexcept
        {
            return ids.empty();
        }

        /// Reserves storage for the index arrays.
        void reserve(std::size_t count)
        {
            ids.reserve(count);
            entities.reserve(count);
        }

        /// Clears indexed state while preserving capacity.
        void clear() noexcept
        {
            ids.clear();
            entities.clear();
        }
    };

    /// Latest replicated client tick.
    struct ClientReplicationClock
    {
        Tick latest_tick {};
    };

    /// Raw facts reported by client patch application.
    struct ApplyPatchReport
    {
        Tick tick {};
        SnapshotKind kind { SnapshotKind::Patch };
        std::uint32_t previous_entities {};
        std::uint32_t final_entities {};
        std::uint32_t upsert_count {};
        std::uint32_t delete_count {};
        bool valid { true };
        std::string error {};
    };
}
