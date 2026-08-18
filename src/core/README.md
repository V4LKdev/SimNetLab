# simnet_core

`simnet_core` provides the dependency-free value types used throughout SimNetLab.

## API

- `simnet.core:bytes` defines `Byte`, `ByteSpan`, and network-order byte and number operations.
- `simnet.core:ids` defines `EntityNetId`, `PeerId`, and `SequenceId`.
- `simnet.core:math` defines `Vec3f`, `Aabb3f`, vector operations, bounds, and finite checks.
- `simnet.core:time` defines `Tick`, `FixedStepSettings`, and `FixedStepClock`.

`ByteSpan` is a non-owning view. The caller must preserve the referenced storage for its complete
use. Create fixed-step clocks with `make_clock(settings)` so the configured duration is applied
consistently. `advance` consumes an accepted nonnegative duration up to the configured step cap.
