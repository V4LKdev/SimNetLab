# telemetry

Lightweight instrumentation library for SimNetLab benchmarks and runtime monitoring.

- Lock‑free per‑thread counters and histograms.
- Automatic JSON logging to file every second via spdlog.

## Usage

```cpp
import telemetry;

simnet::telemetry::init("mysim", "logfile.log");

// … simulation loop …
simnet::telemetry::add_counter(MetricID::EntitiesActive, 500);
simnet::telemetry::add_histogram(MetricID::SpatialDivergence, 0.0123);

auto snap = simnet::telemetry::tick_snapshot();
// snap.counters[i] contains the value for MetricID i

simnet::telemetry::shutdown();
```

## Available metrics

See `MetricID` enum - covers bandwidth, CPU breakdown, memory, thread contention, snapshot delivery, determinism,
compression and jitter.

Counters are accumulated automatically. histograms are merged in the background thread logs.

## Integration with Google Benchmark

```cpp
#include <benchmark/benchmark.h>
import telemetry;

static void BM_Tick(benchmark::State& state) {
    simnet::telemetry::init("bench");
    for (auto _ : state) {
        // run one tick …
        simnet::telemetry::add_counter(MetricID::NetworkBytesSent, data.size());
        auto snap = simnet::telemetry::tick_snapshot();
        state.SetBytesProcessed(snap.counters[ (size_t)MetricID::NetworkBytesSent ]);
    }
    simnet::telemetry::shutdown();
}
BENCHMARK(BM_Tick);
```