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
    /// Fully encoded update ready to be handed to the transport layer.
    struct EncodedUpdate
    {
        Tick                tick {};
        SequenceId          sequence {};
        SequenceId          baseline_sequence {};   /// nonzero for delta
        std::vector<Byte>   bytes;                  /// raw header + body
    };

    /// Metrics produced by each encode call.
    struct EncodeReport
    {
        Tick                 tick {};
        SequenceId           sequence {};
        SequenceId           baseline_sequence {};
        SnapshotKind         snapshot_kind { SnapshotKind::FullReplace };
        PipelineTechniqueFlags techniques { PipelineTechniqueFlags::None };

        bool emitted           {}; /// true when an encoded update was emitted
        bool skipped           {}; /// true when the call was skipped
        bool delta             {}; /// true when a delta baseline was used
        EncodeSkipReason skip_reason { EncodeSkipReason::None };
        bool size_target_exceeded {}; /// true when the encoded update exceeded its size target

        std::uint32_t input_entities    {}; /// total entities in the source snapshot
        std::uint32_t selected_entities {}; /// entities selected for this update
        std::uint32_t upsert_count      {}; /// number of upserts in the payload
        std::uint32_t delete_count      {}; /// number of deletes in the payload

        std::uint32_t encoded_update_bytes {}; /// full size including header
        std::uint32_t payload_bytes        {}; /// body size before any compression
        std::uint32_t uncompressed_bytes   {}; /// equals final_bytes until compression exists
        std::uint32_t final_bytes          {}; /// final size after all processing
    };

    /// Metrics produced by each decode call.
    struct DecodeReport
    {
        Tick         tick {};
        SequenceId   sequence {};
        SequenceId   baseline_sequence {};
        SnapshotKind snapshot_kind { SnapshotKind::FullReplace };

        std::uint32_t upsert_count {};
        std::uint32_t delete_count {};
        std::uint32_t encoded_update_bytes {};

        bool delta {};
        bool valid {};                /// true when the encoded update passed all contract checks
        std::string error {};         /// error message when !valid
    };

    /// Input to `encode_snapshot`.
    struct EncodeInput
    {
        WorldSnapshot const* snapshot {};            /// current snapshot (required)
        WorldSnapshot const* baseline_snapshot {};   /// optional baseline for delta
        SequenceId           baseline_sequence {};   /// nonzero if delta
    };

    /// Input to `decode_update`.
    struct DecodeInput
    {
        ByteSpan bytes {};                           /// raw encoded update bytes
    };

    /// Result of `encode_snapshot`.
    struct EncodeOutput
    {
        EncodeResultKind kind { EncodeResultKind::Update };
        EncodeSkipReason skip_reason { EncodeSkipReason::None };
        EncodedUpdate    update {};
        EncodeReport     report {};
    };

    /// Result of `decode_update`.
    struct DecodeOutput
    {
        SnapshotUpdate update {};
        DecodeReport report {};
    };
}
