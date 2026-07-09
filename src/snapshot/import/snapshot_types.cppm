module;

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// @brief Snapshot data types.
export module simnet.snapshot:types;

import simnet.core;

export namespace simnet
{
    /// Max deviation from unit length for heading normalisation check.
    inline constexpr float heading_normalization_tolerance = 0.01F;

    /// Per-entity replicated boid state.
    struct BoidState
    {
        /// Network identifier.
        EntityNetId id {};

        /// World-space position.
        Vec3f position {};

        /// Normalized facing direction.
        Vec3f heading {};

        /// Color hue (0 - 255)
        std::uint8_t hue {};
    };

    /// Authoritative full world state for one simulation tick.
    /// The SoA vectors are lock-step arrays keyed by strictly ascending ids.
    struct WorldSnapshot
    {
        /// Simulation tick for this snapshot.
        Tick tick {};

        /// Entity network identifiers (strictly ascending).
        std::vector<EntityNetId> ids;

        /// Positions, same index order as ids.
        std::vector<Vec3f> positions;

        /// Normalized facing directions, same index order as ids.
        std::vector<Vec3f> headings;

        /// Color hues, same index order as ids.
        std::vector<std::uint8_t> hues;

        /// Returns the number of entities in the snapshot.
        [[nodiscard]] std::size_t size() const noexcept
        {
            return ids.size();
        }

        /// Returns true when the snapshot has no entities.
        [[nodiscard]] bool empty() const noexcept
        {
            return ids.empty();
        }

        /// Reserves storage for all SoA vectors.
        void reserve(std::size_t count)
        {
            ids.reserve(count);
            positions.reserve(count);
            headings.reserve(count);
            hues.reserve(count);
        }

        /// Clears all snapshot data while preserving capacity.
        void clear() noexcept
        {
            tick = {};
            ids.clear();
            positions.clear();
            headings.clear();
            hues.clear();
        }
    };

    /// Logical snapshot patch kind.
    enum class SnapshotKind : std::uint8_t
    {
        FullReplace,
        Patch
    };

    /// Logical client-side changes for one simulation tick.
    struct ClientSnapshotPatch
    {
        /// Simulation tick for this patch.
        Tick tick {};

        /// How to apply the patch.
        SnapshotKind kind { SnapshotKind::Patch };

        /// Entities to insert or update (ids strictly ascending).
        std::vector<BoidState> upserts;

        /// Entities to delete (ids strictly ascending).
        std::vector<EntityNetId> deletes;

        /// Returns true when the patch contains no changes.
        [[nodiscard]] bool empty() const noexcept
        {
            return upserts.empty() && deletes.empty();
        }

        /// Reserves storage for upserts and deletes.
        void reserve(std::size_t upsert_count, std::size_t delete_count)
        {
            upserts.reserve(upsert_count);
            deletes.reserve(delete_count);
        }

        /// Clears patch data while preserving capacity and current patch kind.
        void clear() noexcept
        {
            tick = {};
            upserts.clear();
            deletes.clear();
        }
    };

    /// First-error snapshot validation result.
    struct SnapshotValidationResult
    {
        /// True when the snapshot is valid.
        bool valid { true };

        /// Error message, if any.
        std::string message {};
    };
}
