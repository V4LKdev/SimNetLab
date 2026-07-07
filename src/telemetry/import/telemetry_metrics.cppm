/// @brief Raw metric submission API.
export module simnet.telemetry:metrics;

import :types;

export namespace simnet
{
    /// Submits per-tick raw metrics.
    void submit_tick_metrics(TickMetrics const& metrics);
}
