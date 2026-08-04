module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

/// @brief Presentation interpolation between validated world snapshots.
export module simnet.snapshot:interpolate;

import :types;
import :validate;
import simnet.core;

export namespace simnet
{
    /// Advanced interpolation path for caller-owned snapshots with proven validity.
    ///
    /// `previous` and `current` must have passed validate_world_snapshot at their producer boundary
    /// or come from successful reconstruction or extraction and remain under ownership that
    /// preserves the invariant. The caller must not mutate either value between that proof and this
    /// call. Non-finite alpha is rejected before `output` changes. Inputs and output must be
    /// distinct objects. Arbitrary or external values must use interpolate_world_snapshots.
    [[nodiscard]] inline SnapshotValidationResult interpolate_world_snapshots_unchecked(
        WorldSnapshot const& previous,
        WorldSnapshot const& current,
        double alpha,
        WorldSnapshot& output
    )
    {
        if (!std::isfinite(alpha)) {
            return {false, "snapshot interpolation alpha must be finite"};
        }

        auto const blend = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
        output.tick = current.tick;
        output.ids.resize(current.size());
        output.classifications.resize(current.size());
        output.positions.resize(current.size());
        output.headings.resize(current.size());
        output.hues.resize(current.size());

        auto previous_index = std::size_t{};
        for (auto current_index = std::size_t{}; current_index < current.size(); ++current_index) {
            auto const id = current.ids[current_index];
            while (previous_index < previous.size() && previous.ids[previous_index] < id) {
                ++previous_index;
            }

            output.ids[current_index] = id;
            output.classifications[current_index] = current.classifications[current_index];
            if (previous_index >= previous.size() || previous.ids[previous_index] != id) {
                output.positions[current_index] = current.positions[current_index];
                output.headings[current_index] = current.headings[current_index];
                output.hues[current_index] = current.hues[current_index];
                continue;
            }

            auto const inverse = 1.0F - blend;
            output.positions[current_index] = previous.positions[previous_index] * inverse
                + current.positions[current_index] * blend;
            output.headings[current_index] = normalize_or(
                previous.headings[previous_index] * inverse
                    + current.headings[current_index] * blend,
                current.headings[current_index]
            );

            auto const from = static_cast<std::int32_t>(previous.hues[previous_index]);
            auto const to = static_cast<std::int32_t>(current.hues[current_index]);
            auto const circular_delta = ((to - from + 384) % 256) - 128;
            auto interpolated
                = static_cast<std::int32_t>(std::lround(
                      static_cast<float>(from) + static_cast<float>(circular_delta) * blend
                  ))
                % 256;
            if (interpolated < 0) {
                interpolated += 256;
            }
            output.hues[current_index] = static_cast<std::uint8_t>(interpolated);
        }
        return {};
    }

    /// Builds a presentation snapshot using the current snapshot's entity set.
    ///
    /// Matching entities interpolate position, normalized heading, and circular hue.
    /// Classification is categorical and always uses the current authoritative endpoint.
    /// New entities use their current state. Removed entities are absent.
    /// Inputs and output must be distinct objects. This is the normal entry point for arbitrary or
    /// external snapshot values.
    [[nodiscard]] inline SnapshotValidationResult interpolate_world_snapshots(
        WorldSnapshot const& previous,
        WorldSnapshot const& current,
        double alpha,
        WorldSnapshot& output
    )
    {
        if (!std::isfinite(alpha)) {
            return {false, "snapshot interpolation alpha must be finite"};
        }
        auto const previous_validation = validate_world_snapshot(previous);
        if (!previous_validation.valid) {
            return {false, "invalid previous snapshot: " + previous_validation.message};
        }
        auto const current_validation = validate_world_snapshot(current);
        if (!current_validation.valid) {
            return {false, "invalid current snapshot: " + current_validation.message};
        }

        return interpolate_world_snapshots_unchecked(previous, current, alpha, output);
    }
}
