# SimNet network snapshot compression probe

This is a deliberately small C++23 benchmark for deciding whether snapshot compression is worth integrating before implementing it in SimNet.

## What it compares

For the same deterministic boid updates, it generates three entity encodings:

1. `full_f32`
   - `uint32_t id`
   - `float position[3]`
   - `float heading[3]`
   - `uint8_t hue`
   - 29 packed wire bytes per entity. A directly copied C++ struct may be 32 bytes because of padding.

2. `full_quantized`
   - `uint32_t id`
   - three 16-bit bounded-world positions
   - two 16-bit octahedral heading components
   - `uint8_t hue`
   - 15 wire bytes per entity.

3. `delta_quantized`
   - `uint32_t id`
   - one field-change mask
   - ZigZag/ULEB128 deltas for quantized position and octahedral heading
   - hue only when changed
   - variable-sized records.

Each encoding is tested as:

- uncompressed, independently packetized records
- plain Zstd over the whole snapshot, then fragmented
- plain Zstd independently per packet
- trained-dictionary Zstd over the whole snapshot, then fragmented
- trained-dictionary Zstd independently per packet

The dictionaries are trained on separate simulation trajectories and packet-sized samples. This is the practical interpretation of the proposed "pre-trained static dictionary" technique. It is not a separate hand-written static Huffman codec. Zstd dictionaries initialize both history and entropy-compression state.

## Arch Linux setup

```bash
sudo pacman -S --needed base-devel pkgconf zstd
```

## Run

```bash
chmod +x run_network_probe.sh
./run_network_probe.sh
```

Default assumptions:

- 2,000 boids
- 20 Hz updates (`dt = 0.05`)
- MTU 1,500 bytes
- 300 bytes reserved as conservative transport/protocol headroom
- 1,200-byte maximum compressed or uncompressed application payload
- Zstd level 1
- 8 KiB dictionary
- 1% illustrative independent packet-loss probability

A more thesis-like run:

```bash
./run_network_probe.sh \
  --entities 10000 \
  --train-ticks 120 \
  --test-ticks 120 \
  --repeats 5 \
  --mtu 1500 \
  --headroom 300 \
  --zstd-level 1 \
  --dict-size 8192 \
  --loss 0.01 \
  --csv results/network_probe.csv
```

Create the output directory first when selecting one:

```bash
mkdir -p results
```

## How to read the table

- `src B`: encoded bytes before compression.
- `out B`: transmitted application payload bytes after compression.
- `ratio`: `out B / src B`. Lower is better.
- `pkts`: packets or fragments needed under the configured payload ceiling.
- `boids/pkt`: average entities represented by each packet.
- `comp us`, `decomp us`: mean snapshot cost on the machine running the probe, including output-buffer allocation.
- `complete%`: probability every packet arrives without loss under the illustrative loss rate.
- `usable%`:
  - whole-snapshot compression: same as `complete%`, because an incomplete compressed frame cannot be decoded.
  - per-packet compression: expected independently usable entity fraction, approximately `1 - packet_loss`.

With reliable ENet delivery, a missing fragment normally causes retransmission and delay rather than permanent data loss. The whole compressed snapshot is not "garbage". It is simply unavailable until complete. The table's loss model is therefore most directly relevant to unreliable snapshot delivery, while `complete%` can also be read as the chance of avoiding retransmission delay.

## Decision rule

Compression is promising only when its saved packet count or bandwidth is meaningful after considering CPU cost and failure isolation.

Likely outcomes to look for:

- If quantized/delta records already approach entropy, plain per-packet Zstd may save too little to justify integration.
- Dictionary Zstd is most likely to help packet-sized payloads because ordinary compressors have little prior context on small inputs.
- Whole-snapshot compression should usually produce the smallest byte count, but couples all fragments to one decodable frame.
- Per-packet dictionary compression is the strongest compromise when it approaches whole-frame size while preserving packet-level loss isolation.

## Moving from synthetic data to SimNet

For an actual thesis result, keep the packet/compression measurement code and replace the three synthetic encoder calls with byte buffers produced by the real SimNet pipeline. Feed representative snapshots from several simulation seeds into dictionary training, then benchmark different seeds and scenarios. Do not train and test on the same captured run.
