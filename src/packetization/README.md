# simnet_packetization

`simnet_packetization` splits one nonempty byte group into bounded application packets and
reassembles complete groups without interpreting their payload.

| Configuration | Send behavior | Receive behavior |
| --- | --- | --- |
| Disabled | Sends one raw payload, rejects it above the hard size limit, and adds no application packet header. | Accepts the bounded raw payload directly. |
| Enabled | Adds a fixed `SNPK` header and sends one or more chunks. One-chunk groups still use the header. | Uses bounded per-peer reassembly until every chunk is present. |

## Header

The fixed 25-byte network-order header contains `SNPK`, protocol and schema versions, packet kind,
nonzero group ID, chunk index and count, complete group size, and current chunk size.

## Reassembly

Reassembly validates sizes and widened arithmetic before allocation. It accepts reordered and
byte-identical duplicate chunks. Conflicting metadata or bytes invalidate only the affected group.
Per-peer state bounds incomplete groups, retained bytes, chunk count, and lifetime.

A complete group becomes application input only after all chunks arrive. The application commits
it after decoding, snapshot reconstruction, and sink application succeed.

## Compression order

Whole-update compression runs before packetization. Per-packet compression wraps each complete
serialized packet, then restores the packet before header parsing and reassembly.
