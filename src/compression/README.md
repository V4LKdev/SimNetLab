# simnet_compression

`simnet_compression` transforms bounded byte sequences with a versioned Raw or ordinary Zstd
envelope. It provides reusable codec contexts, exact-size validation, and transform reports.

## Envelope

The 17-byte network-order envelope starts with the four-byte `SNCZ` magic, followed by protocol and
schema versions, the Raw or Zstd encoding, the declared uncompressed size, and the encoded payload
size. Decoding requires one complete frame with an exact known content size. Truncated frames,
concatenated frames, trailing bytes, corrupt data, and size mismatches are rejected. Caller output
changes only after successful validation and decoding.

## Placement

Whole-update compression always uses an envelope. Per-packet compression uses a Zstd envelope only
when the complete envelope is smaller than the original application packet. Otherwise it preserves
the original packet bytes.

## Timing

Compression and decompression report elapsed wall time measured with
`std::chrono::steady_clock` around the complete successful transform. This includes envelope work,
codec work, scratch-buffer work, Raw copying, and construction of the final output vector.
