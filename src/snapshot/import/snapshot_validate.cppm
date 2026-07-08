module;

#include <cmath>
#include <cstddef>

/// @brief Snapshot contract validation.
export module simnet.snapshot:validate;

import :types;
import simnet.core;

export namespace simnet
{
    /// Returns true when the heading length is within tolerance.
    [[nodiscard]] inline bool is_normalized_heading(Vec3f heading) noexcept
    {
        return std::abs(length(heading) - 1.0F) <= heading_normalization_tolerance;
    }

    /// Validates the full world snapshot contract.
    [[nodiscard]] inline SnapshotValidationResult validate_world_snapshot(WorldSnapshot const& snapshot)
    {
        auto const count = snapshot.ids.size();
        if (snapshot.positions.size() != count) {
            return { false, "world snapshot positions size does not match ids size" };
        }
        if (snapshot.headings.size() != count) {
            return { false, "world snapshot headings size does not match ids size" };
        }
        if (snapshot.hues.size() != count) {
            return { false, "world snapshot hues size does not match ids size" };
        }

        for (std::size_t index = 1; index < count; ++index) {
            if (snapshot.ids[index - 1] >= snapshot.ids[index]) {
                return { false, "world snapshot ids must be strictly ascending" };
            }
        }

        for (std::size_t index = 0; index < count; ++index) {
            if (!is_finite(snapshot.positions[index])) {
                return { false, "world snapshot position contains a non-finite component" };
            }
            if (!is_finite(snapshot.headings[index])) {
                return { false, "world snapshot heading contains a non-finite component" };
            }
            if (!is_normalized_heading(snapshot.headings[index])) {
                return { false, "world snapshot heading is not normalized" };
            }
        }

        return {};
    }

    /// Validates the logical client patch contract.
    [[nodiscard]] inline SnapshotValidationResult validate_client_snapshot_patch(ClientSnapshotPatch const& patch)
    {
        if (patch.kind != SnapshotKind::FullReplace && patch.kind != SnapshotKind::Patch) {
            return { false, "client snapshot patch kind is unknown" };
        }

        for (std::size_t index = 1; index < patch.upserts.size(); ++index) {
            if (patch.upserts[index - 1].id >= patch.upserts[index].id) {
                return { false, "client snapshot patch upserts must be strictly ascending" };
            }
        }

        for (std::size_t index = 1; index < patch.deletes.size(); ++index) {
            if (patch.deletes[index - 1] >= patch.deletes[index]) {
                return { false, "client snapshot patch deletes must be strictly ascending" };
            }
        }

        auto delete_index = std::size_t {};
        for (auto const& boid : patch.upserts) {
            if (!is_finite(boid.position)) {
                return { false, "client snapshot patch upsert position contains a non-finite component" };
            }
            if (!is_finite(boid.heading)) {
                return { false, "client snapshot patch upsert heading contains a non-finite component" };
            }
            if (!is_normalized_heading(boid.heading)) {
                return { false, "client snapshot patch upsert heading is not normalized" };
            }

            while (delete_index < patch.deletes.size() && patch.deletes[delete_index] < boid.id) {
                ++delete_index;
            }
            if (delete_index < patch.deletes.size() && patch.deletes[delete_index] == boid.id) {
                return { false, "client snapshot patch id appears in both upserts and deletes" };
            }
        }

        return {};
    }
}
