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

The handshake payload is protocol version 4 and uses fixed-field message shapes for client hello, server accept, and server reject.

## Lanes and delivery

`TransportLane::Lane0`, `Lane1`, and `Lane2` map directly to ENet channels 0, 1, and 2. The application owns the meaning of each lane after session readiness. Transport exposes every post-session payload as `ReceivedPacket` and accepts it through generic send operations. It does not parse application messages, acknowledgments, packet groups, or snapshot bytes.

The API exposes only reliable sequenced and unreliable sequenced delivery. Reliable sequenced maps
to `ENET_PACKET_FLAG_RELIABLE`. Unreliable sequenced maps to flags zero and therefore retains ENet's
per-channel sequencing without reliability. Application configuration selects snapshot delivery.

`TransportLimits` rejects payloads above the configured application limit before ENet. Reliable
sends within that limit may use ENet's internal fragmentation. Unreliable sequenced sends also
reject payloads above the live peer MTU after public ENet protocol overhead because ENet would
otherwise promote fragmentation to reliable delivery.

## Threading

One owner thread uses each `TransportServer` or `TransportClient`. All lifecycle, poll, send, and disconnect calls stay on that thread. There are no background networking threads, callbacks, or hidden synchronization.

Transport integration coverage is under `tests/` and verifies handshake readiness, identity rejection, lane traffic, limits, disconnects, timeout behavior, and reconnects.

Lanes follow ENet channel ordering per protocol version 4 session rules.
Reliable and unreliable delivery are session-transported application payload paths only.
Transport remains byte-opaque.
Disconnect events are application-visible commit points for lifecycle ownership changes.
Diagnostic events continue to report recovery-relevant failures.
