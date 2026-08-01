module;

#include <cstdint>
#include <string>

/// @brief Client-side replicated world state contracts.
export module simnet.game_client:types;

import simnet.core;
import simnet.snapshot;

export namespace simnet
{
    /// Raw facts reported by client patch application.
    struct ApplyPatchReport
    {
        Tick tick{};
        SnapshotKind kind{SnapshotKind::Patch};
        std::uint32_t previous_entities{};
        std::uint32_t final_entities{};
        std::uint32_t upsert_count{};
        std::uint32_t delete_count{};
        bool valid{true};
        std::string error{};
    };
}
