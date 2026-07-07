# simnet_pipeline

`simnet_pipeline` converts authoritative `WorldSnapshot` data into pipeline-owned `EncodedPacket` bytes and decodes those packets into logical `ClientSnapshotPatch` updates.

The first profile is `RawFullReplace`: all entities are selected, byte-aligned records are written in network byte order, and decode returns a full-replace patch. The private wire header and records are serialized field-by-field, not by writing C++ object memory.

`PipelineDefinition` names the profile, codec, enabled technique flags, and packet budget. `RawFullReplace` uses the byte-aligned codec and no techniques. Skipped encode results are API vocabulary for later stages and are not emitted by the raw baseline.

`PacketBudget::max_packet_bytes` is compared against the final encoded packet size, including the private pipeline header. Encode advances `ClientReplicationState::next_sequence` only after a packet/report is fully built. Sequence `0` is reserved. Decode rejects stale or out-of-order sequences and treats malformed packet bytes as data errors with `DecodeReport::valid = false`.

The module owns packet codec contracts and reports, but not transport, ECS, rendering, telemetry, synthetic generation, AoI, LOD, delta, compression, or client storage.
