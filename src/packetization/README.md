# simnet_packetization

`simnet_packetization` splits one nonempty opaque byte group into bounded application packets and reassembles complete groups without interpreting their contents. It depends only on `simnet_core` and the standard library.

The fixed 25-byte network-order header contains `SNPK`, protocol and schema versions, packet kind, nonzero group ID, chunk index and count, complete group bytes, and current chunk bytes. Enabled packetization uses the header for one-chunk groups too. Disabled packetization returns one raw payload and rejects it when it exceeds the configured hard payload limit.

Reassembly validates all sizes and widened arithmetic before allocation. It accepts reordered and byte-identical duplicate chunks. Conflicting metadata or duplicate bytes invalidate only the affected group. Per-peer caller-owned state limits incomplete groups, retained bytes, chunk count, and lifetime.

One missing chunk prevents a complete group from being reconstructed. Complete group reconstruction does not become canonical by itself. The application commits a reconstructed group only after decoding, reconstruction, and sink application succeed.

Reliable and unreliable snapshot treatments recover through later complete groups, exact acknowledgment-proven baselines, and repeated FullReplace updates.

Compression is outside this target. Whole-update compression produces the opaque group before packetization. Per-packet compression wraps complete serialized application packets outside this target, then restores the original packet bytes before parsing and reassembly.
