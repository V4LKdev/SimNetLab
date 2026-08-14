#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <chrono>
#include <stdexcept>

namespace simnet::test
{
    class TestTemporaryDirectory
    {
      public:
        explicit TestTemporaryDirectory(std::string_view prefix = "simnet_tests_tmp")
            : prefix_(prefix)
        {
            claim_directory();
        }

        TestTemporaryDirectory(TestTemporaryDirectory const&) = delete;
        TestTemporaryDirectory& operator=(TestTemporaryDirectory const&) = delete;
        TestTemporaryDirectory(TestTemporaryDirectory&&) = delete;
        TestTemporaryDirectory& operator=(TestTemporaryDirectory&&) = delete;

        ~TestTemporaryDirectory() noexcept
        {
            if (!path_.empty())
            {
                auto error = std::error_code{};
                std::filesystem::remove_all(path_, error);
            }
        }

        [[nodiscard]] std::filesystem::path const& path() const noexcept
        {
            return path_;
        }

      private:
        void claim_directory()
        {
            auto const base = std::filesystem::temp_directory_path();
            auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            auto const prefix = std::string{prefix_} + "_" + std::to_string(stamp);

            for (auto attempt = 0U; attempt < max_attempts_; ++attempt)
            {
                auto candidate = base / (prefix + "_" + std::to_string(attempt));
                auto error = std::error_code{};
                auto const claimed = std::filesystem::create_directory(candidate, error);

                if (!error && claimed)
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

        std::string prefix_;
        std::filesystem::path path_{};
        static constexpr auto max_attempts_ = 32U;
    };
}
