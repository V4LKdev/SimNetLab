module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

/// @brief Pipeline public data contracts.
export module simnet.pipeline:types;

import simnet.core;
import simnet.snapshot;

export namespace simnet
{
    // --- Profiles and codecs ---

    /// Built pipeline profiles, as pre-configured combinations of techniques.
    enum class PipelineProfileKind : std::uint8_t
    {
        RawSnapshot
    };

    /// Packet codec family used by a pipeline profile.
    enum class CodecKind : std::uint8_t
    {
        ByteAligned /// field-by-field byte streams.
    };

    // --- Technique flags ---

    /// Replication technique flags. Combined with bitwise OR.
    enum class PipelineTechniqueFlags : std::uint32_t
    {
        None = 0,
        SendInterval = 1U << 0U,    /// adjust snapshot cadence.
        Incremental = 1U << 1U,     /// partial round-robin upserts.
        Quantization = 1U << 2U,    /// position and heading quantisation.
        OctHeading = 1U << 3U,      /// octahedral heading quantization.
        Delta = 1U << 4U,           /// XOR delta from baseline.
        Aoi = 1U << 5U,             /// area-of-interest filtering. (Reserved)
        Lod = 1U << 6U,             /// level-of-detail priorities. (Reserved)
        Compression = 1U << 7U,     /// full-payload compression. (Reserved)
        BitPacking = 1U << 8U,      /// bit-packed record layout.

        // Future flags:
        // DeadReckoning // error-based correction sends
        // DirtyFlags // send only changed entities (or assumption based from a straight linear motion)
        // LeaderFollower // deterministic follower groups (major simulation work)
    };

    /// Result kind for an encode call.
    enum class EncodeResultKind : std::uint8_t
    {
        Packet,     /// a complete packed was produced.
        Skipped     /// no packed emitted this call.
    };

    /// Reason an encode call produced no packet.
    enum class EncodeSkipReason : std::uint8_t
    {
        None,
        SendInterval
        // Future reasons:
        // Aoi, Lod, etc.
    };

    /// Combines pipeline technique flags.
    [[nodiscard]] constexpr PipelineTechniqueFlags operator|(
        PipelineTechniqueFlags lhs,
        PipelineTechniqueFlags rhs
    ) noexcept
    {
        return static_cast<PipelineTechniqueFlags>(
            static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs)
        );
    }

    /// Intersects pipeline technique flags.
    [[nodiscard]] constexpr PipelineTechniqueFlags operator&(
        PipelineTechniqueFlags lhs,
        PipelineTechniqueFlags rhs
    ) noexcept
    {
        return static_cast<PipelineTechniqueFlags>(
            static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs)
        );
    }

    /// Adds pipeline technique flags in place.
    constexpr PipelineTechniqueFlags& operator|=(
        PipelineTechniqueFlags& lhs,
        PipelineTechniqueFlags rhs
    ) noexcept
    {
        lhs = lhs | rhs;
        return lhs;
    }

    /// Returns true when all requested flags are set.
    [[nodiscard]] constexpr bool has_all_flags(
        PipelineTechniqueFlags value,
        PipelineTechniqueFlags flag
    ) noexcept
    {
        return (value & flag) == flag;
    }

    // --- Packet metadata ---

    /// Encoded pipeline packet kind.
    enum class PipelinePacketKind : std::uint8_t
    {
        Snapshot
        // maybe Ack, Command, etc. later
    };

    /// Encoded pipeline packet flags.
    enum class PipelinePacketFlags : std::uint32_t
    {
        None = 0
        // Future:
        // compressed flag, reliable, etc.
    };

    // --- Settings structs ---

    /// Soft final encoded packet byte target for reports. Encode still emits oversized packets.
    struct PacketBudget
    {
        std::uint32_t max_packet_bytes { 1200 };
    };

    /// Configuration for the send-interval policy.
    struct SendIntervalSettings
    {
        /// Send every N ticks.
        std::uint32_t interval_ticks { 1 };
        /// Tick offset for staggered sends.
        std::uint32_t phase_offset { 0 };
    };

    /// Configuration for incremental round-robin selection.
    struct IncrementalSettings
    {
        std::uint32_t max_entities_per_packet { 512 };
    };

    /// Configuration for position/heading quantization
    struct QuantizationSettings
    {
        Aabb3f position_bounds { make_centered_bounds(400.0F) };
    };

    // --- Pipeline state structs ---

    /// Immutable definition of a pipeline profile and its enabled techniques.
    struct PipelineDefinition
    {
        PipelineProfileKind profile { PipelineProfileKind::RawSnapshot };
        CodecKind codec { CodecKind::ByteAligned };
        PipelineTechniqueFlags techniques { PipelineTechniqueFlags::None };
        PacketBudget budget {};
        SendIntervalSettings send_interval {};
        IncrementalSettings incremental {};
        QuantizationSettings quantization {};

        // Future:
        // delta settings, aoi, lod, compression etc.
    };

    /// Caller-owned per-client replication state.
    struct ClientReplicationState
    {
        /// Next outbound sequence id.
        SequenceId next_sequence { 1 };
        /// Latest sequence received from remote.
        SequenceId latest_remote_sequence {};
        /// Latest sequence acknowledged by remote.
        SequenceId latest_acked_sequence {};
        /// Next incremental selection cursor for round-robin selection.
        std::uint32_t incremental_cursor {};
    };

    /// Reusable scratch-memory for encode/decode. Stored externally to avoid allocations on hot path.
    struct PipelineScratch
    {
        /// Indices of entities to send.
        std::vector<std::uint32_t> selected_indices;
        /// IDs to delete (for delta)
        std::vector<EntityNetId> selected_delete_ids;
        /// Temporary buffer for encoding.
        std::vector<Byte> bytes;
    };
}
