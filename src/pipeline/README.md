# simnet_pipeline

`simnet_pipeline` selects snapshot data, transforms it, produces encoded updates, and decodes them into `SnapshotUpdate` values. It does not depend on transport, Flecs, configuration, telemetry, rendering, or client storage.

## Public API

- `PipelineDefinition` describes the enabled techniques and their settings.
- `ClientReplicationState` holds per-client sequence and incremental cursor state.
- `PipelineScratch` owns reusable encode and decode buffers.
- `validate_pipeline_definition` rejects unsupported technique combinations and invalid settings.
- `encode_snapshot` produces an encoded update or a skipped result.
- `decode_update` validates encoded update bytes and returns a state update or an error report.
- `pipeline_decode_signature` identifies the receiver-side representation.

Each concurrent caller needs its own `ClientReplicationState` and `PipelineScratch`.

## Supported techniques

`SendInterval` skips selected ticks without consuming a sequence number. `Incremental` without `Delta` schedules round-robin candidate upserts. `Delta` filters scheduled candidates against a retained acknowledged baseline, while retaining every baseline-only delete. Without a baseline, `Delta` emits a complete `FullReplace` and does not advance the incremental cursor. `Quantization` encodes positions within configured bounds. `OctHeading` requires quantization and encodes a heading as two octahedral components.

`BitPacking` requires quantization and octahedral headings. The current record layout totals 128 bits, which is also 16 bytes in the byte-aligned representation. It is retained for technique evaluation even though it currently does not reduce record size.

## Stage order and compatibility

| Order | Stage | Current control | Compatibility and ownership |
| ---: | --- | --- | --- |
| 1 | Cadence control | `SendInterval` | Composes with every active technique. A skipped call changes no sequence or selection state. |
| 2 | Relevancy selection | None | Reserved for later AOI and FOV work. It will consume authoritative snapshot state. |
| 3 | Update scheduling | `Incremental` | Composes with every active representation and delta technique. It advances by scheduled candidates, including candidates later removed by delta selection. |
| 4 | Delta selection | `Delta` plus an exact retained baseline | Filters scheduled or complete upsert candidates. Baseline-only deletes remain truthful and are included. Without a baseline this stage is inactive. |
| 5 | Representation encoding | `Quantization`, `OctHeading` | Octahedral headings require quantization. PIPE-011 owns comparing delta candidates by canonical encoded values. |
| 6 | Record layout | `BitPacking` | Bitpacking requires quantization and octahedral headings. It does not change decoded precision. |
| 7 | Whole-update compression | None | Later compression consumes the complete encoded update. |
| 8 | Application packetization | None | Later packetization owns the hard payload budget and consumes pipeline bytes. |
| 9 | Delivery | Application and transport configuration | Outside the pipeline. Transport carries opaque application bytes. |

Unsupported stages fail validation. All active techniques otherwise compose when their listed prerequisites are met.

## Contracts

- Sequence zero is reserved. Encoding fails if allocation would wrap to zero.
- The encoded update size target is used only for reporting. Transport owns actual send limits.
- `PipelineScratch` should be reused across calls to avoid recurring allocations.
- The private wire header carries a magic value, protocol and schema versions, and a decode signature. Schema version 4 adds one lossless classification byte to every entity record. Decode rejects mismatches and stale sequences.
- Delta decoding reports the declared baseline sequence. Client storage must retain and resolve that exact reconstructed snapshot before applying the patch.

### Reference FullReplace codec

The reference layout is a complete, baseline-independent `FullReplace` with baseline sequence zero.
All fields are fixed-width and written in network byte order. It uses IEEE 754 binary32 bit patterns
for float fields and is not a native C++ object representation.

| Field | Width | Order |
| --- | ---: | --- |
| Header | 45 bytes | magic `u32`, protocol `u16`, schema `u16`, decode signature `u64`, kind `u8`, tick `u64`, sequence `u32`, baseline sequence `u32`, upsert count `u32`, delete count `u32`, payload bytes `u32` |
| Reference entity | 30 bytes | id `u32`, classification `u8`, position x/y/z `f32`, heading x/y/z `f32`, hue `u8` |

FullReplace records are encoded in the snapshot's strictly ascending entity-ID order. The decoder
checks the complete declared payload before advancing its accepted sequence, then delegates entity
ID ordering and snapshot semantics to `simnet_snapshot`.
