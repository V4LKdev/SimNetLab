`simnet_config` owns runtime configuration loaded from JSON.

Import it with:

```txt
import simnet.config
```

It exports shared, client, and server config structs, defaults, JSON loading, and fingerprints. It does not own CLI parsing, live state, logging setup, or compile-time feature switches.

Shared configuration controls deterministic seed, world and population settings, spatial acceleration, authoritative boid behavior, and supported pipeline selections. Boid settings cover independent separation/alignment/cohesion radii, FOV, speed and acceleration limits, containment, deterministic wander, circular hue behavior, and a direct toggle for each rule. The runtime derives its spatial query radius from the largest configured rule radius. Older local profiles may still use `perception_radius` as a compatibility alias for both social radii; maintained profiles should use the explicit fields.

Server-local `flecs.thread_count` selects `1..64` persistent Flecs worker threads. The default value of one preserves serial system execution. It is included in the Server runtime fingerprint but not in network compatibility.

The current application runtime supports one server peer and one client peer. Server configuration values above one client are rejected during startup.

Visualization is local-only configuration shared by Server and Client. It controls optional window creation and does not affect network compatibility. Both applications render their current authoritative or replicated state when visualization is enabled. `entity_mesh_path` is an optional local OBJ path. An empty path uses the built-in directional wedge. A load failure logs one warning and uses the same instanced wedge fallback. Debug observer radius, vertical FOV, and the maximum displayed spatial cells are local viewer settings. The authoritative spatial cell size remains in shared spatial configuration.

Benchmark, JSON telemetry export, area-of-interest, LOD, and compression configuration vocabulary is retained for planned work. The Server's small sampled boid CSV is implemented; it is tuning evidence rather than the future benchmark runner. Tracy instrumentation is controlled by the CMake build option. Unsupported pipeline selections are rejected during app startup instead of being ignored.

Network compatibility fingerprints encode shared fields in a canonical order and byte representation. Their numeric values changed from the earlier native-byte implementation.

`shared_boids_demo.json` is a conservative 1,000-entity visual profile. Its rule values are starting points, not final tuning constants. `shared_stress_100k.json` remains a high-population visual and profiling scenario rather than a realistic unpacketized ENet replication workload.
