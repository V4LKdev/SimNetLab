# simnet_transport

`simnet_transport` moves opaque byte payloads between peers and owns the small SimNet session handshake.

It does not understand config files, telemetry, pipeline packets, snapshots, Flecs worlds, rendering, benchmarking, or gameplay data. Apps compute compatibility values and pass them in as opaque `SessionIdentity` fields.

Allowed dependencies:

```txt
public:  simnet_core
private: ENet
```

Forbidden dependencies include config, telemetry, pipeline, snapshot, game modules, synthetic data, spatial indexing, render, Flecs, and Raylib.

## Backends

The public settings carry an explicit backend choice:

```txt
ENet      -> implemented reference backend
LocalIpc  -> Linux/POSIX Unix-domain socket backend
```

Selecting an unavailable backend returns `UnsupportedBackend`; it must not silently fall back to ENet. The app config maps backend strings at the app boundary, keeping transport independent from config parsing.

`SIMNET_ENABLE_LOCAL_IPC` builds the Unix-domain socket backend on Linux/POSIX. It defaults on for Linux and off elsewhere. Windows named pipes and shared memory are intentionally not implemented yet.

## Lanes

Three lanes map directly to ENet channels:

```txt
Control  -> channel 0
Snapshot -> channel 1
Input    -> channel 2
```

Control is used for handshake messages and uses reliable sequenced delivery internally. Snapshot carries opaque app payloads. Input is reserved for semantic `SnapshotAck` messages in this phase. Future client input can extend the private session protocol.

## Delivery

The public API exposes the relevant delivery modes:

```txt
ReliableSequenced
UnreliableSequenced
UnreliableUnsequenced
UnreliableFragmented
```

If a future backend cannot support one honestly, it should reject that send mode instead of silently faking semantics.

`LocalIpc` is reliable stream IPC. It supports `ReliableSequenced` and rejects unreliable delivery modes with `UnsupportedDelivery`.

## Send Size Policy

`TransportLimits` are real send-time behavior, separate from the pipeline's soft packet budget.

`EnforceLimit` rejects oversized sends before reaching the backend and increments `oversize_drops`. `AllowBackendFragmentation` passes the payload to the backend.

## Session Handshake

ENet connection is not the same as SimNet session readiness.

```txt
ENet connect
-> ClientHello on Control
-> ServerAccept or ServerReject
-> PeerSessionReady
```

`PeerConnected` means the backend connection exists. `PeerSessionReady` means the SimNet session identity matched and app payloads may be sent.

Duplicate `ClientHello` after readiness is treated as a protocol error and disconnects the peer with `ProtocolMismatch`.

Identity mismatches report a specific disconnect code for the first mismatching field: application protocol, compatible config, pipeline decode signature, then capabilities.

`poll` appends events to the caller-owned event vector. If ENet reports a backend service error after earlier events were appended, those events remain available and `poll` returns `BackendError`.

Client disconnect performs a bounded graceful ENet disconnect before destroying its host. It never starts a background worker or waits without a deadline.

## Snapshot Acknowledgements

`SnapshotAck` reports the newest snapshot sequence decoded by the client, a 32-bit history mask, and the newest sequence applied to client state. These are semantic replication acknowledgements, not ENet reliability acknowledgements.

ACK messages use a fixed field-by-field wire format on the Input lane. Phase 4 sends them reliably for deterministic smoke verification. Future cumulative runtime ACK traffic may use unreliable sequenced delivery. Transport validates only the wire envelope. Apps own sequence-history and baseline semantics.

The Server app now uses acknowledged `newest_applied_snapshot` values to promote retained snapshots as delta baselines. That baseline selection remains app replication state, not transport state.

## Threading

One thread owns each `TransportServer` or `TransportClient`. All `start`/`connect`, `poll`, `send`, and `disconnect` calls must happen from that owner thread.

There is no background networking thread, no callbacks, and no hidden synchronization in this first backend.

## Future Work

Kept smoke coverage lives in the `TransportSmoke` executable. It verifies local session readiness, identity rejection, send-size enforcement, unavailable-backend rejection, and LocalIpc behavior when compiled in, without importing config, telemetry, pipeline, or game modules.

Server and Client map their config to `TransportLimits` and snapshot delivery mode at the app boundary. Reliable sequenced snapshots remain the default deterministic smoke path; unreliable sequenced snapshots can be selected for experiments.

Shared-memory backends are future work and must preserve the same byte/session contract.
