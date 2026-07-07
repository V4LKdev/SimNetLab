module;

#include <mutex>
#include <vector>

module simnet.telemetry;

import :metrics;
import :types;

namespace
{
    std::mutex metrics_mutex;
    std::vector<simnet::TickMetrics> tick_metrics;
}

namespace simnet
{
    void submit_tick_metrics(TickMetrics const& metrics)
    {
        std::scoped_lock lock { metrics_mutex };
        tick_metrics.push_back(metrics);
    }
}
