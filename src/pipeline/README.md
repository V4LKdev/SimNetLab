# simnet_pipeline

`simnet_pipeline` converts authoritative `WorldSnapshot` data into pipeline-owned `EncodedPacket` bytes and decodes those packets into logical `ClientSnapshotPatch` updates.

The first profile is `RawSnapshot`: selected entities are written as byte-aligned records in network byte order, and decode returns a logical snapshot patch. The private wire header and records are serialized field-by-field, not by writing C++ object memory.

`PipelineDefinition` names the profile, codec, enabled technique flags, and packet budget. `RawSnapshot` uses the byte-aligned codec and can be combined with the `SendInterval` and `Incremental` policies. Other technique flags are reserved for later profiles.

`SendInterval` is the first emit policy. When enabled, encode emits only on matching ticks and returns `EncodeResultKind::Skipped` with `EncodeSkipReason::SendInterval` otherwise. Skips do not consume packet sequences.

`Incremental` is the first selection policy. It emits `SnapshotKind::Patch` packets with a round-robin slice of the current `WorldSnapshot`; missing entities mean unchanged. Deletes, dirty flags, AoI, priorities, and deltas are not part of this pass.

`Quantization` is the first transform policy. In this pass it is byte-aligned and full-snapshot only: positions are stored as 16-bit unsigned values inside configured bounds, and headings are stored as 16-bit signed normalized components.

`PacketBudget::max_packet_bytes` is compared against the final encoded packet size, including the private pipeline header. Encode advances `ClientReplicationState::next_sequence` only after a packet/report is fully built. Sequence `0` is reserved. Decode rejects stale or out-of-order sequences and treats malformed packet bytes as data errors with `DecodeReport::valid = false`.

The module owns packet codec contracts and reports, but not transport, ECS, rendering, telemetry, synthetic generation, AoI, LOD, delta, compression, or client storage.
