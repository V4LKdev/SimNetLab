#include "logger.hpp"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace simnet::telemetry {
    static std::shared_ptr<spdlog::logger> g_logger;

    void init_logger(std::string_view app_name, std::string_view log_file_path)
    {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file_path.data(), true);

        spdlog::sinks_init_list sinks = {
            console_sink, file_sink
        };

        g_logger = std::make_shared<spdlog::logger>(app_name.data(), sinks.begin(), sinks.end());
        g_logger->set_level(spdlog::level::debug); // Default rn
        spdlog::register_logger(g_logger);
        g_logger->flush_on(spdlog::level::info);
    }

    std::shared_ptr<spdlog::logger> get_logger()
    {
        return g_logger;
    }

    void set_log_level(int level)
    {
        if (g_logger) {
            g_logger->set_level(static_cast<spdlog::level::level_enum>(level));
        }
    }
}
