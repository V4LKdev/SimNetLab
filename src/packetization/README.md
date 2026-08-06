# simnet_packetization

`simnet_packetization` splits one nonempty opaque byte group into bounded application packets and reassembles complete groups without interpreting their contents. It depends only on `simnet_core` and the standard library.

The fixed 25-byte network-order header contains `SNPK`, protocol and schema versions, packet kind, nonzero group ID, chunk index and count, complete group bytes, and current chunk bytes. Enabled packetization uses the header for one-chunk groups too. Disabled packetization returns one raw payload and rejects it when it exceeds the configured hard payload limit.

Reassembly validates all sizes and widened arithmetic before allocation. It accepts reordered and byte-identical duplicate chunks. Conflicting metadata or duplicate bytes destroy only that group. Per-peer caller-owned state bounds incomplete groups, declared retained bytes, chunk count, and lifetime. Completing a byte group does not make it canonical. The application commits the group only after decoding, reconstruction, and sink application succeed.

One missing chunk discards the utility of its complete opaque group. Reliable and unreliable
snapshot treatments recover through later complete groups, exact ACK-proven baselines, and repeated
FullReplace updates. Independently applicable entity chunks and chunk-level acknowledgements remain
a separate experimental technique.

Compression is not part of this target. Whole-update compression produces the opaque group before
packetization. Per-packet compression wraps complete serialized application packets outside this
target, then restores the original packet bytes before parsing and reassembly.
