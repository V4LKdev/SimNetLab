#pragma once

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace simnet::test
{
    class TestTemporaryDirectory
    {
      public:
        explicit TestTemporaryDirectory(std::string_view prefix = "simnet_tests_tmp")
        {
            claim_directory(prefix);
        }

        TestTemporaryDirectory(TestTemporaryDirectory const&) = delete;
        TestTemporaryDirectory& operator=(TestTemporaryDirectory const&) = delete;
        TestTemporaryDirectory(TestTemporaryDirectory&&) = delete;
        TestTemporaryDirectory& operator=(TestTemporaryDirectory&&) = delete;

        ~TestTemporaryDirectory() noexcept
        {
            auto error = std::error_code{};
            std::filesystem::remove_all(path_, error);
        }

        [[nodiscard]] std::filesystem::path const& path() const noexcept
        {
            return path_;
        }

      private:
        void claim_directory(std::string_view prefix)
        {
            auto const base = std::filesystem::temp_directory_path();
            auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            auto const stem = std::string{prefix} + "_" + std::to_string(stamp);

            for (auto attempt = 0U; attempt < max_attempts; ++attempt)
            {
                auto const candidate = base / (stem + "_" + std::to_string(attempt));
                auto error = std::error_code{};
                if (std::filesystem::create_directory(candidate, error))
                {
                    path_ = candidate;
                    return;
                }
                if (error)
                {
                    throw std::runtime_error{
                        "Failed to claim temporary directory: " + candidate.string() + " (" +
                        error.message() + ")"
                    };
                }
            }

            throw std::runtime_error{"Failed to claim exclusive temporary directory"};
        }

        static constexpr auto max_attempts = 32U;
        std::filesystem::path path_{};
    };
}