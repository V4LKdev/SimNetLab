@defgroup core simnet.core
@brief Dependency-free core vocabulary.

## Exported Types

### simnet.core:bytes
- `EntityNetId`, `PeerId`, `SequenceId`
- `Byte`, `ByteSpan`, `MutableByteSpan`

### simnet.core:math
- `Vec3f`, `Aabb3f`
- Arithmetic operators, `dot`, `length`, `length_squared`, `normalize_or`
- `is_finite`, `make_centered_bounds`, `contains`

### simnet.core:time
- `Tick`, `FixedStepSettings`, `FixedStepClock`
- `fixed_dt_from_tick_rate` - returns the duration of one fixed step.
- `make_clock` - factory that properly initialises a `FixedStepClock`
  from the given settings.
- `advance` - consumes at most one frame’s worth of time.

## Notes

- All types are PoD. No heap allocations, no third-party imports.
- Hot-path math (`Vec3f` operators, `contains`) is `constexpr`.
- `length()` and `normalize_or()` use `std::sqrt` and are not `constexpr`.
- Byte spans are non-owning views. The caller must keep the referenced buffer alive.
- Use `make_clock(settings)` to avoid forgetting to set `fixed_dt`.
