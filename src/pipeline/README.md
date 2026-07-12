# simnet_pipeline

`simnet_pipeline` converts authoritative `WorldSnapshot` data into pipeline-owned `EncodedPacket` bytes and decodes those packets into logical `ClientSnapshotPatch` updates.

The first profile is `RawSnapshot`: selected entities are written as byte-aligned records in network byte order, and decode returns a logical snapshot patch. Decode accepts the serialized byte span directly and treats its private wire header as the receive-side metadata source of truth. The header and records are serialized field-by-field, not by writing C++ object memory. Each packet carries a private decode signature for the pipeline representation; decode rejects packets whose signature does not match the local `PipelineDefinition`. `pipeline_decode_signature` exposes that same canonical value to app/session code for compatibility checks; it is not a complete runtime configuration fingerprint.

`PipelineDefinition` names the profile, codec, enabled technique flags, and packet budget. `RawSnapshot` supports the byte-aligned codec, plus the explicit bit-packed codec for quantized oct-heading snapshots. Other technique flags are reserved for later profiles.

`SendInterval` is the first emit policy. When enabled, encode emits only on matching ticks and returns `EncodeResultKind::Skipped` with `EncodeSkipReason::SendInterval` otherwise. Skips do not consume packet sequences.

`Incremental` is the first selection policy. It currently emits upsert-only `SnapshotKind::Patch` packets with a round-robin slice of the current `WorldSnapshot`; missing entities mean unchanged. It does not emit deletes yet, so removed authoritative entities require a future `FullReplace` resync or a later delete-support pass. Dirty flags, AoI, and priorities are not part of this pass.

`Delta` accepts an explicit baseline snapshot and nonzero baseline sequence from the caller. It emits a `SnapshotKind::Patch` containing absolute changed/new upserts and explicit sorted delete IDs. Without a baseline it falls back to `FullReplace`. Decode does not need baseline snapshot contents, but it rejects a delta unless the packet baseline matches the caller's latest successfully applied sequence. Delta selection works with the existing byte-aligned, quantized, oct-heading, and bit-packed record codecs; combining Delta and Incremental is explicitly unsupported.

`Quantization` stores positions as 16-bit unsigned values inside configured bounds and headings as 16-bit signed normalized components. It supports full and delta packets but remains incompatible with Incremental selection.

`OctHeading` stores normalized headings as two octahedral 16-bit components. `BitPacked` is a codec option for quantized oct-heading snapshots; with the current field widths it proves the codec path while staying 15 bytes per entity.

`PacketBudget::max_packet_bytes` is currently a soft reporting target compared against the final encoded packet size, including the private pipeline header. Oversized packets still emit, and `budget_exceeded = true` means only that the final packet exceeded the target; it does not imply encode failure, invalid packet bytes, transport drop, or reliability behavior. Encode advances `ClientReplicationState::next_sequence` only after a packet/report is fully built. Sequence `0` is reserved. Current evaluation assumes sequence IDs do not wrap; a proper wrap protocol is deferred to transport/reliability work. Decode rejects stale or out-of-order sequences, decode-signature mismatches, and malformed packet bytes with `DecodeReport::valid = false`.

The module owns packet codec contracts and reports, but not transport, ECS, rendering, telemetry, synthetic generation, AoI, LOD, delta, compression, or client storage.
