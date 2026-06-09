#include "telemetry.hpp"

#include <spdlog/logger.h>

namespace simnet::telemetry {
    void init(const std::string_view app_name, const std::string_view log_file_path)
    {
        init_logger(app_name, log_file_path);
    }

    void shutdown()
    {
        // flush logs
        if (auto logger = get_logger(); logger) {
            logger->flush();
        }
    }
}
