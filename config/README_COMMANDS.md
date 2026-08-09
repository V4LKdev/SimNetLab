# SimNet demo config set

## Profiles

- `shared_demo_network.json`: use for Server + Client + player demo. Small enough to replicate cleanly.
- `shared_demo_visual.json`: use for a nicer medium-size Server visual demo.
- `shared_aoi_radius_visual.json`: radius AOI treatment using the visual demo world.
- `shared_aoi_fov_visual.json`: 3D conical FOV AOI treatment using the same visual demo world.
- `shared_packetization_aoi_radius_visual.json`: forced 256-byte multipacket stress treatment.
- `shared_compression_none_aoi_radius_visual.json`: 1,200-byte compression comparison control.
- `shared_compression_whole_aoi_radius_visual.json`: whole-update Zstd comparison treatment.
- `shared_compression_per_packet_aoi_radius_visual.json`: per-packet Zstd comparison treatment.
- `shared_delivery_reliable_aoi_radius_visual.json`: matched reliable sequenced delivery control.
- `shared_delivery_unreliable_aoi_radius_visual.json`: matched unreliable sequenced delivery treatment.
- `shared_lod_none_aoi_radius_visual.json`: matched distance-LOD control with Incremental disabled.
- `shared_lod_distance_bands_aoi_radius_visual.json`: temporal distance-LOD treatment with 40, 100, 4, and 16 band values.
- `shared_player_influence_control_visual.json`: matched Player influence control with lure and predator disabled.
- `shared_player_lure_visual.json`: authoritative Player lure treatment using a 35-unit radius and 5-unit acceleration limit.
- `shared_player_predator_visual.json`: authoritative Player predator treatment using a 24-unit radius and 10-unit acceleration limit.
- `shared_stress_50k.json`: local high-count stress and visual-inspection profile. Use it only with the Server. Do not present it as realistic current network replication.
- `shared_synthetic_delta_full_change.json`: full-change synthetic Delta control.
- `shared_synthetic_delta_sparse_entities.json`: 12.5 percent entity cohort treatment for whole-record Delta.
- `shared_synthetic_delta_sparse_fields_whole_record.json`: 12.5 percent position-only treatment with whole-record Delta.
- `shared_synthetic_delta_sparse_fields_field_mask.json`: matched position-only treatment with field-mask Delta.
- `shared_default.json`: conservative fallback baseline.

## Commands

From the repository root:

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

Network + player demo, in two terminals:

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

Stationary observer client instead of player:

```sh
build/relWithDebInfo/app/Client \
  --config config/client_visual.json \
  --shared-config config/shared_demo_network.json
```

Radius AOI treatment, using the stationary observer Client in two terminals:

```sh
build/relWithDebInfo/app/Server \
  --config config/server_visual.json \
  --shared-config config/shared_aoi_radius_visual.json
```

```sh
build/relWithDebInfo/app/Client \
  --config config/client_visual.json \
  --shared-config config/shared_aoi_radius_visual.json
```

Use `shared_aoi_fov_visual.json` in both commands for the conical FOV treatment.

Headless synthetic treatments use a Server built with `SIMNET_ENABLE_SYNTHETIC=ON`, the stationary observer Client, and the same shared profile in both processes. Start with the full-change control and substitute each other synthetic profile for its matched treatment:

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
