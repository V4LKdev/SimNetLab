module;

#include <cstddef>
#include <utility>

/// @brief Complete snapshot reconstruction from logical client patches.
export module simnet.snapshot:reconstruct;

import :types;
import :validate;

export namespace simnet
{
    /// Reconstructs a complete snapshot without mutating `out_snapshot` on failure.
    ///
    /// Full replacements require no baseline. Patches are applied to the supplied
    /// baseline using the strictly ascending snapshot and patch contracts.
    [[nodiscard]] inline SnapshotValidationResult reconstruct_world_snapshot(
        WorldSnapshot const* baseline,
        ClientSnapshotPatch const& patch,
        WorldSnapshot& out_snapshot
    )
    {
        auto const patch_validation = validate_client_snapshot_patch(patch);
        if (!patch_validation.valid) {
            return patch_validation;
        }
        if (patch.kind == SnapshotKind::Patch && baseline == nullptr) {
            return { false, "snapshot patch requires a baseline" };
        }
        if (baseline != nullptr) {
            auto const baseline_validation = validate_world_snapshot(*baseline);
            if (!baseline_validation.valid) {
                return { false, "snapshot patch baseline is invalid: " + baseline_validation.message };
            }
        }

        auto reconstructed = WorldSnapshot {};
        reconstructed.tick = patch.tick;
        reconstructed.reserve(
            patch.kind == SnapshotKind::FullReplace
                ? patch.upserts.size()
                : baseline->size() + patch.upserts.size()
        );

        auto append = [&reconstructed](BoidState const& boid) {
            reconstructed.ids.push_back(boid.id);
            reconstructed.positions.push_back(boid.position);
            reconstructed.headings.push_back(boid.heading);
            reconstructed.hues.push_back(boid.hue);
        };

        if (patch.kind == SnapshotKind::FullReplace) {
            for (auto const& boid : patch.upserts) {
                append(boid);
            }
        } else {
            auto baseline_index = std::size_t {};
            auto upsert_index = std::size_t {};
            auto delete_index = std::size_t {};
            while (baseline_index < baseline->size() || upsert_index < patch.upserts.size()) {
                auto const baseline_id = baseline_index < baseline->size()
                    ? baseline->ids[baseline_index]
                    : EntityNetId {};
                auto const have_upsert = upsert_index < patch.upserts.size();
                auto const upsert_id = have_upsert ? patch.upserts[upsert_index].id : EntityNetId {};

                if (have_upsert && (baseline_index == baseline->size() || upsert_id < baseline_id)) {
                    append(patch.upserts[upsert_index++]);
                    continue;
                }

                while (delete_index < patch.deletes.size()
                    && patch.deletes[delete_index] < baseline_id) {
                    ++delete_index;
                }
                auto const deleted = delete_index < patch.deletes.size()
                    && patch.deletes[delete_index] == baseline_id;
                if (deleted) {
                    ++delete_index;
                } else if (have_upsert && upsert_id == baseline_id) {
                    append(patch.upserts[upsert_index++]);
                } else {
                    append({
                        .id = baseline_id,
                        .position = baseline->positions[baseline_index],
                        .heading = baseline->headings[baseline_index],
                        .hue = baseline->hues[baseline_index],
                    });
                }
                ++baseline_index;
            }
        }

        auto const reconstructed_validation = validate_world_snapshot(reconstructed);
        if (!reconstructed_validation.valid) {
            return {
                false,
                "reconstructed world snapshot is invalid: " + reconstructed_validation.message
            };
        }
        out_snapshot = std::move(reconstructed);
        return {};
    }
}
