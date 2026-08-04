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
    /// Max deviation from unit length for heading normalization check.
    /// Must exceed octahedral dequantization error.
    inline constexpr float heading_normalization_tolerance = 0.01F;

    /// Opaque replicated entity classification. Zero is reserved as invalid.
    class EntityClassification
    {
    public:
        constexpr EntityClassification() noexcept = default;

        explicit constexpr EntityClassification(std::uint8_t value) noexcept
            : value_(value)
        {
        }

        [[nodiscard]] constexpr std::uint8_t value() const noexcept
        {
            return value_;
        }

        [[nodiscard]] constexpr bool operator==(EntityClassification const& other) const noexcept
        {
            return value_ == other.value_;
        }

    private:
        std::uint8_t value_{};
    };

    static_assert(sizeof(EntityClassification) == 1U);

    /// Per-entity replicated state.
    struct EntityState
    {
        /// Nonzero network identifier. Zero is reserved as invalid.
        EntityNetId id{};

        /// Nonzero opaque entity classification.
        EntityClassification classification{};

        /// World-space position.
        Vec3f position{};

        /// Normalized facing direction.
        Vec3f heading{};

        /// Color hue (0 - 255)
        std::uint8_t hue{};
    };

    /// Authoritative full world state for one simulation tick.
    /// The SoA vectors are lock-step arrays keyed by strictly ascending ids.
    struct WorldSnapshot
    {
        /// Simulation tick for this snapshot.
        Tick tick{};

        /// Entity network identifiers (nonzero and strictly ascending).
        std::vector<EntityNetId> ids;

        /// Opaque entity classifications, same index order as ids.
        std::vector<EntityClassification> classifications;

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
            classifications.reserve(count);
            positions.reserve(count);
            headings.reserve(count);
            hues.reserve(count);
        }

        /// Clears all snapshot data while preserving capacity.
        void clear() noexcept
        {
            tick = {};
            ids.clear();
            classifications.clear();
            positions.clear();
            headings.clear();
            hues.clear();
        }
    };

    /// Logical snapshot update kind.
    enum class SnapshotKind : std::uint8_t
    {
        /// Upserts define the complete resulting entity population. Deletes must be empty.
        FullReplace,

        /// Upserts and deletes modify an existing complete population.
        Patch
    };

    /// Generic full or partial state update for one simulation tick.
    struct SnapshotUpdate
    {
        /// Simulation tick for this update.
        Tick tick{};

        /// How to apply the update.
        SnapshotKind kind{SnapshotKind::Patch};

        /// Entities to insert or update (ids nonzero and strictly ascending).
        std::vector<EntityState> upserts;

        /// Patch entities to delete (ids nonzero and strictly ascending).
        /// FullReplace updates must leave this empty.
        std::vector<EntityNetId> deletes;

        /// Returns true when the update carries no upserts or deletes.
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

        /// Clears update data while preserving capacity and current update kind.
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
        bool valid{true};

        /// Error message, if any.
        std::string message{};
    };
}
