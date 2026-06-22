#include "logger.hpp"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>

namespace simnet::telemetry {
    static std::shared_ptr<spdlog::logger> g_logger;

    void init_logger(std::string_view app_name, std::string_view log_file_path)
    {
        // Initialize the async thread pool (8192 queue size, 1 background writer thread)
        spdlog::init_thread_pool(8192, 1);

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file_path.data(), true);

        console_sink->set_level(spdlog::level::trace);
        file_sink->set_level(spdlog::level::trace);

        // Create an ASYNC logger
        g_logger = std::make_shared<spdlog::async_logger>(
            app_name.data(),
            spdlog::sinks_init_list({console_sink, file_sink}),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block // Block if queue fills
        );

        g_logger->set_level(spdlog::level::debug);
        spdlog::register_logger(g_logger);
        g_logger->flush_on(spdlog::level::warn);
    }

    std::shared_ptr<spdlog::logger> get_logger()
    {
        return g_logger;
    }

    void set_log_level(spdlog::level::level_enum level)
    {
        if (g_logger) {
            g_logger->set_level(level);
        }
    }
}
