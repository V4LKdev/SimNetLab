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

    /// Existing canonical field groups changed for a serviced synthetic entity.
    enum class SyntheticFieldChangeMode : std::uint8_t
    {
        All,
        Transform,
        PositionOnly,
        HeadingOnly
    };

    /// Settings for deterministic synthetic snapshot generation.
    struct SyntheticSnapshotSettings
    {
        std::uint64_t seed{12345};
        std::uint32_t entity_count{1000};
        Aabb3f bounds{make_centered_bounds(400.0F)};
        SyntheticPattern pattern{SyntheticPattern::RandomUniform};
    };

    /// Deterministic synthetic change scheduling policy.
    struct SyntheticChangeSettings
    {
        double entity_change_fraction{1.0};
        SyntheticFieldChangeMode field_change_mode{SyntheticFieldChangeMode::All};
    };
}
