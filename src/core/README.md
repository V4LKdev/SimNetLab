# simnet_core

`simnet_core` provides dependency-free core vocabulary.

## Exported Types

### simnet.core:bytes
- `Byte`, `ByteSpan`
- Network-order byte, integer, and IEEE-754 binary32 append/read operations

### simnet.core:ids
- `EntityNetId`, `PeerId`, `SequenceId`

### simnet.core:math
- `Vec3f`, `Aabb3f`
- Arithmetic operators, `dot`, `cross`, `length`, `length_squared`, `normalize_or`
- `is_finite`, `make_centered_bounds`, `contains`

### simnet.core:time
- `Tick`, `FixedStepSettings`, `FixedStepClock`
- `make_clock` - factory that properly initializes a `FixedStepClock`
  from the given settings.
- `advance` - accumulates an already accepted nonnegative duration and consumes
  at most the configured number of fixed steps.

## Notes

- Core owns simple value types and non-owning byte views.
- `ByteSpan` does not own storage. Callers must preserve the referenced lifetime.
- Container allocation, protocol policy, ECS, rendering, and file/process policy are
  outside core.
- Core depends only on the C++ standard library.
- Use `make_clock(settings)` so `fixed_dt` is applied consistently.
- Elapsed-frame acceptance policy belongs to runtime, not `FixedStepClock`.
