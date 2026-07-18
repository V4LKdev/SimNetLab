# simnet_pipeline

`simnet_pipeline` selects snapshot data, transforms it, encodes packet bytes, and decodes them into `ClientSnapshotPatch` values. It does not depend on transport, Flecs, configuration, telemetry, rendering, or client storage.

## Public API

- `PipelineDefinition` describes the enabled techniques and their settings.
- `ClientReplicationState` holds per-client sequence and incremental cursor state.
- `PipelineScratch` owns reusable encode and decode buffers.
- `make_snapshot_pipeline` creates a default definition.
- `validate_pipeline_definition` rejects unsupported technique combinations and invalid settings.
- `encode_snapshot` produces a packet or a skipped result.
- `decode_packet` validates bytes and returns a patch or an error report.
- `pipeline_decode_signature` identifies the receiver-side representation.

Each concurrent caller needs its own `ClientReplicationState` and `PipelineScratch`.

## Supported techniques

`SendInterval` skips selected ticks without consuming a sequence number. `Incremental` emits round-robin upsert-only patches. `Quantization` encodes positions within configured bounds. `OctHeading` requires quantization and encodes a heading as two octahedral components. `Delta` emits a baseline-relative patch when the caller supplies a retained acknowledged baseline. Without one, encoding emits `FullReplace`. `Delta` cannot be combined with `Incremental`.

`BitPacking` requires quantization and octahedral headings. The current record layout totals 120 bits, which is also 15 bytes in the byte-aligned representation. It is retained for technique evaluation even though it currently does not reduce record size.

`Aoi`, `Lod`, and `Compression` are retained public vocabulary for planned work. They are not supported by the current pipeline validator.

## Contracts

- Sequence zero is reserved. Encoding fails if allocation would wrap to zero.
- Packet budgets are soft reporting targets. Transport owns actual send limits.
- `PipelineScratch` should be reused across calls to avoid recurring allocations.
- The private wire header carries a magic value, protocol and schema versions, and a decode signature. Decode rejects mismatches and stale sequences.
