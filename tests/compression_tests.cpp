#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

import simnet.compression;
import simnet.core;

namespace
{
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
    auto const decompressed_report =
        simnet::decompress_bytes(decompressor, compressed, limits(), restored);
    REQUIRE(decompressed_report.valid);
    CHECK(decompressed_report.encoding == simnet::CompressionEncoding::Zstd);
    CHECK(decompressed_report.input_bytes == compressed.size());
    CHECK(decompressed_report.output_bytes == compressible.size());
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

    auto const tiny = std::vector<simnet::Byte>{simnet::Byte{0x78U}};
    auto raw_envelope = std::vector<simnet::Byte>{};
    auto const raw_envelope_report = simnet::compress_bytes(
        compressor,
        tiny,
        1,
        limits(),
        simnet::CompressionEnvelopePolicy::Always,
        raw_envelope
    );
    REQUIRE(raw_envelope_report.valid);
    CHECK(raw_envelope_report.encoding == simnet::CompressionEncoding::Raw);
    CHECK(simnet::has_compression_envelope(raw_envelope));

    restored.clear();
    auto const raw_decompression_report =
        simnet::decompress_bytes(decompressor, raw_envelope, limits(), restored);
    REQUIRE(raw_decompression_report.valid);
    CHECK(raw_decompression_report.encoding == simnet::CompressionEncoding::Raw);
    CHECK(raw_decompression_report.input_bytes == raw_envelope.size());
    CHECK(raw_decompression_report.output_bytes == tiny.size());
    CHECK(restored == tiny);
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
