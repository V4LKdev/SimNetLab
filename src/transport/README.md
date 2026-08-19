# simnet_transport

`simnet_transport` moves opaque application bytes between ENet peers and performs the SimNet session
handshake.

```text
public:  simnet_core
private: ENet
```

## Session lifecycle

```text
ENet connect
-> ClientHello on lane 0
-> ServerAccept or ServerReject
-> PeerSessionReady
```

`PeerConnected` reports the ENet connection. `PeerSessionReady` confirms a matching SimNet session
identity and permits application payloads. The private handshake uses protocol version 4 with
fixed-field ClientHello, ServerAccept, and ServerReject messages. Identity mismatches report the
first differing field, and duplicate hello messages after readiness are protocol errors.

## Lanes and delivery

`TransportLane::Lane0`, `Lane1`, and `Lane2` map directly to ENet channels 0, 1, and 2. After session
readiness, received payloads are exposed as `ReceivedPacket` without interpreting application
messages.

Reliable sequenced delivery maps to `ENET_PACKET_FLAG_RELIABLE`. Unreliable sequenced delivery uses
ENet channel sequencing without reliability.

`TransportLimits` rejects payloads above the configured application limit before ENet. Reliable
payloads within that limit may use ENet fragmentation. Unreliable payloads also reject sizes above
the live peer MTU after ENet protocol overhead, preventing ENet from promoting fragmentation to
reliable delivery.

## Threading

Each `TransportServer` or `TransportClient` is operated by one application-owned thread. A Server
instance handles all connected peers on that same owner thread. SimNet and ENet create no
additional networking thread.
