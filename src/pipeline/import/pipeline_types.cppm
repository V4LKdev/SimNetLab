module;

#include <cstddef>
#include <cstdint>
#include <vector>

/// @brief Pipeline public data contracts.
export module simnet.pipeline:types;

import simnet.core;
import simnet.snapshot;

export namespace simnet
{
    /// Replication technique flags. Combined with bitwise OR.
    enum class PipelineTechniqueFlags : std::uint32_t
    {
        None = 0,
        SendInterval = 1U << 0U, /// adjust snapshot cadence.
        Incremental = 1U << 1U, /// partial round-robin upserts.
        Quantization = 1U << 2U, /// position and heading quantization.
        OctHeading = 1U << 3U, /// octahedral heading quantization.
        Delta = 1U << 4U, /// baseline-relative patch selection.
        DeltaFieldMask = 1U << 5U, /// transmit only changed fields for existing Delta upserts.
        BitPacking = 1U << 8U, /// bit-packed record layout.
    };

    /// Result kind for an encode call.
    enum class EncodeResultKind : std::uint8_t
    {
        Update, /// a complete encoded update was produced.
        Skipped /// no encoded update was emitted by this call.
    };

    /// Server-side population selection mode applied before update scheduling.
    enum class AreaOfInterestMode : std::uint8_t
    {
        None,
        Radius,
        Fov,
    };

    /// Server-side temporal level-of-detail selection mode.
    enum class LevelOfDetailMode : std::uint8_t
    {
        None,
        DistanceBands,
    };

    /// Distance band assigned to one AOI-retained entity.
    enum class LevelOfDetailBand : std::uint8_t
    {
        Near,
        Medium,
        Far,
    };

