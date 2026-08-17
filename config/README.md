# SimNet configuration reference

SimNet loads one shared JSON file and one role-local JSON file. The Server uses a Server-local
file. The Client uses a Client-local file. Missing sections and fields retain the C++ defaults.
The shipped default files express those same defaults explicitly. Every root and nested object
rejects keys outside the current contract.

The default paths are:

- `config/shared_default.json`
- `config/server_default.json`
- `config/client_default.json`

Named profiles are complete experiment inputs. Use the same shared profile for both processes in
a connected run. Shared settings participate in the network compatibility fingerprint. Role-local
settings participate only in that process's runtime fingerprint unless noted otherwise.

## Shared configuration

### `run`

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `seed` | Unsigned 64-bit integer | `12345` | Deterministic run seed |

### `simulation`

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `tick_rate_hz` | Number greater than zero | `60` | Authoritative ticks per second |
| `world_half` | Number greater than zero | `220` | Half extent of the cubic world |
| `initial_boid_count` | Unsigned 32-bit integer, including zero | `1000` | Initial Flecs boids or synthetic entities |

### `spatial`

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `cell_size` | Number greater than zero | `18` | Authoritative spatial-grid cell width |
| `max_neighbors` | Unsigned 32-bit integer greater than zero | `64` | Maximum neighbors considered per boid |

### `boids`

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `enable_separation` | Boolean | `true` | Enables separation force |
| `enable_alignment` | Boolean | `true` | Enables alignment force |
| `enable_cohesion` | Boolean | `true` | Enables cohesion force |
| `enable_containment` | Boolean | `true` | Enables world containment force |
| `enable_wander` | Boolean | `true` | Enables deterministic wander force |
| `enable_hue_assimilation` | Boolean | `true` | Enables neighbor hue assimilation |
| `enable_hue_drift` | Boolean | `true` | Enables deterministic hue drift |
| `min_speed` | Number at least zero and no greater than `cruise_speed` | `6` | Minimum boid speed |
| `cruise_speed` | Number from `min_speed` through `max_speed` | `8` | Target boid speed |
| `max_speed` | Number greater than zero and at least `cruise_speed` | `10` | Maximum boid speed |
| `max_acceleration` | Number greater than zero | `12` | Total acceleration bound |
| `separation_radius` | Number greater than zero | `3.6` | Separation neighborhood radius |
| `alignment_radius` | Number greater than zero | `12` | Alignment neighborhood radius |
| `cohesion_radius` | Number greater than zero | `24` | Cohesion neighborhood radius |
| `field_of_view_degrees` | Number in `(0, 360]` | `240` | Boid perception angle |
| `containment_prediction_seconds` | Number greater than zero | `0.75` | Containment look-ahead time |
| `containment_margin` | Number greater than zero | `22.5` | Inner containment margin |
| `separation_acceleration` | Number at least zero | `10` | Separation force limit |
| `containment_acceleration` | Number at least zero | `9` | Containment force limit |
| `alignment_acceleration` | Number at least zero | `1.2` | Alignment force limit |
| `cohesion_acceleration` | Number at least zero | `2` | Cohesion force limit |
| `wander_acceleration` | Number at least zero | `0.55` | Wander force limit |
| `wander_frequency_hz` | Number greater than zero | `0.35` | Wander frequency |
| `hue_assimilation_rate` | Number greater than zero | `0.05` | Neighbor hue blend rate |
| `hue_drift_rate` | Number greater than zero | `0.008` | Independent hue drift rate |

`player_lure` and `player_predator` are optional nested objects. Each accepts only the following
keys:

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `enabled` | Boolean and required when the object is present | `false` | Enables the Player influence |
| `radius` | Positive finite number required only when enabled | `0` | Influence radius, no greater than twice `simulation.world_half` |
| `max_acceleration` | Positive finite number required only when enabled | `0` | Influence bound, no greater than `boids.max_acceleration` |

A disabled influence rejects `radius` and `max_acceleration`.

