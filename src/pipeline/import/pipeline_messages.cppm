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
        EncodeSkipReason skip_reason{EncodeSkipReason::None};
        SequenceId sequence{};
        SequenceId baseline_sequence{};
        SnapshotKind snapshot_kind{SnapshotKind::FullReplace};
        std::uint32_t upsert_count{}; /// number of upserts in the payload
        std::uint32_t delete_count{}; /// number of deletes in the payload
        AreaOfInterestReport area_of_interest{};
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
        WorldSnapshot const* baseline_snapshot{}; /// optional baseline for delta
        SequenceId baseline_sequence{}; /// nonzero if delta
        /// Latest exact emitted Client result for non-Delta Incremental convergence.
        WorldSnapshot const* replica_snapshot{};
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
