#pragma once

#include <memory>
#include <string_view>

namespace spdlog {
    class logger;

    namespace level {
        enum level_enum : int;
    }
}


namespace simnet::telemetry {
    void init_logger(std::string_view app_name, std::string_view log_file_path);

    std::shared_ptr<spdlog::logger> get_logger();

    // Change log level at runtime
    void set_log_level(int level);
}
