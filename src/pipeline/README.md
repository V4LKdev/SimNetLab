# simnet_pipeline

`simnet_pipeline` selects snapshot data, transforms it, produces encoded updates, and decodes them
into `SnapshotUpdate` values.

## Public API

- `PipelineDefinition` describes the enabled techniques and their settings.
- `ClientReplicationState` holds per-client sequence and incremental cursor state.
- `PipelineScratch` owns reusable encode-side selection, preparation, and byte buffers.
- `validate_pipeline_definition` rejects unsupported technique combinations and invalid settings.
- `should_emit_snapshot` provides the pure cadence preflight used before baseline resolution.
- `encode_snapshot` produces an encoded update or a skipped result. Emitted output includes the
  exact complete logical Client snapshot represented by its canonical encoded values.
- Optional representation quality accounting compares only produced source upserts with their
  already prepared canonical records. Position error uses world units. Heading angular error uses
  degrees. Disabled collection adds no per-record quality math.
- `decode_update` validates encoded update bytes and returns a state update or an error report.
- `pipeline_decode_signature` identifies the receiver-side representation.

Each concurrent encoder needs its own `ClientReplicationState` and `PipelineScratch`. Each
concurrent decoder needs its own `ClientReplicationState`.

## Supported techniques

### Cadence and incremental scheduling

`SendInterval` uses the authoritative snapshot tick. Interval `1` emits every tick. Interval `N > 1`
emits only when `tick % N == 0`. Other ticks return `Skipped` without changing sequence, selection,
scratch, or baseline state.

`Incremental` without `Delta` schedules candidate upserts with a fair round-robin scan. Its first
emission is a complete `FullReplace` and does not advance the cursor. Later incremental Patches carry
the exact replica sequence and include every entity removed from that replica.

### AOI and temporal LOD

`DistanceBands` operates on the AOI-retained population and keeps complete entity records. Near
entities are due every tick, while Medium and Far work is distributed across configured
deterministic intervals. Due work remains latched until a successful update commit. Distance bands
produce explicit-baseline Patches with or without `Incremental` and `Delta`.

The Incremental cap limits only ordinary pending work. Player self and delivery-recovery upserts
bypass the cap. Deletes bypass temporal scheduling.

### Delta and recovery

`Delta` filters scheduled candidates against an explicit retained baseline and preserves every
baseline-only delete. Applications use the latest submitted replica for reliable ordered delivery
and the latest ACK-proven replica for loss recovery. Without a baseline, `Delta` emits a complete
`FullReplace` and does not advance the incremental cursor. Delivery-recovery upserts are merged
after temporal scheduling and therefore bypass LOD and the ordinary Incremental cap.

### Representation

Raw records preserve the source values. `Quantization` encodes positions within configured bounds.
`OctHeading` requires quantization and encodes a heading as two octahedral components. `BitPacking`
requires both techniques and produces a 128-bit record. Byte-aligned octahedral and bit-packed
records are both 16 bytes and have identical canonical precision.

Each representation report identifies its complete-record layout and width. Optional quality
collection reports Euclidean position error in world units and normalized heading angular error in
degrees for produced upserts only. Delta-suppressed candidates and deletes are not samples. Raw
records report exact zero error.

## Stage order and compatibility

