## Core
- Bandwidth (bytes per second & bytes per sim-tick).
- Packet count / size.
- Entity count.
- CPU time per tick plus breakdown into sim, serialization, compression.
- Determinism: snapshot equality between runs with same technique, spatial divergence, anisotropy drift.

## Nice to Have
- Memory allocation rate (transient vs persistent).
- Garbage collection pauses if any.
- Thread contention ratio.
- Performance spike count.
- Snapshot delivery ratio.
- Jitter / Standard deviation of send rate.
- Compression ratio per technique.
- Client CPU.

Later exported to CSV or JSON



