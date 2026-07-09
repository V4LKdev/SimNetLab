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
    using namespace simnet;

    /// Protects the two metric buffers.
    std::mutex metrics_mutex;
    std::vector<TickMetrics> tick_metrics;
    std::vector<MetricRecord> metric_records;

    /// Converts a metric value to its string representation.
    [[nodiscard]] std::string metric_value_to_string(MetricValue const& value)
    {
        // Visit with a lambda to handle each type in the variant
        return std::visit([]<typename T0>(T0 const& item) -> std::string {

            // Decay_t to remove references and cv-qualifiers for type comparison
            using Value = std::decay_t<T0>;

            // Handle each type explicitly
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
    // --- Tick metrics ---

    void submit_tick_metrics(TickMetrics const& metrics)
    {
        std::scoped_lock lock { metrics_mutex };
        tick_metrics.push_back(metrics);
    }

    std::vector<TickMetrics> take_tick_metrics()
    {
        // Swap-drain: the O(1) move under the lock hands the caller sole ownership and
        // leaves an empty buffer ready for producers, keeping the critical section tiny.
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

    // --- Generic metrics ---

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

    // --- Formatting ---

    std::string format_metric_record_key_value(MetricRecord const& record)
    {
        auto output = std::ostringstream {};
        output << record.stream << " tick=" << record.tick;

        // Append each field as 'name=value'
        for (auto const& field : record.fields) {
            output << ' ' << field.name << '=' << metric_value_to_string(field.value);
        }
        return output.str();
    }
}
