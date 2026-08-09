# External research evidence collectors

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

Unix nanoseconds join collector files to SimNet evidence for the supplied run ID. Monotonic elapsed
nanoseconds and record order provide within-collector ordering. Host temperature, powercap energy,
and pressure stall information are environmental covariates. They are not process-attributable
measurements.

