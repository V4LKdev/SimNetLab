# simnet_transport

`simnet_transport` moves opaque bytes between ENet peers and owns the SimNet session handshake. Apps provide session identity and map configuration at their boundary.

It does not depend on config, telemetry, pipeline, snapshots, game modules, Flecs, rendering, benchmarking, or gameplay data.

```txt
public:  simnet_core
private: ENet
```

## Session lifecycle

```txt
ENet connect
-> ClientHello on lane 0
-> ServerAccept or ServerReject
-> PeerSessionReady
```

`PeerConnected` reports an ENet connection. `PeerSessionReady` reports a matching SimNet session identity and permits app payloads. Duplicate hello messages after readiness are protocol errors. Identity mismatches identify the first mismatching field.

## Lanes and delivery

`TransportLane::Lane0`, `Lane1`, and `Lane2` map directly to ENet channels 0, 1, and 2. The application owns the meaning of each lane after session readiness. Transport exposes every post-session payload as `ReceivedPacket` and accepts it through the generic send operations. It does not parse application messages, acknowledgements, packet groups, or snapshot bytes.

The API supports reliable sequenced, unreliable sequenced, unreliable unsequenced, and unreliable fragmented delivery. App configuration selects snapshot delivery. Current default snapshots use reliable sequenced delivery.

`TransportLimits` applies real send-time limits. `EnforceLimit` rejects oversized payloads before ENet. `AllowBackendFragmentation` passes them to ENet while preserving the hard reassembly limit.

## Threading

One owner thread uses each `TransportServer` or `TransportClient`. All lifecycle, poll, send, and disconnect calls stay on that thread. There are no background networking threads, callbacks, or hidden synchronization.

Transport integration coverage is under `tests/` and verifies handshake readiness, identity rejection, generic lane traffic, send limits, disconnects, and reconnects.
