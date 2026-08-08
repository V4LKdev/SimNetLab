`simnet_config` owns runtime configuration loaded from JSON.

Import it with:

```txt
import simnet.config
```

It exports shared, client, and server config structs, defaults, JSON loading, and fingerprints. It does not own CLI parsing, live state, logging setup, or compile-time feature switches.

The typed defaults match `shared_default.json`, `server_default.json`, and `client_default.json`
after semantic loading. Named visual, stress, and treatment profiles remain explicit deviations.

Shared configuration controls deterministic seed, world and population settings, spatial acceleration, authoritative boid and player movement, and supported pipeline selections. Boid settings cover independent separation/alignment/cohesion radii, FOV, speed and acceleration limits, containment, deterministic wander, circular hue behavior, and a direct toggle for each rule. The runtime derives its spatial query radius from the largest configured rule radius. Older local profiles may still use `perception_radius` as a compatibility alias for both social radii. Maintained profiles should use the explicit fields.

Server-local `flecs.thread_count` selects `1..64` persistent Flecs worker threads. The default value of one preserves serial system execution. It is included in the Server runtime fingerprint but not in network compatibility.

Server-local `transport.max_clients` accepts `1..64` simultaneous sessions. Maintained one-client
profiles remain capped at one. `server_multi_client_visual.json` is the two-client visual profile.
The capacity is included in the local runtime fingerprint but not network compatibility.

Visualization is local-only configuration shared by Server and Client. It controls optional window creation and does not affect network compatibility. `interpolation_enabled` makes the Server render between adjacent authoritative ticks and the Client render between retained reconstructed snapshots. Disabling it renders the latest exact state. `entity_mesh_path` is an optional local OBJ path. An empty path uses the built-in directional wedge. A load failure logs one warning and uses the same instanced wedge fallback. Stationary observer radius, vertical FOV, and the maximum displayed spatial cells are local viewer settings. The authoritative spatial cell size remains in shared spatial configuration.

Client-local `gameplay.role` is either `stationary_observer` or `player`. The role is negotiated after transport session readiness and is part of the Client runtime fingerprint, not network compatibility. A stationary observer receives no owned entity. Its finite `stationary_observer_position` is sent once as its locked connection position while later messages update only its orientation. A player receives the ID of one Server-owned replicated fish. `client_player_visual.json` is the maintained visual player profile. The other Client profiles remain stationary observers.

Shared `pipeline.area_of_interest` selects `none`, `radius`, or `fov`. Radius uses a positive finite
3D radius. FOV also requires a full conical angle in `(0, 180]`. These settings participate in the
network compatibility fingerprint. Maintained visual treatment profiles keep the world and boid
settings equal while changing only the AOI mode and geometry.

Shared `pipeline.level_of_detail` selects `none` or `distance_bands`. Distance bands require an
active radius or FOV AOI, positive Near and Medium distances below the AOI radius, a Medium
interval of at least two ticks, and a larger Far interval no greater than 65,535 ticks. Near
cadence is fixed at every tick. These values participate in runtime and network compatibility but
do not change the pipeline decode signature.

Shared `pipeline.send_interval_ticks` is an unsigned integer from 1 through `UINT32_MAX`. One
preserves the every-tick control. Larger values enable deterministic emission only on divisible
authoritative ticks. Cadence participates in runtime and network compatibility fingerprints but
does not change the pipeline decode signature.

Shared `pipeline.enable_quantization`, `enable_oct_heading`, and `enable_bit_packing` select the
fixed production record layouts. Octahedral heading requires quantization. Bit packing requires
both quantization and octahedral heading. Position bounds remain derived from the shared world
extent. The complete record widths are 30 bytes for Raw, 18 bytes for Quantized, and 16 bytes for
both Quantized Oct Heading and Bit Packed Quantized Oct Heading. The two 16-byte layouts preserve
identical canonical precision. Bit packing is retained as an honest neutral-size treatment.

The matched radius-AOI visual profiles are `shared_representation_raw_aoi_radius_visual.json`,
`shared_representation_quantized_aoi_radius_visual.json`,
`shared_representation_oct_heading_aoi_radius_visual.json`,
`shared_representation_bit_packed_aoi_radius_visual.json`, and
`shared_cadence_reduced_aoi_radius_visual.json`. The Raw profile is also the every-tick cadence
control. The cadence treatment emits every four authoritative ticks.

Shared `compression` selects `none`, `whole_update`, or `per_packet`. Active modes require an
explicit Zstd level from 1 through 19. Whole-update mode compresses the complete encoded update
before packetization. Per-packet mode independently compresses complete application packets after
packetization. Compression settings participate in network compatibility.

Shared `player` settings define smooth authoritative movement. Input accelerates
private yaw and pitch velocities. Damping reduces them after release, maximum
angular rates bound the turn, and `pitch_limit_degrees` bounds orientation to at
most 85 degrees so the locked chase camera stays clear of vertical singularities.
Speed continues to approach the configured slow, cruise, or boost target at
`speed_change_rate`. These are deterministic simulation settings and therefore
participate in network compatibility.

Optional shared `boids.player_lure` and `boids.player_predator` objects make authoritative
Players influence nearby Boids. Disabled objects contain only `enabled: false`. Enabled objects
require a positive finite `radius` and `max_acceleration`. Radius is capped at twice the world half
extent. Force acceleration is capped by the ordinary Boid maximum acceleration. Predator force is
part of the safety budget and lure force is part of the remaining social budget. Both treatments
are Server-authoritative, disabled by default, and included in network compatibility.

`telemetry.log_directory` owns enabled log files and CSV evidence files. Logging remains controlled
independently by `console_log_enabled`, `file_log_enabled`, and `min_level`.
`metrics_csv_enabled` controls the Server replication CSV and sampled boid CSV on Server. It
controls the Client replication CSV on Client. When disabled, the CSV path creates no directory or
evidence file. Enabled file logging may independently create `log_directory`.

Benchmark configuration vocabulary is retained for planned work. Temporal distance LOD is an
implemented independent Patch-producing pipeline selection. The Server's small sampled boid CSV
is tuning evidence rather than the future benchmark runner. Tracy instrumentation is controlled
by the CMake build option. Unsupported pipeline selections are rejected during app startup
instead of being ignored.

Network compatibility fingerprints encode shared fields in a canonical order and byte representation. Their numeric values changed from the earlier native-byte implementation.

`shared_boids_demo.json` is a conservative 1,000-entity visual profile. Its rule values are starting points, not final tuning constants. `shared_stress_50k.json` is the local high-count stress and visual-inspection profile. It is a Server-only scenario rather than a realistic unpacketized ENet replication workload.