### `player`

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `cruise_speed` | Finite number from `slow_speed` through `boost_speed` | `8` | Normal Player speed |
| `boost_speed` | Finite number greater than zero and at least `cruise_speed` | `14` | Boost target speed |
| `slow_speed` | Finite number from zero through `cruise_speed` | `3` | Slow target speed |
| `speed_change_rate` | Positive finite number | `12` | Speed approach rate |
| `yaw_acceleration_degrees` | Positive finite number | `360` | Yaw angular acceleration |
| `pitch_acceleration_degrees` | Positive finite number | `300` | Pitch angular acceleration |
| `yaw_damping` | Positive finite number | `8` | Released yaw damping |
| `pitch_damping` | Positive finite number | `8` | Released pitch damping |
| `max_yaw_rate_degrees` | Positive finite number | `120` | Maximum yaw rate |
| `max_pitch_rate_degrees` | Positive finite number | `90` | Maximum pitch rate |
| `pitch_limit_degrees` | Finite number in `(0, 85]` | `80` | Absolute pitch limit |

### `synthetic`

The optional `synthetic` object selects the synthetic producer instead of the Flecs world. When
present, all three keys are required and no other keys are accepted.

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `pattern` | `random_uniform` or `grid` | Absent object uses Flecs | Deterministic spatial pattern |
| `entity_change_fraction` | Finite number in `[0, 1]` | `1` inside an active object | Fraction serviced per tick |
| `field_change_mode` | `all`, `transform`, `position_only`, or `heading_only` | `all` inside an active object | Canonical field groups copied from each serviced candidate |

Synthetic mode requires a Server built with `SIMNET_ENABLE_SYNTHETIC=ON`. It rejects Server
visualization and Player clients. Stationary observer clients can render the reconstructed
snapshot. Omitting the object preserves the Flecs producer and the default fingerprints.

### `pipeline`

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `send_interval_ticks` | Unsigned 32-bit integer from `1` through `4294967295` | `1` | Snapshot emission cadence |
| `enable_incremental` | Boolean | `false` | Enables incremental selection |
| `enable_quantization` | Boolean | `false` | Enables bounded position and hue quantization |
| `enable_oct_heading` | Boolean | `false` | Enables octahedral heading encoding and requires quantization |
| `enable_delta` | Boolean | `false` | Enables acknowledged Delta patches |
| `enable_delta_field_mask` | Boolean | `false` | Enables Delta field masks and requires Delta |
| `enable_bit_packing` | Boolean | `false` | Enables bit-packed representation and requires quantization plus octahedral heading |

`area_of_interest` accepts only the keys valid for its selected mode.

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `mode` | `none`, `radius`, or `fov` | `none` | AOI selection mode |
| `radius` | Positive finite number required for `radius` and `fov` | `0` while inactive | 3D interest radius |
| `fov_degrees` | Finite number in `(0, 180]` required only for `fov` | `0` while inactive | Full conical field of view |

`none` rejects geometry fields. `radius` rejects `fov_degrees`.

`level_of_detail` accepts only the keys valid for its selected mode.

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `mode` | `none` or `distance_bands` | `none` | Temporal distance-LOD mode |
| `near_distance` | Positive finite number required for `distance_bands` | `0` while inactive | Every-tick band boundary |
| `medium_distance` | Positive finite number greater than `near_distance` | `0` while inactive | Medium band boundary |
| `medium_interval_ticks` | Unsigned 32-bit integer at least `2` | `0` while inactive | Medium band cadence |
| `far_interval_ticks` | Unsigned integer greater than the medium interval and at most `65535` | `0` while inactive | Far band cadence |

`distance_bands` requires an active radius or FOV AOI. Its `medium_distance` must remain below the
AOI radius. `none` rejects every band field.

### `snapshot_delivery`

Both keys are required when this object is present.

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `mode` | `reliable_sequenced` or `unreliable_sequenced` | `reliable_sequenced` | Snapshot transport delivery policy |
| `full_replace_after_unacknowledged_updates` | Unsigned integer from `1` through `63` | `32` | Bounded recovery threshold |

### `compression`

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `mode` | `none`, `whole_update`, or `per_packet` | `none` | Compression placement |
| `level` | Integer from `1` through `19`, required for active modes | `1` internally | Zstd compression level |

`none` rejects `level`.

