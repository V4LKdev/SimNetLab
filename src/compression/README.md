# simnet_compression

`simnet_compression` transforms bounded opaque byte sequences using a versioned Raw or Zstd
envelope. It owns reusable codec contexts, exact-size validation, and typed codec reports. It has no
knowledge of snapshots, pipeline records, packet headers, transport, configuration, or rendering.

The 17-byte network-order envelope contains `SNCZ`, protocol and schema versions, an active Raw or
Zstd encoding, the declared uncompressed byte count, and the encoded payload byte count. Decoding
requires one complete frame with a known exact content size. Truncated frames, concatenated frames,
trailing bytes, corrupt data, and size mismatches are rejected before caller state changes.

Whole-update compression always uses the envelope. Per-packet composition uses a Zstd envelope only
when the complete envelope is smaller than the original application packet. Otherwise it preserves
the original packet bytes.

The retired exploratory probe evaluated only Zstd level 1 on synthetic layouts. Its directionally
useful result was that whole-buffer compression was smaller and cheaper than independently
compressed packet-sized ranges. Current pipeline layouts, AOI populations, packet headers, and
Delta semantics require fresh production benchmarking. The old probe did not record enough build
and dependency provenance for its numerical results to serve as final evidence.
