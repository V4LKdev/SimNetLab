#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <limits>

namespace simnet::telemetry {
    /**
     * @brief Simple histogram for latency / size distributions.
     *
     * Records values in a pre‑allocated ring buffer to avoid allocation during
     * gameplay. Percentiles are computed on demand via sort.
     */
    class SimpleHistogram {
    public:
        explicit SimpleHistogram(size_t capacity = 1024);

        void add(double value);

        // --- read‑only stats, callable after capture is stopped or at flush ---
        [[nodiscard]] size_t count() const noexcept { return count_; }
        [[nodiscard]] double min() const noexcept { return min_; }
        [[nodiscard]] double max() const noexcept { return max_; }
        [[nodiscard]] double sum() const noexcept { return sum_; }

        [[nodiscard]] double average() const noexcept;

        [[nodiscard]] double percentile(double p) const; // 0.0 – 100.0

        /// Reset all data (used after flushing)
        void clear();

    private:
        const size_t capacity_;
        std::vector<double> buffer_;
        size_t write_index_ = 0;
        size_t count_ = 0;
        double min_ = std::numeric_limits<double>::max();
        double max_ = std::numeric_limits<double>::lowest();
        double sum_ = 0.0;

        size_t size_for_stats() const noexcept;
    };

    /**
     * @brief Singleton collector for counters and histograms.
     *
     * Thread‑safe via internal mutex (guards both counter and histogram maps).
     * Flushing writes a JSON line to the global spdlog logger at INFO level.
     */
    class MetricsCollector {
    public:
        static MetricsCollector &instance();

        // Increment a named counter by delta (signed)
        void add_counter(const std::string &name, int64_t delta);

        void set_counter(const std::string& name, int64_t value) {
            std::lock_guard lock(mutex_);
            counters_[name] = value;
        }

        // Add a sample to a named histogram
        void add_histogram(const std::string &name, double value);

        /**
         * @brief Reset all counters and histograms and write their current values
         *        as a JSON object to the log.
         */
        void flush_to_log();

        // --- Accessors for external consumers (e.g., Google Benchmark) ---
        [[nodiscard]] std::unordered_map<std::string, int64_t> get_counters_copy() const;

        struct HistogramStats {
            size_t count;
            double min, max, avg, p50, p95, p99;
        };

        [[nodiscard]] std::unordered_map<std::string, HistogramStats> get_histogram_stats() const;

        [[nodiscard]] int64_t get_counter(const std::string &name) const;

    private:
        MetricsCollector() = default;

        mutable std::mutex mutex_;
        std::unordered_map<std::string, int64_t> counters_;
        std::unordered_map<std::string, SimpleHistogram> histograms_;

        static std::string histogram_to_json(const std::string &name,
                                             const SimpleHistogram &hist);
    };

    // Convenience helper: creates a JSON escape for the key if needed
    inline std::string escape_json(const std::string &s);
}
