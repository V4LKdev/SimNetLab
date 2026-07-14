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
    /// Fully encoded packet ready to be handed to the transport layer.
    struct EncodedPacket
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

        bool emitted           {}; /// true when a packet was emitted
        bool skipped           {}; /// true when the call was skipped
        bool delta             {}; /// true when a delta baseline was used
        EncodeSkipReason skip_reason { EncodeSkipReason::None };
        bool budget_exceeded   {}; /// true when the final packet exceeded the budget

        std::uint32_t input_entities    {}; /// total entities in the source snapshot
        std::uint32_t selected_entities {}; /// entities selected for this packet
        std::uint32_t upsert_count      {}; /// number of upserts in the payload
        std::uint32_t delete_count      {}; /// number of deletes in the payload

        std::uint32_t packet_bytes      {}; /// full size including header
        std::uint32_t payload_bytes     {}; /// body size before any compression
        std::uint32_t uncompressed_bytes{}; /// placeholder for future decompressed size
        std::uint32_t final_bytes       {}; /// final size after all processing
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
        std::uint32_t packet_bytes {};

        bool delta {};
        bool valid {};                /// true when the packet passed all contract checks
        std::string error {};         /// error message when !valid
    };

    /// Input to `encode_snapshot`.
    struct EncodeInput
    {
        WorldSnapshot const* snapshot {};            /// current snapshot (required)
        WorldSnapshot const* baseline_snapshot {};   /// optional baseline for delta
        SequenceId           baseline_sequence {};   /// nonzero if delta
    };

    /// Input to `decode_packet`.
    struct DecodeInput
    {
        ByteSpan   bytes {};                         /// raw packet bytes
        SequenceId applied_baseline_sequence {};     /// last successfully applied sequence
    };

    /// Result of `encode_snapshot`.
    struct EncodeOutput
    {
        EncodeResultKind kind { EncodeResultKind::Packet };
        EncodeSkipReason skip_reason { EncodeSkipReason::None };
        EncodedPacket    packet {};
        EncodeReport     report {};
    };

    /// Result of `decode_packet`.
    struct DecodeOutput
    {
        ClientSnapshotPatch patch {};
        DecodeReport report {};
    };
}
