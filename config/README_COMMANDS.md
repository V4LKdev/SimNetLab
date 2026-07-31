# SimNet demo config set

## Profiles

- `shared_demo_network.json`: use for Server + Client + player demo. Small enough to replicate cleanly.
- `shared_demo_visual.json`: use for a nicer medium-size Server visual demo.
- `shared_stress_100k.json`: use for Server-only render/simulation stress. Do not present this as realistic current network replication.
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

Server-only 100k stress view:

```sh
build/relWithDebInfo/app/Server \
  --config config/server_visual.json \
  --shared-config config/shared_stress_100k.json
```
