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
-> ClientHello on Control
-> ServerAccept or ServerReject
-> PeerSessionReady
```

`PeerConnected` reports an ENet connection. `PeerSessionReady` reports a matching SimNet session identity and permits app payloads. Duplicate hello messages after readiness are protocol errors. Identity mismatches identify the first mismatching field.

## Lanes and delivery

`Control`, `Snapshot`, and `Input` map to ENet channels 0, 1, and 2. Control carries the handshake and bounded reliable opaque application-control payloads after session readiness. Snapshot carries opaque app bytes. Input carries both `SnapshotAck` messages and bounded opaque application-input payloads.

`send_application_control` and `ReceivedApplicationControl` preserve reliable application payloads without interpreting them. `send_application_input` uses ENet's unreliable-sequenced delivery and produces `ReceivedApplicationInput`; the transport still does not interpret the payload. Application schemas and authorization remain at the app boundary.

The API supports reliable sequenced, unreliable sequenced, unreliable unsequenced, and unreliable fragmented delivery. App configuration selects snapshot delivery. Current default snapshots use reliable sequenced delivery.

`TransportLimits` applies real send-time limits. `EnforceLimit` rejects oversized payloads before ENet. `AllowBackendFragmentation` passes them to ENet while preserving the hard reassembly limit.

## Snapshot acknowledgements

`SnapshotAck` reports the newest decoded sequence, receipt bits for the preceding 32 sequences, and the newest applied sequence. The current Client advances this history monotonically and ignores late packets. It is a replication acknowledgement, not an ENet reliability acknowledgement. The wire format is fixed field by field on the Input lane. Transport validates the envelope. The Server app owns history retention and delta-baseline selection.

## Threading

One owner thread uses each `TransportServer` or `TransportClient`. All lifecycle, poll, send, and disconnect calls stay on that thread. There are no background networking threads, callbacks, or hidden synchronization.

Transport integration coverage is under `tests/` and verifies handshake readiness, identity rejection, send limits, acknowledgements, disconnects, and reconnects.
