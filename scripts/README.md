# External research evidence collectors

SimNet research evidence has three layers:

1. SimNet Server and Client CSV files record replication semantics and application stage elapsed
   wall times.
2. `simnet_linux_collector.py` records Linux process and host time series.
3. `simnet_perf_stat.py` records run-level perf counters for each attached process.

All layers share `run_id`. Join process evidence by `run_id`, role, PID identity, and overlapping
Unix-nanosecond intervals. Perf measures the complete attached process, not an individual SimNet
stage. Perf `task-clock` and hardware cycles are different measurements from the application's
steady-clock stage durations.

## Linux process and host samples

`simnet_linux_collector.py` attaches to explicit Linux process IDs. It writes one versioned JSON
manifest and fixed-schema process and host CSV files. It uses only the Python standard library and
does not require elevated privileges.

Example:

```sh
python3 scripts/simnet_linux_collector.py \
    --output-directory results/run-001/linux \
    --run-id run-001 \
    --target server:1234 \
    --target client:1235 \
    --interval-ms 100 \
    --network-interface lo \
    --duration-seconds 30
```

The collector stops when all targets exit unless a duration ends first. It validates each PID by
its Linux process start time so that PID reuse cannot be mistaken for the original process.
`smaps_rollup` defaults to every tenth sample because it is a heavier kernel interface. Use
`--smaps-every` to select another positive interval.

Empty CSV cells mean unavailable. The row `errors_json` field and manifest capabilities explain
missing files, permission failures, parse failures, and unsupported sensors. Zero is written only
when Linux reports a real zero. Variable CPU frequency, hwmon temperature, and powercap sources use
self-describing JSON cells so the process schema remains fixed.

Process samples cover CPU and scheduler activity, memory residency, file IO, and process identity.
Host samples cover pressure stalls, available memory, load, interface counters, and optional
frequency, temperature, and energy sources.

Unix nanoseconds join collector files to SimNet evidence for the supplied run ID. Monotonic elapsed
nanoseconds and record order provide within-collector ordering. Host temperature, powercap energy,
and pressure stall information are environmental covariates. They are not process-attributable
measurements.

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
be accepted. Perf and Linux sampling overhead should be quantified with short instrumentation-on
and instrumentation-off pilot runs before final evidence collection.

On hybrid CPUs, perf may expand a generic event into one row per compatible PMU. The collector
combines those components by event and group, summing their values and counter runtimes. The
reported running percentage is their combined coverage, capped at 100 percent, and the normal
acceptance threshold still applies. Perf-selected user-space event scope is preserved by the
underlying event rows and does not replace the Linux collector's process-wide user and system CPU
accounting.

## Repository checks

Formatting and text-policy checks use tracked maintained files by default:

```sh
python3 scripts/simnet_format_check.py
python3 scripts/simnet_text_policy.py
```

Configure compiler-specific advisory builds with compilation databases:

```sh
cmake --preset gcc-analysis --fresh
cmake --build --preset gcc-analysis --parallel
cmake --preset clang-analysis --fresh
cmake --build --preset clang-analysis --parallel
```

Run clang-tidy only against the Clang database. Restrict a pass with repeatable `--path` options.
The runner never applies fixes.

```sh
python3 scripts/simnet_clang_tidy.py --build-dir build/clang-analysis --path src/core
python3 scripts/simnet_clang_tidy.py --build-dir build/clang-analysis --path src/pipeline
```

The analysis presets also provide opt-in `simnet_format_check`, `simnet_text_check`, and Clang-only
`simnet_clang_tidy` build targets. Extended compiler warnings and clang-tidy remain advisory until
the maintained modules are clean under GCC and Clang.
