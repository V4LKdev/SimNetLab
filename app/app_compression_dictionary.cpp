module;

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module simnet.app_compression_dictionary;

import simnet.app_common;
import simnet.compression;
import simnet.core;

namespace
{
    [[nodiscard]] std::vector<simnet::Byte> read_dictionary_bytes(std::filesystem::path const& path)
    {
        auto input = std::ifstream{path, std::ios::binary | std::ios::ate};
        if (!input)
        {
            throw std::runtime_error(
                "failed to open maintained compression dictionary: " + path.string()
            );
        }
        auto const size = input.tellg();
        if (size <= 0 || static_cast<std::uint64_t>(size) > simnet::maximum_zstd_dictionary_bytes ||
            size > std::numeric_limits<std::streamsize>::max())
        {
            throw std::runtime_error(
                "maintained compression dictionary byte count is invalid: " + path.string()
            );
        }
        auto bytes = std::vector<simnet::Byte>(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size()))
        {
            throw std::runtime_error(
                "failed to read maintained compression dictionary: " + path.string()
            );
        }
        return bytes;
    }
}

namespace simnet::app
{
    std::optional<LoadedCompressionDictionary>
    load_compression_dictionary(CompressionSettings const& settings)
    {
        if (settings.dictionary == "none")
        {
            return std::nullopt;
        }
        if (settings.mode != CompressionMode::WholeUpdate ||
            settings.dictionary != pipeline_v1_dictionary_name)
        {
            throw std::runtime_error(
                "unsupported maintained compression dictionary selection: " + settings.dictionary
            );
        }
        auto const path = std::filesystem::path{SIMNET_COMPRESSION_ASSET_DIR} / "pipeline_v1.zdict";
        auto bytes = read_dictionary_bytes(path);
        return LoadedCompressionDictionary{
            .name = pipeline_v1_dictionary_name,
            .dictionary = ZstdDictionary{
                std::move(bytes),
                settings.level,
                pipeline_v1_dictionary_expectations,
            },
        };
    }
}
