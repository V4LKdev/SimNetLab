module;

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

module simnet.telemetry;

import :types;

namespace
{
    [[nodiscard]] std::string lowercase(std::string_view value)
    {
        auto result = std::string { value };
        std::ranges::transform(result, result.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return result;
    }
}

namespace simnet
{
    LogLevel parse_log_level(std::string_view value)
    {
        // Unknown values deliberately fall back to a useful default.
        const auto normalized = lowercase(value);
        if (normalized == "trace") {
            return LogLevel::Trace;
        }
        if (normalized == "debug") {
            return LogLevel::Debug;
        }
        if (normalized == "info") {
            return LogLevel::Info;
        }
        if (normalized == "warn" || normalized == "warning") {
            return LogLevel::Warn;
        }
        if (normalized == "error") {
            return LogLevel::Error;
        }
        if (normalized == "critical") {
            return LogLevel::Critical;
        }
        if (normalized == "off") {
            return LogLevel::Off;
        }
        return LogLevel::Info;
    }
}
