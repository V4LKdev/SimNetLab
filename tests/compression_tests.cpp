#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <utility>
#include <vector>
#include <zstd.h>

import simnet.compression;
import simnet.core;

namespace
{
    [[nodiscard]] std::vector<simnet::Byte> bytes(std::string_view text)
    {
        auto result = std::vector<simnet::Byte>{};
        result.reserve(text.size());
        for (auto const value : text)
        {
            result.push_back(static_cast<simnet::Byte>(value));
        }
        return result;
    }

    [[nodiscard]] simnet::CompressionLimits limits(std::uint32_t maximum = 8192U)
    {
        return {
            .max_uncompressed_bytes = maximum,
            .max_output_bytes = maximum,
        };
    }

    [[nodiscard]] std::vector<simnet::Byte> compressible_bytes()
    {
        auto result = std::vector<simnet::Byte>(4096U);
        for (auto index = std::size_t{}; index < result.size(); ++index)
        {
            result[index] = static_cast<simnet::Byte>(index % 7U);
        }
        return result;
    }

    [[nodiscard]] std::vector<simnet::Byte> incompressible_bytes(std::size_t size)
    {
        auto result = std::vector<simnet::Byte>(size);
        auto state = std::uint32_t{0x12345678U};
        for (auto& value : result)
        {
            state = state * 1664525U + 1013904223U;
            value = static_cast<simnet::Byte>(state >> 24U);
        }
        return result;
    }

    [[nodiscard]] std::vector<simnet::Byte> dictionary_bytes()
    {
        auto const path = std::filesystem::path{__FILE__}.parent_path().parent_path() /
                          "assets/compression/pipeline_v1.zdict";
        auto input = std::ifstream{path, std::ios::binary | std::ios::ate};
        REQUIRE(input);

        auto const size = input.tellg();
        REQUIRE(size > 0);

        auto result = std::vector<simnet::Byte>(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(
            reinterpret_cast<char*>(result.data()),
            static_cast<std::streamsize>(result.size())
        );
        REQUIRE(input);
        return result;
    }

    [[nodiscard]] std::uint64_t dictionary_fingerprint(simnet::ByteSpan data)
    {
        auto hash = std::uint64_t{14695981039346656037ULL};
        for (auto const byte : data)
        {
            hash ^= static_cast<std::uint8_t>(byte);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    [[nodiscard]] simnet::ZstdDictionary pipeline_dictionary()
    {
        auto data = dictionary_bytes();
        auto const expectations = simnet::ZstdDictionaryExpectations{
            .dictionary_id = ZSTD_getDictID_fromDict(data.data(), data.size()),
            .byte_count = static_cast<std::uint32_t>(data.size()),
            .content_fingerprint = dictionary_fingerprint(data),
        };
        return simnet::ZstdDictionary{std::move(data), 1, expectations};
    }
}

TEST_CASE("ordinary compression round-trips and only keeps useful compression", "[compression]")
{
    auto compressor = simnet::ZstdCompressor{};
    auto decompressor = simnet::ZstdDecompressor{};

    auto const compressible = compressible_bytes();
    auto compressed = std::vector<simnet::Byte>{};
    auto const compressed_report = simnet::compress_bytes(
        compressor,
        compressible,
        1,
        limits(),
        simnet::CompressionEnvelopePolicy::OnlyWhenSmaller,
        compressed
    );
    REQUIRE(compressed_report.valid);
    CHECK(compressed_report.encoding == simnet::CompressionEncoding::Zstd);
    CHECK(compressed_report.output_bytes < compressed_report.input_bytes);

    auto restored = std::vector<simnet::Byte>{};
    REQUIRE(simnet::decompress_bytes(decompressor, compressed, limits(), restored).valid);
    CHECK(restored == compressible);

    auto const incompressible = incompressible_bytes(1024U);
    auto raw = std::vector<simnet::Byte>{};
    auto const raw_report = simnet::compress_bytes(
        compressor,
        incompressible,
        1,
        limits(),
        simnet::CompressionEnvelopePolicy::OnlyWhenSmaller,
        raw
    );
    REQUIRE(raw_report.valid);
    CHECK(raw_report.encoding == simnet::CompressionEncoding::Raw);
    CHECK(raw == incompressible);
}

TEST_CASE("compression respects configured byte bounds", "[compression][bounds]")
{
    auto compressor = simnet::ZstdCompressor{};
    auto const input = compressible_bytes();
    auto output = std::vector<simnet::Byte>{};

    CHECK_FALSE(
        simnet::compress_bytes(
            compressor,
            input,
            1,
            {
                .max_uncompressed_bytes = static_cast<std::uint32_t>(input.size() - 1U),
                .max_output_bytes = 8192U,
            },
            simnet::CompressionEnvelopePolicy::Always,
            output
        )
            .valid
    );

    REQUIRE(
        simnet::compress_bytes(
            compressor,
            input,
            1,
            limits(),
            simnet::CompressionEnvelopePolicy::Always,
            output
        )
            .valid
    );

    auto decompressor = simnet::ZstdDecompressor{};
    auto restored = std::vector<simnet::Byte>{};
    CHECK_FALSE(
        simnet::decompress_bytes(
            decompressor,
            output,
            {
                .max_uncompressed_bytes = static_cast<std::uint32_t>(input.size() - 1U),
                .max_output_bytes = 8192U,
            },
            restored
        )
            .valid
    );
}

TEST_CASE("pipeline dictionary has stable identity and round-trips exactly", "[compression][dictionary]")
{
    auto dictionary = pipeline_dictionary();
    CHECK(dictionary.identity().dictionary_id == 0x534E0001U);
    CHECK(dictionary.identity().byte_count == 16384U);
    CHECK(dictionary.identity().content_fingerprint == 0x5fe43e7c3e7804a1ULL);

    auto compressor = simnet::ZstdCompressor{};
    auto decompressor = simnet::ZstdDecompressor{};

    auto const input = compressible_bytes();
    auto compressed = std::vector<simnet::Byte>{};
    auto const report =
        simnet::compress_bytes_with_dictionary(compressor, dictionary, input, limits(), compressed);
    REQUIRE(report.valid);
    CHECK(report.encoding == simnet::CompressionEncoding::ZstdDictionary);

    auto restored = std::vector<simnet::Byte>{};
    REQUIRE(
        simnet::decompress_bytes_with_dictionary(
            decompressor,
            dictionary,
            compressed,
            limits(),
            restored
        )
            .valid
    );
    CHECK(restored == input);

    auto const tiny = bytes("x");
    auto fallback = std::vector<simnet::Byte>{};
    auto const fallback_report =
        simnet::compress_bytes_with_dictionary(compressor, dictionary, tiny, limits(), fallback);
    REQUIRE(fallback_report.valid);
    CHECK(fallback_report.encoding == simnet::CompressionEncoding::Raw);

    REQUIRE(
        simnet::decompress_bytes_with_dictionary(
            decompressor,
            dictionary,
            fallback,
            limits(),
            restored
        )
            .valid
    );
    CHECK(restored == tiny);
}