# pipeline_v1 Zstd dictionary provenance

## Identity

- Capture source commit: `726fb8a99b18b685fcc4c4ab100c4e6d5fe9a889`
- Zstd CLI version: `1.5.7`
- Linked production Zstd library version: `1.5.7`
- Dictionary byte count: `16384`
- Dictionary ID: `0x534E0001` or `1397620737`
- Dictionary SHA-256: `e74c0e4559ddb462cfe0384947223d1df52f987e230294950642ad60c60a1815`
- Runtime FNV-1a 64-bit content fingerprint: `0x5fe43e7c3e7804a1`

## Training corpus

- Representations: Raw, Quantized, Quantized Oct Heading, Bit Packed Quantized Oct Heading
- Update forms: FullReplace, whole-record Delta Patch, field-mask Delta Patch
- Initial entity counts: 500, 1500, 5000
- Training seeds: 41001, 41002
- Samples per matrix cell: 12
- Matrix cells: 72
- Selected sample count: 864
- Selected source byte count: 39763970
- Ordered selected-manifest SHA-256:
  `ff3a9eaf865fcc423e499da3cb5f0b0bb41fcbb9ff7a9f75ffa024639637f055`

Every selected file matched the byte count and SHA-256 in its production capture manifest. Patch
samples had an explicit nonzero acknowledged baseline sequence. The Zstd CLI reported 38499306
loaded training bytes for the same 864 selected files under its default sample loading behavior.
Raw samples, generated configurations, logs, and the ordered manifest remain outside the repository.

## Training command

The sample arguments were read from the frozen manifest and sorted lexicographically by complete
sample path. The command was run once with `LC_ALL=C`.

```sh
LC_ALL=C zstd \
  --train-cover=k=64,d=8,steps=1,split=100 \
  --maxdict=16384 \
  --dictID=1397620737 \
  -o pipeline_v1.zdict \
  <864 lexicographically ordered selected sample files>
```

## Evaluation separation

- Reserved feature-acceptance seeds: 51001 through 51004
- Reserved final-evaluation seeds: 61001 through 61020
- Training and evaluation seeds and trajectories are disjoint.
- No final benchmark or feature-acceptance result influenced dictionary training or selection.
