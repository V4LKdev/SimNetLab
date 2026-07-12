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
    /// Available compiled pipeline profiles.
    enum class PipelineProfileKind : std::uint8_t
    {
        RawSnapshot
    };

    /// Packet codec family used by a pipeline profile.
    enum class CodecKind : std::uint8_t
    {
        ByteAligned,
        BitPacked
    };

    /// Optional replication techniques enabled by a pipeline definition.
    enum class PipelineTechniqueFlags : std::uint32_t
    {
        None = 0,
        SendInterval = 1U << 0U,
        Incremental = 1U << 1U,
        Quantization = 1U << 2U,
        OctHeading = 1U << 3U,
        Delta = 1U << 4U,
        Aoi = 1U << 5U,
        Lod = 1U << 6U,
        Compression = 1U << 7U
    };

    /// Result kind for an encode call.
    enum class EncodeResultKind : std::uint8_t
    {
        Packet,
        Skipped
    };

    /// Reason an encode call produced no packet.
    enum class EncodeSkipReason : std::uint8_t
    {
        None,
        SendInterval
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
    [[nodiscard]] constexpr bool has_flag(
        PipelineTechniqueFlags value,
        PipelineTechniqueFlags flag
    ) noexcept
    {
        return (value & flag) == flag;
    }

    /// Encoded pipeline packet kind.
    enum class PipelinePacketKind : std::uint8_t
    {
        Snapshot
    };

    /// Encoded pipeline packet flags.
    enum class PipelinePacketFlags : std::uint32_t
    {
        None = 0
    };

    /// Soft final encoded packet byte target for reports; encode still emits oversized packets.
    struct PacketBudget
    {
        std::uint32_t max_packet_bytes { 1200 };
    };

    /// Tick cadence settings for optional send interval policy.
    struct SendIntervalSettings
    {
        std::uint32_t interval_ticks { 1 };
        std::uint32_t phase_offset { 0 };
    };

    /// Round-robin upsert-only partial snapshot settings.
    struct IncrementalSettings
    {
        std::uint32_t max_entities_per_packet { 512 };
    };

    /// Byte-aligned quantization settings.
    struct QuantizationSettings
    {
        Aabb3f position_bounds { make_centered_bounds(400.0F) };
    };

    /// Immutable pipeline profile definition.
    struct PipelineDefinition
    {
        PipelineProfileKind profile { PipelineProfileKind::RawSnapshot };
        CodecKind codec { CodecKind::ByteAligned };
        PipelineTechniqueFlags techniques { PipelineTechniqueFlags::None };
        PacketBudget budget {};
        SendIntervalSettings send_interval {};
        IncrementalSettings incremental {};
        QuantizationSettings quantization {};
    };

    /// Caller-owned per-client replication state.
    struct ClientReplicationState
    {
        SequenceId next_sequence { 1 };
        SequenceId latest_remote_sequence {};
        SequenceId latest_acked_sequence {};
        std::uint32_t incremental_cursor {};
    };

    /// Caller-owned reusable pipeline scratch memory.
    struct PipelineScratch
    {
        std::vector<std::uint32_t> selected_indices;
        std::vector<std::byte> bytes;
    };

    /// Pipeline-owned encoded transfer object.
    struct EncodedPacket
    {
        Tick tick {};
        SequenceId sequence {};
        SequenceId baseline_sequence {};
        PipelinePacketKind kind { PipelinePacketKind::Snapshot };
        PipelinePacketFlags flags { PipelinePacketFlags::None };
        std::vector<std::byte> bytes;
    };

    /// Raw facts reported by encode.
    struct EncodeReport
    {
        Tick tick {};
        SequenceId sequence {};
        PipelineProfileKind profile { PipelineProfileKind::RawSnapshot };
        CodecKind codec { CodecKind::ByteAligned };
        PipelineTechniqueFlags techniques { PipelineTechniqueFlags::None };
        bool emitted {};
        bool skipped {};
        EncodeSkipReason skip_reason { EncodeSkipReason::None };
        bool budget_exceeded {};
        std::uint32_t input_entities {};
        std::uint32_t selected_entities {};
        std::uint32_t upsert_count {};
        std::uint32_t delete_count {};
        std::uint32_t packet_bytes {};
        std::uint32_t payload_bytes {};
        std::uint32_t uncompressed_bytes {};
        std::uint32_t final_bytes {};
    };

    /// Raw facts reported by decode.
    struct DecodeReport
    {
        Tick tick {};
        SequenceId sequence {};
        std::uint32_t upsert_count {};
        std::uint32_t delete_count {};
        std::uint32_t packet_bytes {};
        bool valid {};
        std::string error {};
    };

    /// Encode request.
    struct EncodeInput
    {
        WorldSnapshot const* snapshot {};
    };

    /// Decode request.
    struct DecodeInput
    {
        std::span<std::byte const> bytes {};
    };

    /// Encode result.
    struct EncodeOutput
    {
        EncodeResultKind kind { EncodeResultKind::Packet };
        EncodeSkipReason skip_reason { EncodeSkipReason::None };
        EncodedPacket packet {};
        EncodeReport report {};
    };

    /// Decode result.
    struct DecodeOutput
    {
        ClientSnapshotPatch patch {};
        DecodeReport report {};
    };
}
