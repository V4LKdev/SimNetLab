module;

#include <cstddef>
#include <string>
#include <utility>

/// @brief Complete snapshot reconstruction from logical client patches.
export module simnet.snapshot:reconstruct;

import :types;
import :validate;

export namespace simnet
{
    /// Advanced reconstruction path for caller-owned state with proven snapshot validity.
    ///
    /// `patch` must have passed validate_client_snapshot_patch at its producer or receive
    /// boundary. A Patch baseline must have passed validate_world_snapshot, be a locally
    /// default-constructed empty snapshot, or come from a successful reconstruction and remain
    /// under ownership that preserves the invariant. The caller must not mutate either value
    /// between that proof and this call. A null Patch baseline is rejected before `out_snapshot`
    /// changes. Reconstruction uses a temporary and commits only the complete result. Arbitrary or
    /// external values must use reconstruct_world_snapshot.
    [[nodiscard]] inline SnapshotValidationResult reconstruct_world_snapshot_unchecked(
        WorldSnapshot const* baseline,
        SnapshotUpdate const& patch,
        WorldSnapshot& out_snapshot
    )
    {
        if (patch.kind == SnapshotKind::Patch && baseline == nullptr)
        {
            return {false, "snapshot patch requires a baseline"};
        }
        auto reconstructed = WorldSnapshot{};
        auto const& source_baseline = baseline == nullptr ? reconstructed : *baseline;
        reconstructed.tick = patch.tick;
        reconstructed.reserve(
            patch.kind == SnapshotKind::FullReplace ? patch.upserts.size()
                                                    : source_baseline.size() + patch.upserts.size()
        );

        auto append = [&reconstructed](EntityState const& boid)
        {
            reconstructed.ids.push_back(boid.id);
            reconstructed.classifications.push_back(boid.classification);
            reconstructed.positions.push_back(boid.position);
            reconstructed.headings.push_back(boid.heading);
            reconstructed.hues.push_back(boid.hue);
        };

        if (patch.kind == SnapshotKind::FullReplace)
        {
            for (auto const& boid : patch.upserts)
            {
                append(boid);
            }
        }
        else
        {
            auto baseline_index = std::size_t{};
            auto upsert_index = std::size_t{};
            auto delete_index = std::size_t{};
            while (baseline_index < source_baseline.size() || upsert_index < patch.upserts.size())
            {
                auto const baseline_id = baseline_index < source_baseline.size()
                                             ? source_baseline.ids[baseline_index]
                                             : EntityNetId{};
                auto const have_upsert = upsert_index < patch.upserts.size();
                auto const upsert_id = have_upsert ? patch.upserts[upsert_index].id : EntityNetId{};

                if (have_upsert &&
                    (baseline_index == source_baseline.size() || upsert_id < baseline_id))
                {
                    append(patch.upserts[upsert_index++]);
                    continue;
                }

                while (delete_index < patch.deletes.size() &&
                       patch.deletes[delete_index] < baseline_id)
                {
                    ++delete_index;
                }
                auto const deleted = delete_index < patch.deletes.size() &&
                                     patch.deletes[delete_index] == baseline_id;
                if (deleted)
                {
                    ++delete_index;
                }
                else if (have_upsert && upsert_id == baseline_id)
                {
                    append(patch.upserts[upsert_index++]);
                }
                else
                {
                    append({
                        .id = baseline_id,
                        .classification = source_baseline.classifications[baseline_index],
                        .position = source_baseline.positions[baseline_index],
                        .heading = source_baseline.headings[baseline_index],
                        .hue = source_baseline.hues[baseline_index],
                    });
                }
                ++baseline_index;
            }
        }

        out_snapshot = std::move(reconstructed);
        return {};
    }

    /// Reconstructs a complete snapshot without mutating `out_snapshot` on failure.
    ///
    /// Full replacement upserts define the complete population and deletes must be empty.
    /// Patches are applied to the supplied baseline using explicit upserts and deletes.
    /// This is the normal entry point for arbitrary or external snapshot values.
    [[nodiscard]] inline SnapshotValidationResult reconstruct_world_snapshot(
        WorldSnapshot const* baseline,
        SnapshotUpdate const& patch,
        WorldSnapshot& out_snapshot
    )
    {
        auto patch_validation = validate_client_snapshot_patch(patch);
        if (!patch_validation.valid)
        {
            return patch_validation;
        }
        if (baseline != nullptr)
        {
            auto const baseline_validation = validate_world_snapshot(*baseline);
            if (!baseline_validation.valid)
            {
                return {
                    false,
                    "snapshot patch baseline is invalid: " + baseline_validation.message
                };
            }
        }
        return reconstruct_world_snapshot_unchecked(baseline, patch, out_snapshot);
    }
}
