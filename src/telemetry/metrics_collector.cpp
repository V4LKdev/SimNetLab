#include "metrics_collector.hpp"
#include "logger.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <sstream>

namespace simnet::telemetry {
    // --------------- SimpleHistogram ---------------

    SimpleHistogram::SimpleHistogram(size_t capacity)
        : capacity_(capacity)
    {
        buffer_.reserve(capacity_);
    }

    void SimpleHistogram::add(double value)
    {
        if (buffer_.size() < capacity_) {
            buffer_.push_back(value);
        } else {
            buffer_[write_index_] = value;
            write_index_ = (write_index_ + 1) % capacity_;
        }
        ++count_;
        if (value < min_) min_ = value;
        if (value > max_) max_ = value;
        sum_ += value;
    }

    double SimpleHistogram::average() const noexcept
    {
        const size_t n = size_for_stats();
        return n > 0 ? sum_ / static_cast<double>(n) : 0.0;
    }

    double SimpleHistogram::percentile(double p) const
    {
        const size_t n = size_for_stats();
        if (n == 0) return 0.0;

        // Create a copy of the current buffer content sorted
        std::vector<double> sorted(buffer_.begin(), buffer_.begin() + n);
        std::sort(sorted.begin(), sorted.end());

        double rank = (p / 100.0) * (n - 1);
        size_t idx_low = static_cast<size_t>(std::floor(rank));
        size_t idx_high = static_cast<size_t>(std::ceil(rank));
        if (idx_high >= n) idx_high = n - 1;

        double frac = rank - idx_low;
        return sorted[idx_low] * (1.0 - frac) + sorted[idx_high] * frac;
    }

    void SimpleHistogram::clear()
    {
        buffer_.clear();
        write_index_ = 0;
        count_ = 0;
        min_ = std::numeric_limits<double>::max();
        max_ = std::numeric_limits<double>::lowest();
        sum_ = 0.0;
    }

    size_t SimpleHistogram::size_for_stats() const noexcept
    {
        return (count_ > buffer_.size()) ? buffer_.size() : count_; // for ring buffer
    }

    // --------------- MetricsCollector ---------------

    MetricsCollector &MetricsCollector::instance()
    {
        static MetricsCollector inst;
        return inst;
    }

    void MetricsCollector::add_counter(const std::string &name, int64_t delta)
    {
        std::lock_guard lock(mutex_);
        counters_[name] += delta;
    }

    void MetricsCollector::add_histogram(const std::string &name, double value)
    {
        std::lock_guard lock(mutex_);
        auto it = histograms_.find(name);
        if (it == histograms_.end()) {
            it = histograms_.emplace(name, SimpleHistogram(1024)).first;
        }
        it->second.add(value);
    }

    void MetricsCollector::flush_to_log()
    {
        std::unordered_map<std::string, int64_t> local_counters;
        std::unordered_map<std::string, SimpleHistogram> local_histograms;

        {
            std::lock_guard lock(mutex_);
            local_counters = std::move(counters_);
            counters_.clear();
            local_histograms = std::move(histograms_);
            histograms_.clear();
        }

        if (local_counters.empty() && local_histograms.empty()) return;

        std::ostringstream json;
        json << "{";

        // Counters as key: value pairs
        bool first = true;
        for (const auto &[name, val]: local_counters) {
            if (!first) json << ", ";
            first = false;
            json << "\"" << escape_json(name) << "\":" << val;
        }

        // Histograms as nested objects
        for (const auto &[name, hist]: local_histograms) {
            if (!first) json << ", ";
            first = false;
            json << "\"" << escape_json(name) << "\":{"
                    << "\"count\":" << hist.count()
                    << ", \"min\":" << hist.min()
                    << ", \"max\":" << hist.max()
                    << ", \"avg\":" << hist.average()
                    << ", \"p50\":" << hist.percentile(50.0)
                    << ", \"p95\":" << hist.percentile(95.0)
                    << ", \"p99\":" << hist.percentile(99.0)
                    << "}";
        }

        json << "}";

        auto logger = get_logger();
        if (logger) {
            logger->info("metrics {}", json.str());
        }
    }

    std::unordered_map<std::string, int64_t> MetricsCollector::get_counters_copy() const
    {
        std::lock_guard lock(mutex_);
        return counters_;
    }

    std::unordered_map<std::string, MetricsCollector::HistogramStats> MetricsCollector::get_histogram_stats() const
    {
        std::lock_guard lock(mutex_);
        std::unordered_map<std::string, HistogramStats> result;
        for (const auto &[name, hist]: histograms_) {
            result[name] = {
                hist.count(), hist.min(), hist.max(), hist.average(),
                hist.percentile(50.0), hist.percentile(95.0), hist.percentile(99.0)
            };
        }
        return result;
    }

    std::string MetricsCollector::histogram_to_json(const std::string &name,
                                                    const SimpleHistogram &hist)
    {
        std::ostringstream oss;
        oss << "\"" << escape_json(name) << "\":{"
                << "\"count\":" << hist.count()
                << ", \"min\":" << hist.min()
                << ", \"max\":" << hist.max()
                << ", \"avg\":" << hist.average()
                << ", \"p50\":" << hist.percentile(50.0)
                << ", \"p95\":" << hist.percentile(95.0)
                << ", \"p99\":" << hist.percentile(99.0)
                << "}";
        return oss.str();
    }

    std::string escape_json(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c: s) {
            switch (c) {
                case '\\': out += "\\\\";
                    break;
                case '"': out += "\\\"";
                    break;
                default: out += c;
                    break;
            }
        }
        return out;
    }
}
