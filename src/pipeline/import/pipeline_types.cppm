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
        BitPacking = 1U << 8U, /// bit-packed record layout.
    };

    /// Result kind for an encode call.
    enum class EncodeResultKind : std::uint8_t
    {
        Update, /// a complete encoded update was produced.
        Skipped /// no encoded update was emitted by this call.
    };

    /// Active reason why an encode call emitted no update.
    enum class EncodeSkipReason : std::uint8_t
    {
        None,
        SendInterval,
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

    // --- Pipeline state structs ---

    /**
     * Immutable definition of enabled pipeline techniques and settings.
     *
     * Replication stages are ordered as cadence control, relevancy selection, update scheduling,
     * delta selection, representation encoding, record layout, whole-update compression, and
     * application packetization. Unsupported stages are rejected. Delivery remains outside the
     * pipeline.
     */
    struct PipelineDefinition
    {
        PipelineTechniqueFlags techniques{PipelineTechniqueFlags::None};
        SendIntervalSettings send_interval{};
        IncrementalSettings incremental{};
        QuantizationSettings quantization{};
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
    };

    /// Reusable encode scratch memory. Stored externally to avoid allocations on the hot path.
    struct PipelineScratch
    {
        /// Indices of entities to include in the encoded update.
        std::vector<std::uint32_t> selected_indices;
        /// IDs to delete (for delta)
        std::vector<EntityNetId> selected_delete_ids;
        /// Temporary buffer for encoding.
        std::vector<Byte> bytes;
    };
}
