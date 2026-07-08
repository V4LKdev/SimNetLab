module;

#include <cstdint>

/// @brief Synthetic workload data contracts.
export module simnet.synthetic:types;

import simnet.core;

export namespace simnet
{
    /// Synthetic snapshot placement pattern.
    enum class SyntheticPattern : std::uint8_t
    {
        Grid,
        RandomUniform
    };

    /// Settings for deterministic synthetic snapshot generation.
    struct SyntheticSnapshotSettings
    {
        std::uint64_t seed { 12345 };
        std::uint32_t entity_count { 1000 };
        Aabb3f bounds { make_centered_bounds(400.0F) };
        SyntheticPattern pattern { SyntheticPattern::RandomUniform };
    };
}
