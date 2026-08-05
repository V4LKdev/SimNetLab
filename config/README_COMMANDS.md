# SimNet demo config set

## Profiles

- `shared_demo_network.json`: use for Server + Client + player demo. Small enough to replicate cleanly.
- `shared_demo_visual.json`: use for a nicer medium-size Server visual demo.
- `shared_aoi_radius_visual.json`: radius AOI treatment using the visual demo world.
- `shared_aoi_fov_visual.json`: 3D conical FOV AOI treatment using the same visual demo world.
- `shared_stress_50k.json`: local high-count stress and visual-inspection profile. Use it only with the Server. Do not present it as realistic current network replication.
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

Local 50k stress and visual inspection:

```sh
build/relWithDebInfo/app/Server \
  --config config/server_visual.json \
  --shared-config config/shared_stress_50k.json
```