    /// Combines pipeline technique flags.
    [[nodiscard]] constexpr PipelineTechniqueFlags
    operator|(PipelineTechniqueFlags lhs, PipelineTechniqueFlags rhs) noexcept
    {
        return static_cast<PipelineTechniqueFlags>(
            static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs)
        );
    }

    /// Intersects pipeline technique flags.
    [[nodiscard]] constexpr PipelineTechniqueFlags
    operator&(PipelineTechniqueFlags lhs, PipelineTechniqueFlags rhs) noexcept
    {
        return static_cast<PipelineTechniqueFlags>(
            static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs)
        );
    }

    /// Adds pipeline technique flags in place.
    constexpr PipelineTechniqueFlags&
    operator|=(PipelineTechniqueFlags& lhs, PipelineTechniqueFlags rhs) noexcept
    {
        lhs = lhs | rhs;
        return lhs;
    }

    /// Returns true when all requested flags are set.
    [[nodiscard]] constexpr bool
    has_all_flags(PipelineTechniqueFlags value, PipelineTechniqueFlags flag) noexcept
    {
        return (value & flag) == flag;
    }

    // --- Settings structs ---

    /// Configuration for the send-interval policy.
    struct SendIntervalSettings
    {
        /// Emit on authoritative ticks divisible by this positive interval.
        std::uint32_t interval_ticks{1};
    };

    /// Configuration for incremental round-robin selection.
    struct IncrementalSettings
    {
        std::uint32_t max_entities_per_update{512};
    };

    /// Configuration for position/heading quantization
    struct QuantizationSettings
    {
        Aabb3f position_bounds{make_centered_bounds(400.0F)};
    };

    /// Shared Server-side area-of-interest settings.
    struct AreaOfInterestSettings
    {
        AreaOfInterestMode mode{AreaOfInterestMode::None};
        float radius{};
        /// Full 3D cone angle in degrees when mode is Fov.
        float fov_degrees{};
    };

    /// Shared temporal distance-LOD settings applied after AOI filtering.
    struct LevelOfDetailSettings
    {
        LevelOfDetailMode mode{LevelOfDetailMode::None};
        float near_distance{};
        float medium_distance{};
        std::uint32_t medium_interval_ticks{};
        std::uint32_t far_interval_ticks{};
    };

    /// Role-independent authoritative pose used for one peer's AOI selection.
    struct InterestSource
    {
        Vec3f position{};
        Vec3f forward{.z = 1.0F};
        /// Nonzero only for a querying Player that must retain its own entity.
        EntityNetId source_entity_id{};
    };

    /// Population counts produced by the AOI stage.
    struct AreaOfInterestReport
    {
        bool source_available{true};
        std::uint32_t source_entity_count{};
        std::uint32_t candidate_count{};
        std::uint32_t retained_count{};
        std::uint32_t culled_count{};
    };

    /// Canonical complete-record Delta selection accounting.
    struct DeltaReport
    {
        std::uint32_t candidate_count{};
        std::uint32_t unchanged_count{};
        std::uint32_t changed_existing_count{};
        std::uint32_t spawned_count{};
        std::uint32_t produced_upsert_count{};
        std::uint32_t whole_record_existing_upsert_count{};
        std::uint32_t masked_existing_upsert_count{};
        std::uint32_t classification_inclusion_count{};
        std::uint32_t position_inclusion_count{};
        std::uint32_t heading_inclusion_count{};
        std::uint32_t hue_inclusion_count{};
        std::uint64_t complete_record_equivalent_bytes{};
        std::uint64_t actual_upsert_representation_bytes{};
    };

    /// Counts grouped by deterministic distance band.
    struct LevelOfDetailBandCounts
    {
        std::uint32_t near{};
        std::uint32_t medium{};
        std::uint32_t far{};
    };

    /// Per-update diagnostics produced by temporal distance LOD.
    struct LevelOfDetailReport
    {
        LevelOfDetailMode mode{LevelOfDetailMode::None};
        LevelOfDetailBandCounts population{};
        LevelOfDetailBandCounts eligible{};
        LevelOfDetailBandCounts serviced{};
        LevelOfDetailBandCounts represented{};
        LevelOfDetailBandCounts deferred{};
        std::uint32_t pending_due_count{};
        std::uint32_t transition_count{};
        std::uint32_t forced_immediate_count{};
        std::uint32_t recovery_forced_count{};
        std::uint32_t deletions_bypassing_count{};
        std::uint32_t full_replace_override_count{};
        std::uint32_t encoded_bytes{};
    };

    /// Per-entity temporal LOD authority retained for one peer.
    struct LevelOfDetailScheduleEntry
    {
        EntityNetId id{};
        LevelOfDetailBand band{LevelOfDetailBand::Near};
        Tick next_due_tick{};
        bool pending_due{};
    };

    // --- Pipeline state structs ---

    /**
     * Immutable definition of enabled pipeline techniques and settings.
     *
     * Pipeline stages are cadence control, relevancy selection, update scheduling, delta
     * selection, representation encoding, and record layout. Compression and opaque byte-group
     * packetization compose after this pipeline. Delivery remains outside the pipeline.
     */
    struct PipelineDefinition
    {
        PipelineTechniqueFlags techniques{PipelineTechniqueFlags::None};
        SendIntervalSettings send_interval{};
        IncrementalSettings incremental{};
        QuantizationSettings quantization{};
        AreaOfInterestSettings area_of_interest{};
        LevelOfDetailSettings level_of_detail{};
    };

    /// Caller-owned per-client replication state.
    struct ClientReplicationState
    {
        /// Next outbound sequence id.
        SequenceId next_sequence{1};
        /// Latest sequence received from remote.
        SequenceId latest_remote_sequence{};
        /// Next incremental selection cursor for round-robin selection.
        std::uint32_t incremental_cursor{};
        /// True after the first complete non-Delta Incremental population was emitted.
        bool incremental_seeded{};
        /// True after a complete LOD population was emitted.
        bool level_of_detail_seeded{};
        /// Sorted temporal LOD authority for the current AOI-retained population.
        std::vector<LevelOfDetailScheduleEntry> level_of_detail_schedule;
    };

    /// Reusable encode scratch memory. Stored externally to avoid allocations on the hot path.
    struct PipelineScratch
    {
        /// Indices of entities to include in the encoded update.
        std::vector<std::uint32_t> selected_indices;
        /// IDs to delete (for delta)
        std::vector<EntityNetId> selected_delete_ids;
        /// Source indices retained by AOI before update scheduling.
        std::vector<std::uint32_t> relevant_source_indices;
        /// Per-peer AOI population used as scheduling input.
        WorldSnapshot relevant_snapshot;
        /// Candidate LOD schedule storage used during deterministic population reconciliation.
        std::vector<LevelOfDetailScheduleEntry> level_of_detail_schedule;
        /// Canonical logical update represented by the encoded bytes.
        SnapshotUpdate logical_update;
        /// Prepared complete-record bytes retained between Delta comparison and final writing.
        std::vector<Byte> prepared_record_bytes;
        /// Temporary buffer for encoding.
        std::vector<Byte> bytes;
    };
}