| Order | Stage | Current control | Compatibility and ownership |
| ---: | --- | --- | --- |
| 1 | Cadence control | `SendInterval` | Composes with every active technique. A skipped call changes no sequence or selection state. |
| 2 | Relevancy selection | `AreaOfInterestSettings` | None passes the complete source. Radius uses inclusive 3D distance. FOV adds an inclusive 3D cone test. The application supplies sorted coarse source indices without exposing its spatial implementation. |
| 3 | Temporal LOD | `LevelOfDetailSettings` | Distance bands classify AOI-retained entities with inclusive squared-distance boundaries. Persistent due state is bounded by the retained population. |
| 4 | Update scheduling | `Incremental` | Optionally caps ordinary pending LOD work with a fair cyclic scan. It advances by serviced candidates, including candidates later removed by delta selection. |
| 5 | Delivery recovery merge | Application-provided canonical IDs | Recovery upserts bypass temporal LOD and the ordinary Incremental cap. |
| 6 | Delta selection | `Delta` plus an exact retained baseline | Filters scheduled or complete upsert candidates. Baseline-only deletes remain truthful and are included. Without a baseline this stage is inactive. |
| 7 | Representation encoding | `Quantization`, `OctHeading` | Octahedral headings require quantization. Delta candidates are compared using canonical encoded values so representation changes remain consistent with transmitted state. |
| 8 | Record layout | `BitPacking` | Bitpacking requires quantization and octahedral headings. It does not change decoded precision. |
| 9 | Whole-update compression | `simnet_compression` | Outside the pipeline. It transforms the complete encoded update as opaque bytes. |
| 10 | Application packetization | `simnet_packetization` | Outside the pipeline. It owns the hard payload budget and consumes opaque pipeline bytes. |
| 11 | Per-packet compression | `simnet_compression` | Outside packetization. It independently transforms complete serialized application packets. |
| 12 | Delivery | Application and transport configuration | Outside the pipeline. Transport carries opaque application bytes. |

Unsupported stages fail validation. All active techniques otherwise compose when their listed prerequisites are met.

## Contracts

### State and sequencing

- Sequence zero is reserved. Encoding fails if allocation would wrap to zero.
- The encoded update size target is used only for reporting. Transport owns actual send limits.
- `PipelineScratch` should be reused across calls to avoid recurring allocations.
- With retained population `N`, Incremental cap `K`, and cadence `S`, persistent ordinary due work
  is serviced within at most `ceil(N / K)` committed emissions. A conservative scheduling-age
  bound is the band interval plus `ceil(N / K) * S` ticks. Network delivery delay is separate.

### AOI and LOD boundaries

- Active AOI requires a finite authoritative interest source. A missing source returns a skipped
  result and never falls back to the complete population.
- Radius and conical FOV boundaries are inclusive. The FOV setting is a full angle in degrees.
  Zero-distance entities are retained and entities behind the source are rejected.
- For offset `d`, radius accepts `dot(d, d) <= radius^2`. FOV also requires
  `dot(forward, d) >= 0` and `dot(forward, d)^2 >= dot(d, d) * cos(fov_degrees / 2)^2`.
- Distance LOD uses Near when `distance_squared <= near_distance^2`, Medium through the inclusive
  medium boundary, and Far for the remainder of the AOI. Player self is always Near.
- Boundary oscillation can increase transmitted work. No hysteresis is applied, and oscillation
  cannot clear pending work or change canonical convergence.

### Wire and baseline contract

- The private wire header carries a magic value, protocol and schema versions, and a decode
  signature. Schema versions 5 and 6 require every Patch to carry an explicit nonzero baseline
  sequence. Decode rejects identity mismatches and stale sequences.
- Patch decoding reports the declared baseline sequence. Client storage must retain and resolve that
  exact reconstructed snapshot before applying the Patch.

### Reference FullReplace codec

The reference layout is a complete, baseline-independent `FullReplace` with baseline sequence zero.
All fields are fixed-width and written in network byte order. Float values are converted to their
standard 32-bit IEEE 754 bit patterns and written in network byte order. The codec never writes a
native C++ struct directly, so compiler padding and host byte order cannot alter the wire format.

| Field | Width | Order |
| --- | ---: | --- |
| Header | 45 bytes | magic `u32`, protocol `u16`, schema `u16`, decode signature `u64`, kind `u8`, tick `u64`, sequence `u32`, baseline sequence `u32`, upsert count `u32`, delete count `u32`, payload bytes `u32` |
| Reference entity | 30 bytes | id `u32`, classification `u8`, position x/y/z `f32`, heading x/y/z `f32`, hue `u8` |

FullReplace records are encoded in the snapshot's strictly ascending entity-ID order. The decoder
checks the complete declared payload before advancing its accepted sequence, then delegates entity
ID ordering and snapshot semantics to `simnet_snapshot`.
