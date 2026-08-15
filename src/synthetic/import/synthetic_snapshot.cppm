module;

#include <cstdint>

/// @brief Synthetic snapshot generation API.
export module simnet.synthetic:snapshot;

import :types;
import simnet.core;
import simnet.snapshot;

export namespace simnet
{
    /// Complete mutable state for one deterministic synthetic snapshot sequence.
    struct SyntheticSnapshotState
    {
        WorldSnapshot current{};
        WorldSnapshot candidate{};
        SyntheticSnapshotSettings accepted_snapshot_settings{};
        SyntheticChangeSettings accepted_change_settings{};
        std::uint32_t next_entity_index{};
        Tick last_accepted_tick{};
        bool initialized{};
    };

    /// Initializes or advances one deterministic retained synthetic snapshot sequence.
    [[nodiscard]] WorldSnapshot const& update_synthetic_world_snapshot(
        SyntheticSnapshotSettings const& snapshot_settings,
        SyntheticChangeSettings const& change_settings,
        Tick tick,
        SyntheticSnapshotState& state
    );
}
