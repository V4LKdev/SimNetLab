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

    /// Fully encoded update ready for compression or opaque byte-group packetization.
    struct EncodedUpdate
    {
        SequenceId sequence{};
        std::vector<Byte> bytes; /// raw header + body
    };

    /// Metrics produced by each encode call.
    struct EncodeReport
    {
        Tick tick{};
        SequenceId sequence{};
        SequenceId baseline_sequence{};
        SnapshotKind snapshot_kind{SnapshotKind::FullReplace};
        std::uint32_t upsert_count{}; /// number of upserts in the payload
        std::uint32_t delete_count{}; /// number of deletes in the payload
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

        bool valid{}; /// true when the encoded update passed all contract checks
        std::string error{}; /// error message when !valid
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
        /// Collect source-to-canonical quality only for produced upserts.
        bool collect_representation_quality{};
        /// Authoritative AOI pose. Required only when AOI is enabled and cadence emits.
        InterestSource const* interest_source{};
        /// Sorted source indices returned by the Server-owned coarse spatial query.
        std::span<std::uint32_t const> candidate_indices{};
    };

    /// Input to `decode_update`.
    struct DecodeInput
    {
        ByteSpan bytes{}; /// raw encoded update bytes
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