### `packetization`

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `enabled` | Boolean | `true` | Enables 25-byte application packet headers and chunking |
| `max_payload_bytes` | Positive unsigned integer, at least `26` when enabled | `1200` | Hard application packet size |
| `max_update_bytes` | Unsigned integer from `1` through `4194304` | `4194304` | Maximum encoded byte group |
| `max_chunks_per_update` | Unsigned integer from `1` through `4096` | `4096` | Maximum chunks per update |
| `max_in_flight_updates` | Unsigned integer from `1` through `64` | `4` | Retained incomplete update limit |
| `max_incomplete_bytes` | Unsigned integer from `max_update_bytes` through `8388608` | `8388608` | Reassembly byte budget |
| `reassembly_timeout_ms` | Positive unsigned 32-bit integer | `5000` | Incomplete update lifetime |

When packetization is enabled, `(max_payload_bytes - 25) * max_chunks_per_update` must cover
`max_update_bytes`. For both roles, the packetization payload limit must not exceed
`transport.max_payload_bytes`.

## Server-local configuration

### `transport`

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `host` | String | `127.0.0.1` | Bind or connection host as used by the role |
| `port` | Unsigned 16-bit integer from `1` through `65535` | `7777` | ENet port |
| `max_clients` | Unsigned integer from `1` through `64` | `1` | Session capacity |
| `max_payload_bytes` | Positive unsigned 32-bit integer | `1200` | Transport send-size limit |

The same `transport` object and constraints are available in Client-local configuration.

### `flecs`

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `thread_count` | Unsigned integer from `1` through `64` | `1` | Persistent Flecs worker count |

### `visualization`

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `enabled` | Boolean | `false` | Creates the local viewer when render support is built |
| `interpolation_enabled` | Boolean | `true` | Interpolates between retained snapshots |
| `window_width` | Positive unsigned integer | `1800` | Window width in pixels |
| `window_height` | Positive unsigned integer | `1080` | Window height in pixels |
| `panel_width` | Unsigned integer less than `window_width` | `420` | Side-panel width in pixels |
| `target_fps` | Positive unsigned integer | `60` | Viewer frame target |
| `entity_scale` | Number greater than zero | `1` | Rendered entity scale |
| `picking_radius` | Number greater than zero | `1` | Picking hit radius |
| `stationary_observer_interest_radius` | Number greater than zero | `150` | Local observer guide radius |
| `stationary_observer_vertical_fov_degrees` | Number in `(0, 180)` | `60` | Local observer guide FOV |
| `max_visible_spatial_cells` | Positive unsigned integer | `2048` | Debug cell display cap |
| `entity_mesh_path` | String, where empty selects the built-in mesh | Empty string | Optional local OBJ path |

The Server and Client accept the same visualization keys. The Server default file omits the two
stationary observer guide fields but retains their C++ defaults. Visualization is local and does
not affect network compatibility.

### `telemetry`

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `console_log_enabled` | Boolean | `true` | Enables console logging |
| `file_log_enabled` | Boolean | `true` | Enables `simnet.log` |
| `log_directory` | String path | `logs` | Directory for enabled logs and CSV evidence |
| `min_level` | `trace`, `debug`, `info`, `warn`, `error`, `critical`, or `off` | `info` | Exact lowercase logging threshold |
| `metrics_csv_enabled` | Boolean | `true` | Enables role-specific replication CSV output |

The Client accepts the same telemetry object. `log_directory` does not participate in runtime
fingerprints. The other telemetry fields do.

## Client-local configuration

Client-local files accept `transport`, `visualization`, and `telemetry` exactly as documented
above. They also accept `gameplay`.

### `gameplay`

| Key | Type and accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `role` | `stationary_observer` or `player` | `stationary_observer` | Role requested after session readiness |
| `stationary_observer_position` | Array of exactly three finite numbers | `[0, 0, 0]` | Locked observer position |

The gameplay role and observer position affect the Client runtime fingerprint but not network
compatibility. Synthetic mode accepts only stationary observers.

## Validation and fingerprint notes

JSON roots and named sections must be objects. Every shared, Server-local, Client-local, and nested
object rejects unknown keys. Invalid enumerated values are rejected instead of being replaced by a
default.

