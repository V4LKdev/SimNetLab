`simnet_config` owns runtime configuration loaded from JSON.

Import it with:

```txt
import simnet.config
```

It exports shared, client, and server config structs, defaults, JSON loading, and fingerprints. It does not own CLI parsing, live state, logging setup, or compile-time feature switches.

Shared configuration currently controls world bounds, initial entity count, and supported pipeline selections. It also declares deterministic, spatial, and planned technique settings that are retained for upcoming work. Server and Client transport configuration selects ENet address, payload policy, and snapshot delivery.

The current application runtime supports one server peer and one client peer. Server configuration values above one client are rejected during startup.

Render, benchmark, spatial, telemetry export, Tracy, area-of-interest, LOD, and compression configuration vocabulary is retained for planned work. Unsupported pipeline selections are rejected during app startup instead of being ignored.
