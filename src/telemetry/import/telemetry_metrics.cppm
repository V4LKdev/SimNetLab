module;

#include <vector>
#include <cstdint>
#include <string>
#include <variant>

/// @brief Raw metric submission API.
export module simnet.telemetry:metrics;

import :types;

export namespace simnet
{
    using MetricValue = std::variant<std::int64_t, std::uint64_t, double, bool, std::string>;

    /// Named value in a structured metric record.
    struct MetricField
    {
        std::string name;
        MetricValue value;
    };

    /// Generic semantic metric record for experiments and runtime events.
    struct MetricRecord
    {
        std::string stream;
        Tick tick {};
        std::vector<MetricField> fields;
    };

    /// Submits per-tick raw metrics.
    void submit_tick_metrics(TickMetrics const& metrics);

    /// Returns buffered tick metrics and clears the buffer.
    [[nodiscard]] std::vector<TickMetrics> take_tick_metrics();

    /// Clears buffered tick metrics without returning them.
    void clear_tick_metrics();

    /// Submits a generic structured metric record.
    void submit_metric_record(MetricRecord record);

    /// Returns buffered generic metric records and clears the buffer.
    [[nodiscard]] std::vector<MetricRecord> take_metric_records();

    /// Clears buffered generic metric records without returning them.
    void clear_metric_records();

    /// Formats a metric record as one stable key=value line.
    [[nodiscard]] std::string format_metric_record_key_value(MetricRecord const& record);
}