All shared fields affect Server and Client runtime fingerprints. They also affect the canonical
network compatibility fingerprint, with two compatibility-preserving exceptions. An absent
`synthetic` object adds no synthetic marker. Disabled `enable_delta_field_mask` adds no field-mask
marker.

Server-local runtime fingerprints include transport except `log_directory`, Flecs, visualization,
and telemetry except `log_directory`. Client-local runtime fingerprints include transport except
`log_directory`, gameplay, visualization, and telemetry except `log_directory`.

## Maintained profiles

Shared controls and treatments:

- `shared_default.json`
- `shared_demo_network.json`
- `shared_demo_visual.json`
- `shared_stress_50k.json`
- `shared_aoi_radius_visual.json`
- `shared_aoi_fov_visual.json`
- `shared_cadence_reduced_aoi_radius_visual.json`
- `shared_lod_none_aoi_radius_visual.json`
- `shared_lod_distance_bands_aoi_radius_visual.json`
- `shared_representation_raw_aoi_radius_visual.json`
- `shared_representation_quantized_aoi_radius_visual.json`
- `shared_representation_oct_heading_aoi_radius_visual.json`
- `shared_representation_bit_packed_aoi_radius_visual.json`
- `shared_packetization_aoi_radius_visual.json`
- `shared_delivery_reliable_aoi_radius_visual.json`
- `shared_delivery_unreliable_aoi_radius_visual.json`
- `shared_delta_whole_record_aoi_radius_visual.json`
- `shared_delta_field_mask_aoi_radius_visual.json`
- `shared_compression_none_aoi_radius_visual.json`
- `shared_compression_whole_aoi_radius_visual.json`
- `shared_compression_per_packet_aoi_radius_visual.json`
- `shared_compression_zstd_delta_field_mask_aoi_radius_visual.json`
- `shared_player_influence_control_visual.json`
- `shared_player_lure_visual.json`
- `shared_player_predator_visual.json`
- `shared_synthetic_delta_full_change.json`
- `shared_synthetic_delta_sparse_entities.json`
- `shared_synthetic_delta_sparse_fields_whole_record.json`
- `shared_synthetic_delta_sparse_fields_field_mask.json`

Role-local profiles:

- `server_default.json`
- `server_visual.json`
- `server_multi_client_visual.json`
- `client_default.json`
- `client_visual.json`
- `client_player_visual.json`

The four synthetic profiles are matched headless treatments. The full-change profile is the
control. The sparse-entity profile changes a 12.5 percent cohort with whole-record Delta. The
sparse-field profiles use the same cohort and position-only changes, once with whole-record Delta
and once with field-mask Delta.

## Common commands

Configure and build from the repository root:

```sh
cmake --preset relWithDebInfo
cmake --build --preset relWithDebInfo
```

Server-only visual demo:

```sh
build/relWithDebInfo/app/Server \
  --config config/server_visual.json \
  --shared-config config/shared_demo_visual.json
```

Network and Player demo, in two terminals:

```sh
build/relWithDebInfo/app/Server \
  --config config/server_visual.json \
  --shared-config config/shared_demo_network.json
```

```sh
build/relWithDebInfo/app/Client \
  --config config/client_player_visual.json \
  --shared-config config/shared_demo_network.json
```

Use `client_visual.json` for a stationary observer Client. Use the same AOI, LOD,
representation, delivery, compression, or packetization shared profile in both processes for its
named treatment.

Synthetic treatments require a build configured with `SIMNET_ENABLE_SYNTHETIC=ON`. Start the
full-change control in two terminals and substitute the same other synthetic profile in both
commands for each treatment:

```sh
build/release/app/Server \
  --config config/server_default.json \
  --shared-config config/shared_synthetic_delta_full_change.json
```

```sh
build/release/app/Client \
  --config config/client_default.json \
  --shared-config config/shared_synthetic_delta_full_change.json
```

Local 50k stress and visual inspection:

```sh
build/relWithDebInfo/app/Server \
  --config config/server_visual.json \
  --shared-config config/shared_stress_50k.json
```
