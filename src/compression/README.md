# simnet_compression

`simnet_compression` transforms bounded opaque byte sequences using a versioned Raw or ordinary
Zstd envelope. It owns reusable codec contexts, exact-size validation, and typed codec reports. It
has no knowledge of snapshots, pipeline records, packet headers, transport, configuration, or
rendering.

The 17-byte network-order envelope contains `SNCZ`, protocol and schema versions, an active Raw,
or Zstd encoding, the declared uncompressed byte count, and the encoded payload byte count.
Decoding requires one complete frame with a known exact content size. Truncated frames,
concatenated frames, trailing bytes, corrupt data, and size mismatches are rejected. Callers consume
decoded output only when the report is valid. Zstd decoding uses reusable scratch storage and
commits caller output only after complete validation and successful decoding. Every failure leaves
caller output unchanged.

Whole-update compression always uses the envelope. Per-packet composition uses a Zstd envelope only
when the complete envelope is smaller than the original application packet. Otherwise it preserves
the original packet bytes.

Compression and decompression report elapsed wall time measured with `std::chrono::steady_clock`
around the complete successful transform. The boundary begins after basic argument and limit
validation and ends after the caller's output vector contains the final bytes. It includes envelope
work, Zstd work when selected, required scratch-buffer work, Raw fallback copying, and the final
output copy.
