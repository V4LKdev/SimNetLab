@defgroup pipeline simnet.pipeline
@brief Snapshot replication pipeline.

## Exported Types

### simnet.pipeline:types
- `PipelineProfileKind` - named presets (currently only `RawSnapshot`).
- `CodecKind` - currently `ByteAligned`.
- `PipelineTechniqueFlags` - bitmask of optional techniques: `SendInterval`, `Incremental`, `Quantization`, `OctHeading`, `Delta`, `BitPacking`, and reserved flags (`Aoi`, `Lod`, `Compression`). Future flags: `DeadReckoning`, `DirtyFlags`, `LeaderFollower`.
- `PipelineDefinition` - immutable configuration combining profile, codec, technique flags, and per-technique settings.
- `ClientReplicationState` - per-client mutable state (sequence numbers, incremental cursor).
- `PipelineScratch` - reusable buffers for encode/decode, owned by the caller to avoid allocations on the hot path.
- `EncodeResultKind`, `EncodeSkipReason`, `PipelinePacketKind`, `PipelinePacketFlags` - metadata enums.

### simnet.pipeline:messages
- `EncodedPacket` - fully encoded output with tick, sequence, and raw `Byte` vector.
- `EncodeInput` / `EncodeOutput`, `DecodeInput` / `DecodeOutput` - request/response envelopes.
- `EncodeReport`, `DecodeReport` - detailed per-call metrics for telemetry and debugging.

### simnet.pipeline:codec
- `make_raw_snapshot_pipeline` - factory returning a default byte-aligned `PipelineDefinition`.
- `pipeline_decode_signature` - canonical hash for receiver-side compatibility checks.
- `encode_snapshot` - converts a `WorldSnapshot` into an `EncodedPacket`, respecting technique flags.
- `decode_packet` - converts raw bytes into a `ClientSnapshotPatch`, rejecting invalid or incompatible packets.

## Technique Reference

### Send Interval
When enabled, encoding only emits packets on ticks matching `(tick + phase) % interval == 0`. Skipped ticks return `EncodeResultKind::Skipped` with reason `SendInterval` and do not consume sequence numbers.

### Incremental
Emits upsert-only `Patch` packets using a round-robin cursor over the entity list. Missing entities are treated as unchanged. Delete operations are not yet supported. Removed entities require a future `FullReplace` resync.

### Quantization
Stores positions as three 16-bit unsigned values within configurable `Aabb3f` bounds (`QuantizationSettings`). Headings are stored as three 16-bit signed normalized components.

### Oct Heading
Stores headings as two octahedral 16-bit components, reducing per-entity heading size from 12 bytes (or 6 bytes quantized) to 4 bytes. **Requires `Quantization`.** Works with both byte-aligned records and the `BitPacking` technique.

### Delta
Emits `Patch` packets containing only entities that changed relative to a baseline snapshot. Requires a baseline snapshot and non-zero baseline sequence from the caller. Falls back to `FullReplace` when no baseline is provided. Decode rejects delta packets whose baseline sequence differs from the receiver's last applied sequence. **Incompatible with `Incremental`.** Delta works with quantization, oct heading, and `BitPacking`.

### Bit Packing
A technique flag (`PipelineTechniqueFlags::BitPacking`) that stores records in a continuous bit stream, currently at 15 bytes per entity. In the current implementation it **requires both `Quantization` and `OctHeading`**.

### Reserved Flags
`Aoi`, `Lod`, and `Compression` are defined in `PipelineTechniqueFlags` but not yet implemented. Their settings structs will be added to `PipelineDefinition` once the corresponding selection or post-processing passes are written.

## Important Notes

- Sequence `0` is reserved. `ClientReplicationState::next_sequence` starts at `1`. Encode throws if sequence would wrap to `0`.
- `PacketBudget::max_packet_bytes` is a soft target. Oversized packets still emit and only set `budget_exceeded = true` in the report.
- `PipelineScratch` must be reused across calls to avoid repeated allocations. Its internal vectors are cleared and refilled by encode/decode.
- All public encode and decode functions are thread-safe when each thread supplies its own `ClientReplicationState` and `PipelineScratch`.
- The private wire header contains a magic value (`SNPL`), protocol/schema versions, and a decode signature. Decode rejects packets with mismatched magic, versions, signature, or out-of-order/stale sequences.
- The module owns the packet encoding contracts and reports, but not transport, ECS, rendering, or client-side storage.
