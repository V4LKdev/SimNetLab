module;

#include <cstdint>
#include <span>
#include <string>
#include <vector>

/// @brief Pipeline request, response, and report types.
export module simnet.pipeline:messages;

import :types;
import simnet.core;
import simnet.snapshot;

export namespace simnet
{
    /// Complete per-entity record layout selected for one encoded update.
    enum class EntityRecordLayout : std::uint8_t
    {
        Raw,
        Quantized,
        QuantizedOctHeading,
        BitPackedQuantizedOctHeading,
    };

    /// Optional source-to-canonical quality accounting for produced complete records.
    struct RepresentationReport
    {
        EntityRecordLayout layout{EntityRecordLayout::Raw};
        std::uint32_t record_bytes{};
        std::uint32_t quality_sample_count{};
        double position_error_sum{};
        double position_error_maximum{};
        double heading_angular_error_degrees_sum{};
        double heading_angular_error_degrees_maximum{};
    };

    /// Complete encoded update for downstream compression or packetization.
    struct EncodedUpdate
    {
        SequenceId sequence{};
        std::vector<Byte> bytes;
    };

    /// Metrics produced by each encode call.
    struct EncodeReport
    {
        Tick tick{};
        SequenceId sequence{};
        SequenceId baseline_sequence{};
        SnapshotKind snapshot_kind{SnapshotKind::FullReplace};
        std::uint32_t upsert_count{};
        std::uint32_t delete_count{};
        std::uint32_t recovery_forced_addition_count{};
        RepresentationReport representation{};
        DeltaReport delta{};
        AreaOfInterestReport area_of_interest{};
        LevelOfDetailReport level_of_detail{};
    };

    /// Metrics produced by each decode call.
    struct DecodeReport
    {
        Tick tick{};
        SequenceId sequence{};
        SequenceId baseline_sequence{};
        SnapshotKind snapshot_kind{SnapshotKind::FullReplace};

        bool valid{};        /// Validation status after full header and payload checks.
        std::string error{}; /// First contract violation when validation fails.
    };

    /// Nonallocating first-error result for encoded update header inspection.
    enum class EncodedUpdateHeaderError : std::uint8_t
    {
        None,
        UnsupportedPipeline,
        ByteCountOutOfRange,
        Truncated,
        InvalidMagic,
        UnsupportedVersion,
        SignatureMismatch,
        UnsupportedSnapshotKind,
        ReservedSequence,
        StaleSequence,
        InvalidPayloadSize,
        FullReplaceHasBaseline,
        PatchUnsupported,
        PatchHasReservedBaseline,
        PatchBaselineNotEarlier,
        InvalidPayloadBounds,
    };

    /**
     * Validated fixed-header facts. Variable masked records are validated only by full decode.
     *
     * This value owns no byte storage and inspection never advances replication state.
     */
    struct EncodedUpdateHeaderInspection
    {
        EncodedUpdateHeaderError error{EncodedUpdateHeaderError::None};
        Tick tick{};
        SequenceId sequence{};
        SequenceId baseline_sequence{};
        SnapshotKind snapshot_kind{SnapshotKind::FullReplace};

        [[nodiscard]] bool valid() const noexcept
        {
            return error == EncodedUpdateHeaderError::None;
        }
    };

    /// Input to `encode_snapshot`.
    struct EncodeInput
    {
        WorldSnapshot const* snapshot{}; /// current snapshot (required)
        /// Optional explicit baseline for Delta or temporal LOD.
        WorldSnapshot const* baseline_snapshot{};
        SequenceId baseline_sequence{}; /// nonzero if baseline_snapshot is present
        /// Exact Client replica selected by the application for non-Delta Incremental convergence.
        WorldSnapshot const* replica_snapshot{};
        SequenceId replica_sequence{}; /// nonzero if replica_snapshot is present
        /// Sorted IDs whose latest canonical state must join an Incremental schedule.
        std::span<EntityNetId const> recovery_upsert_ids{};
        /// Emits a complete current population without advancing the Incremental cursor.
        bool force_full_replace{};
        /// Authoritative AOI pose. Required only when AOI is enabled and cadence emits.
        InterestSource const* interest_source{};
        /// Sorted source indices returned by the Server-owned coarse spatial query.
        std::span<std::uint32_t const> candidate_indices{};
    };

    /// Input to `decode_update`.
    struct DecodeInput
    {
        ByteSpan bytes{}; /// raw encoded update bytes
        /// Exact retained baseline used to complete masked existing Patch records.
        WorldSnapshot const* baseline_snapshot{};
        /// Must match the encoded Patch baseline when baseline_snapshot is supplied.
        SequenceId baseline_sequence{};
    };

    /// Result of `encode_snapshot`.
    struct EncodeOutput
    {
        EncodeResultKind kind{EncodeResultKind::Update};
        EncodedUpdate update{};
        EncodeReport report{};
        /// Exact complete logical Client state represented by a successful update.
        WorldSnapshot resulting_snapshot{};
    };

    /// Result of `decode_update`.
    struct DecodeOutput
    {
        SnapshotUpdate update{};
        DecodeReport report{};
    };
}
