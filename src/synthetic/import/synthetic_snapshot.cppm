/// @brief Synthetic snapshot generation API.
export module simnet.synthetic:snapshot;

import :types;
import simnet.core;
import simnet.snapshot;

export namespace simnet
{
    /// Creates a deterministic valid world snapshot for the requested tick.
    [[nodiscard]] WorldSnapshot make_synthetic_world_snapshot(
        SyntheticSnapshotSettings const& settings,
        Tick tick
    );
}
