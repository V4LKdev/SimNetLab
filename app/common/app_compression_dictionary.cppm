module;

#include <cstdint>
#include <optional>
#include <string_view>

export module simnet.app_compression_dictionary;

import simnet.app_common;
import simnet.compression;

export namespace simnet::app
{
    inline constexpr std::string_view pipeline_v1_dictionary_name = "pipeline_v1";
    inline constexpr ZstdDictionaryExpectations pipeline_v1_dictionary_expectations{
        .dictionary_id = 0x534E0001U,
        .byte_count = 16U * 1024U,
        .content_fingerprint = 0x5fe43e7c3e7804a1ULL,
    };

    struct LoadedCompressionDictionary
    {
        std::string_view name{};
        ZstdDictionary dictionary;
    };

    /// Loads the selected maintained asset. A disabled selection performs no file access.
    [[nodiscard]] std::optional<LoadedCompressionDictionary>
    load_compression_dictionary(CompressionSettings const& settings);
}
