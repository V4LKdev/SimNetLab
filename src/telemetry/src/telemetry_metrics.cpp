module;

#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

module simnet.telemetry;

import :metrics;
import :types;

namespace
{
    std::mutex metrics_mutex;
    std::vector<simnet::TickMetrics> tick_metrics;
    std::vector<simnet::MetricRecord> metric_records;

    [[nodiscard]] std::string metric_value_to_string(simnet::MetricValue const& value)
    {
        return std::visit([](auto const& item) -> std::string {
            using Value = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Value, bool>) {
                return item ? "true" : "false";
            } else if constexpr (std::is_same_v<Value, std::string>) {
                return item;
            } else {
                return std::to_string(item);
            }
        }, value);
    }
}

namespace simnet
{
    void submit_tick_metrics(TickMetrics const& metrics)
    {
        std::scoped_lock lock { metrics_mutex };
        tick_metrics.push_back(metrics);
    }

    std::vector<TickMetrics> take_tick_metrics()
    {
        std::scoped_lock lock { metrics_mutex };
        auto output = std::move(tick_metrics);
        tick_metrics.clear();
        return output;
    }

    void clear_tick_metrics()
    {
        std::scoped_lock lock { metrics_mutex };
        tick_metrics.clear();
    }

    void submit_metric_record(MetricRecord record)
    {
        std::scoped_lock lock { metrics_mutex };
        metric_records.push_back(std::move(record));
    }

    std::vector<MetricRecord> take_metric_records()
    {
        std::scoped_lock lock { metrics_mutex };
        auto output = std::move(metric_records);
        metric_records.clear();
        return output;
    }

    void clear_metric_records()
    {
        std::scoped_lock lock { metrics_mutex };
        metric_records.clear();
    }

    std::string format_metric_record_key_value(MetricRecord const& record)
    {
        auto output = std::ostringstream {};
        output << record.stream << " tick=" << record.tick;
        for (auto const& field : record.fields) {
            output << ' ' << field.name << '=' << metric_value_to_string(field.value);
        }
        return output.str();
    }
}
