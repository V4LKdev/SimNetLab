# simnet_pipeline

`simnet_pipeline` selects snapshot data, transforms it, produces encoded updates, and decodes them into `SnapshotUpdate` values. It does not depend on transport, Flecs, configuration, telemetry, rendering, or client storage.

## Public API

- `PipelineDefinition` describes the enabled techniques and their settings.
- `ClientReplicationState` holds per-client sequence and incremental cursor state.
- `PipelineScratch` owns reusable encode and decode buffers.
- `validate_pipeline_definition` rejects unsupported technique combinations and invalid settings.
- `should_emit_snapshot` provides the pure cadence preflight used before baseline resolution.
- `encode_snapshot` produces an encoded update or a skipped result. Emitted output includes the
  exact complete logical Client snapshot represented by its canonical encoded values.
- `decode_update` validates encoded update bytes and returns a state update or an error report.
- `pipeline_decode_signature` identifies the receiver-side representation.

Each concurrent caller needs its own `ClientReplicationState` and `PipelineScratch`.

## Supported techniques

`SendInterval` uses the authoritative snapshot tick. Interval `1` emits every tick. Interval `N > 1`
emits only when `tick % N == 0`. Other ticks return `Skipped` with reason `SendInterval` and do
not change sequence, selection, scratch, or baseline state. `Incremental` without `Delta`
schedules round-robin candidate upserts. `Delta` filters scheduled candidates against a retained
explicit baseline, while retaining every baseline-only delete. The first non-Delta
`Incremental` emission is a complete `FullReplace` and does not advance the cursor. Later
incremental Patch carries the exact replica sequence and includes every entity removed from that
replica. Applications choose the latest submitted replica for reliable ordered delivery or the
latest ACK-proven replica for loss recovery. Without a
baseline, `Delta` emits a complete `FullReplace` and does not advance the incremental cursor.
`DistanceBands` level of detail operates on the AOI-retained population. It keeps complete entity
records while making Near entities due every tick and distributing Medium and Far work across
configured deterministic intervals. Due work remains latched until a successful update commit.
It produces explicit-baseline Patches with or without Incremental and Delta. Incremental caps only
ordinary pending work. Player self and delivery-recovery upserts bypass that cap. Deletes always
bypass temporal scheduling.
`Quantization` encodes
positions within configured bounds. `OctHeading` requires quantization and encodes a heading as two
octahedral components.

`BitPacking` requires quantization and octahedral headings. The current record layout totals 128 bits, which is also 16 bytes in the byte-aligned representation. It is retained for technique evaluation even though it currently does not reduce record size.

## Stage order and compatibility

| Order | Stage | Current control | Compatibility and ownership |
| ---: | --- | --- | --- |
| 1 | Cadence control | `SendInterval` | Composes with every active technique. A skipped call changes no sequence or selection state. |
| 2 | Relevancy selection | `AreaOfInterestSettings` | None passes the complete source. Radius uses inclusive 3D distance. FOV adds an inclusive 3D cone test. The application supplies sorted coarse source indices without exposing its spatial implementation. |
| 3 | Temporal LOD | `LevelOfDetailSettings` | Distance bands classify AOI-retained entities with inclusive squared-distance boundaries. Persistent due state is bounded by the retained population. |
| 4 | Update scheduling | `Incremental` | Optionally caps ordinary pending LOD work with a fair cyclic scan. It advances by serviced candidates, including candidates later removed by delta selection. |
| 5 | Delivery recovery merge | Application-provided canonical IDs | Recovery upserts bypass temporal LOD and the ordinary Incremental cap. |
| 6 | Delta selection | `Delta` plus an exact retained baseline | Filters scheduled or complete upsert candidates. Baseline-only deletes remain truthful and are included. Without a baseline this stage is inactive. |
| 7 | Representation encoding | `Quantization`, `OctHeading` | Octahedral headings require quantization. PIPE-011 owns comparing delta candidates by canonical encoded values. |
| 8 | Record layout | `BitPacking` | Bitpacking requires quantization and octahedral headings. It does not change decoded precision. |
| 9 | Whole-update compression | `simnet_compression` | Outside the pipeline. It transforms the complete encoded update as opaque bytes. |
| 10 | Application packetization | `simnet_packetization` | Outside the pipeline. It owns the hard payload budget and consumes opaque pipeline bytes. |
| 11 | Per-packet compression | `simnet_compression` | Outside packetization. It independently transforms complete serialized application packets. |
| 12 | Delivery | Application and transport configuration | Outside the pipeline. Transport carries opaque application bytes. |

Unsupported stages fail validation. All active techniques otherwise compose when their listed prerequisites are met.

## Contracts

- Sequence zero is reserved. Encoding fails if allocation would wrap to zero.
- The encoded update size target is used only for reporting. Transport owns actual send limits.
- `PipelineScratch` should be reused across calls to avoid recurring allocations.
- Active AOI requires a finite authoritative interest source. A missing source returns a skipped
  result and never falls back to the complete population.
- Radius and conical FOV boundaries are inclusive. The FOV setting is a full angle in degrees.
  Zero-distance entities are retained and entities behind the source are rejected.
- For offset `d`, radius accepts `dot(d, d) <= radius^2`. FOV also requires
  `dot(forward, d) >= 0` and `dot(forward, d)^2 >= dot(d, d) * cos(fov_degrees / 2)^2`.
- The private wire header carries a magic value, protocol and schema versions, and a decode signature. Schema version 5 requires every Patch to carry an explicit nonzero baseline sequence. Decode rejects mismatches and stale sequences.
- Patch decoding reports the declared baseline sequence. Client storage must retain and resolve that exact reconstructed snapshot before applying the patch.
- Distance LOD uses Near when `distance_squared <= near_distance^2`, Medium through the inclusive
  medium boundary, and Far for the remainder of the AOI. Player self is always Near.
- Boundary oscillation can increase transmitted work. No hysteresis is applied, and oscillation
  cannot clear pending work or change canonical convergence.
- With retained population `N`, Incremental cap `K`, and cadence `S`, persistent ordinary due work
  is serviced within at most `ceil(N / K)` committed emissions. A conservative scheduling-age
  bound is the band interval plus `ceil(N / K) * S` ticks. Network delivery delay is separate.

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
