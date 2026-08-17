# External perf evidence collector

SimNet Server and Client CSV files record replication semantics and application stage elapsed wall
times. `simnet_perf_stat.py` records run-level perf counters for each attached process. Both layers
share `run_id`. Join process evidence by `run_id`, role, PID identity, and overlapping Unix-nanosecond
intervals. Perf measures the complete attached process, not an individual SimNet stage. Perf
`task-clock` and hardware cycles are different measurements from the application's steady-clock
stage durations.

## Perf run-level counters

`simnet_perf_stat.py` launches one `perf stat` attachment per explicit role and PID. It uses
machine-readable aggregate output, preserves counter runtime and running percentage, and writes:

- `perf_manifest_v1.json`
- `perf_counters_v1.csv`

Example:

```sh
python3 scripts/simnet_perf_stat.py \
    --output-dir results/run-001/perf-core \
    --run-id run-001 \
    --target server:1234 \
    --target client:1235 \
    --profile core \
    --duration-seconds 30
```

Profiles are intentionally small and require separate matched repetitions:

- `software`: `{task-clock,context-switches,cpu-migrations,page-faults}`
- `core`: `{cycles,instructions}` and `{branches,branch-misses}`
- `cache`: `{instructions,cache-references,cache-misses}`
- `memory`: independent `{instructions,access-event,miss-event}` groups for L1 data-cache,
  LLC load, data-TLB load, and instruction-TLB load events
- `stalls`: `{cycles,stalled-cycles-frontend,stalled-cycles-backend}`

There is no combined profile. Separate matched runs reduce multiplexing and keep ratios
interpretable. Unsupported generic hardware events are expected on some kernels, PMUs, virtual
machines, and permission configurations. They are recorded as unavailable and are not themselves a
project failure. The tool never invokes elevated privileges or changes host settings.

Analysis may derive IPC from instructions and cycles, branch-miss rate from branch misses and
branches, cache-miss rate from cache misses and references, and MPKI from misses and instructions.
A ratio is suitable only when both source counters are accepted and belong to the same simultaneous
event group. Cache and memory groups include instructions so their miss rates and MPKI are directly
valid. The stalls group includes cycles so frontend and backend stalled-cycle ratios are directly
valid. Joining accepted perf totals to SimNet rows permits per-tick, per-update, per-entity, or
per-peer normalization. Derived ratios are not stored as authoritative counters.

Counts below `--minimum-running-percent` remain in the CSV but are rejected for analysis. A scaled
count is never presented as exact. Use `--require-complete-profile` when every requested event must
be accepted. Perf attachment overhead should be quantified with a short instrumentation-on and
instrumentation-off pilot before final evidence collection.

On hybrid CPUs, perf may expand a generic event into one row per compatible PMU. The collector
combines those components by event and group, summing their values and counter runtimes. The
reported running percentage is their combined coverage, capped at 100 percent, and the normal
acceptance threshold still applies. Perf-selected user-space event scope is preserved by the
underlying event rows.
