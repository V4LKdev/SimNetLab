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

## Dictionary training corpus capture

The Server developer option `--compression-corpus-dir PATH` captures each complete production
`EncodedUpdate` immediately before whole-update compression. The option requires `whole_update`
compression and is disabled by default. The destination may be absent or empty. An existing
nonempty destination is rejected. Each sample is written as an exclusive binary file.
`manifest.csv` records the run, tick, sequence, snapshot kind, representation, active pipeline
techniques, seed, entity counts, byte count, and SHA-256 of that exact sample.

The destination belongs outside the repository. The production collector does not select a
training matrix or train a dictionary. A temporary external harness owns profile generation,
matrix completion, corpus validation, and the later Zstd training command.

Capture performs synchronous hashing and file IO outside the compression timer. Corpus capture
runs collect training data and are not performance evidence.
