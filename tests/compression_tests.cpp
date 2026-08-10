#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

    void write_u32(std::vector<simnet::Byte>& data, std::size_t offset, std::uint32_t value)
    {
        data[offset] = static_cast<simnet::Byte>(value >> 24U);
        data[offset + 1U] = static_cast<simnet::Byte>(value >> 16U);
        data[offset + 2U] = static_cast<simnet::Byte>(value >> 8U);
        data[offset + 3U] = static_cast<simnet::Byte>(value);
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
        if (!input)
        {
            return {};
        }
        auto const size = input.tellg();
        if (size <= 0)
        {
            return {};
        }
        auto result = std::vector<simnet::Byte>(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(
            reinterpret_cast<char*>(result.data()),
            static_cast<std::streamsize>(result.size())
        );
        return input ? result : std::vector<simnet::Byte>{};
    }

    [[nodiscard]] std::uint64_t dictionary_fingerprint(simnet::ByteSpan bytes)
    {
        auto hash = std::uint64_t{14695981039346656037ULL};
        for (auto const byte : bytes)
        {
            hash ^= static_cast<std::uint8_t>(byte);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    [[nodiscard]] simnet::ZstdDictionaryExpectations dictionary_expectations(simnet::ByteSpan data)
    {
        return {
            .dictionary_id = ZSTD_getDictID_fromDict(data.data(), data.size()),
            .byte_count = static_cast<std::uint32_t>(data.size()),
            .content_fingerprint = dictionary_fingerprint(data),
        };
    }

    [[nodiscard]] simnet::ZstdDictionary pipeline_dictionary()
    {
        auto data = dictionary_bytes();
        auto const expectations = dictionary_expectations(data);
        return simnet::ZstdDictionary{std::move(data), 1, expectations};
    }
}

TEST_CASE("compression preserves raw bytes when an envelope is not required", "[compression]")
{
    auto compressor = simnet::ZstdCompressor{};
    auto const input = bytes("one byte is enough");
    auto output = std::vector<simnet::Byte>{};
    auto const report = simnet::compress_bytes(
        compressor,
        input,
        1,
        limits(),
        simnet::CompressionEnvelopePolicy::OnlyWhenSmaller,
        output
    );
    REQUIRE(report.valid);
    CHECK(report.encoding == simnet::CompressionEncoding::Raw);
    CHECK(report.envelope_bytes == 0U);
    CHECK(output == input);
}

TEST_CASE("Raw and Zstd envelopes roundtrip exactly", "[compression][roundtrip]")
{
    auto compressor = simnet::ZstdCompressor{};
    auto decompressor = simnet::ZstdDecompressor{};
    for (auto const& input : {bytes("x"), compressible_bytes()})
    {
        auto envelope = std::vector<simnet::Byte>{};
        auto const encoded = simnet::compress_bytes(
            compressor,
            input,
            1,
            limits(),
            simnet::CompressionEnvelopePolicy::Always,
            envelope
        );
        REQUIRE(encoded.valid);
        CHECK(encoded.envelope_bytes == simnet::compression_envelope_bytes);
        CHECK(encoded.input_bytes == input.size());
        CHECK(encoded.encoded_payload_bytes + encoded.envelope_bytes == encoded.output_bytes);
        CHECK(simnet::has_compression_envelope(envelope));
        CHECK(
            encoded.encoding == (input.size() == 1U ? simnet::CompressionEncoding::Raw
                                                    : simnet::CompressionEncoding::Zstd)
        );
        if (input.size() == 1U)
        {
            CHECK(
                envelope == std::vector<simnet::Byte>{
                                simnet::Byte{'S'},
                                simnet::Byte{'N'},
                                simnet::Byte{'C'},
                                simnet::Byte{'Z'},
                                simnet::Byte{0U},
                                simnet::Byte{1U},
                                simnet::Byte{0U},
                                simnet::Byte{1U},
                                simnet::Byte{0U},
                                simnet::Byte{0U},
                                simnet::Byte{0U},
                                simnet::Byte{0U},
                                simnet::Byte{1U},
                                simnet::Byte{0U},
                                simnet::Byte{0U},
                                simnet::Byte{0U},
                                simnet::Byte{1U},
                                simnet::Byte{'x'},
                            }
            );
        }

        auto decoded_bytes = std::vector<simnet::Byte>{};
        auto const decoded =
            simnet::decompress_bytes(decompressor, envelope, limits(), decoded_bytes);
        REQUIRE(decoded.valid);
        CHECK(decoded.encoding == encoded.encoding);
        CHECK(decoded.output_bytes == input.size());
        CHECK(decoded_bytes == input);
    }
}

TEST_CASE("compression enforces levels and contextual bounds", "[compression][bounds]")
{
    auto compressor = simnet::ZstdCompressor{};
    auto output = std::vector<simnet::Byte>{};
    auto const input = bytes("bounded input");
    CHECK_FALSE(
        simnet::compress_bytes(
            compressor,
            {},
            1,
            limits(),
            simnet::CompressionEnvelopePolicy::Always,
            output
        )
            .valid
    );
    CHECK_FALSE(
        simnet::compress_bytes(
            compressor,
            input,
            0,
            limits(),
            simnet::CompressionEnvelopePolicy::Always,
            output
        )
            .valid
    );
    CHECK_FALSE(
        simnet::compress_bytes(
            compressor,
            input,
            20,
            limits(),
            simnet::CompressionEnvelopePolicy::Always,
            output
        )
            .valid
    );
    CHECK_FALSE(
        simnet::compress_bytes(
            compressor,
            input,
            1,
            {.max_uncompressed_bytes = 4U, .max_output_bytes = 64U},
            simnet::CompressionEnvelopePolicy::Always,
            output
        )
            .valid
    );
    CHECK_FALSE(
        simnet::compress_bytes(
            compressor,
            input,
            1,
            {.max_uncompressed_bytes = 64U, .max_output_bytes = 17U},
            simnet::CompressionEnvelopePolicy::Always,
            output
        )
            .valid
    );
}

TEST_CASE("maximum bounded and incompressible inputs follow deterministic policy", "[compression]")
{
    auto compressor = simnet::ZstdCompressor{};
    auto decompressor = simnet::ZstdDecompressor{};
    auto maximum_input = std::vector<simnet::Byte>(8192U, simnet::Byte{0x5AU});
    auto envelope = std::vector<simnet::Byte>{};
    auto const maximum_report = simnet::compress_bytes(
        compressor,
        maximum_input,
        1,
        limits(),
        simnet::CompressionEnvelopePolicy::Always,
        envelope
    );
    REQUIRE(maximum_report.valid);
    auto restored = std::vector<simnet::Byte>{};
    REQUIRE(simnet::decompress_bytes(decompressor, envelope, limits(), restored).valid);
    CHECK(restored == maximum_input);

    auto const incompressible = incompressible_bytes(1024U);
    auto first = std::vector<simnet::Byte>{};
    auto second = std::vector<simnet::Byte>{};
    auto const first_report = simnet::compress_bytes(
        compressor,
        incompressible,
        1,
        limits(),
        simnet::CompressionEnvelopePolicy::OnlyWhenSmaller,
        first
    );
    auto const second_report = simnet::compress_bytes(
        compressor,
        incompressible,
        1,
        limits(),
        simnet::CompressionEnvelopePolicy::OnlyWhenSmaller,
        second
    );
    REQUIRE(first_report.valid);
    REQUIRE(second_report.valid);
    CHECK(first_report.encoding == simnet::CompressionEncoding::Raw);
    CHECK(first == incompressible);
    CHECK(second == first);
}

TEST_CASE("decompression rejects malformed envelope fields", "[compression][malformed]")
{
    auto compressor = simnet::ZstdCompressor{};
    auto decompressor = simnet::ZstdDecompressor{};
    auto const input = compressible_bytes();
    auto valid = std::vector<simnet::Byte>{};
    REQUIRE(
        simnet::compress_bytes(
            compressor,
            input,
            1,
            limits(),
            simnet::CompressionEnvelopePolicy::Always,
            valid
        )
            .valid
    );

    auto output = std::vector<simnet::Byte>{};
    for (auto mutation : {0U, 4U, 6U, 8U})
    {
        auto malformed = valid;
        malformed[mutation] ^= simnet::Byte{0x7FU};
        CHECK_FALSE(simnet::decompress_bytes(decompressor, malformed, limits(), output).valid);
    }
    auto truncated = valid;
    truncated.pop_back();
    CHECK_FALSE(simnet::decompress_bytes(decompressor, truncated, limits(), output).valid);

    auto zero_size = valid;
    write_u32(zero_size, 9U, 0U);
    CHECK_FALSE(simnet::decompress_bytes(decompressor, zero_size, limits(), output).valid);

    auto excessive = valid;
    write_u32(excessive, 9U, 8193U);
    CHECK_FALSE(simnet::decompress_bytes(decompressor, excessive, limits(), output).valid);

    auto inconsistent = valid;
    write_u32(inconsistent, 13U, static_cast<std::uint32_t>(valid.size()));
    CHECK_FALSE(simnet::decompress_bytes(decompressor, inconsistent, limits(), output).valid);
}

TEST_CASE("Zstd frame validation rejects corruption and extra frames", "[compression][zstd]")
{
    auto compressor = simnet::ZstdCompressor{};
    auto decompressor = simnet::ZstdDecompressor{};
    auto const input = compressible_bytes();
    auto valid = std::vector<simnet::Byte>{};
    REQUIRE(
        simnet::compress_bytes(
            compressor,
            input,
            1,
            limits(),
            simnet::CompressionEnvelopePolicy::Always,
            valid
        )
            .valid
    );

    auto output = bytes("unchanged");
    auto const original = output;
    auto corrupt = valid;
    corrupt.back() ^= simnet::Byte{0xFFU};
    auto const corrupt_report = simnet::decompress_bytes(decompressor, corrupt, limits(), output);
    CHECK_FALSE(corrupt_report.valid);
    CHECK(corrupt_report.error.starts_with("Zstd decompression failed: "));
    CHECK(output == original);

    auto concatenated = valid;
    auto const payload = std::vector<simnet::Byte>(
        valid.begin() + static_cast<std::ptrdiff_t>(simnet::compression_envelope_bytes),
        valid.end()
    );
    concatenated.insert(concatenated.end(), payload.begin(), payload.end());
    write_u32(
        concatenated,
        13U,
        static_cast<std::uint32_t>(concatenated.size() - simnet::compression_envelope_bytes)
    );
    CHECK_FALSE(simnet::decompress_bytes(decompressor, concatenated, limits(16384U), output).valid);
    CHECK(output == original);

    auto trailing = valid;
    trailing.push_back(simnet::Byte{0U});
    write_u32(
        trailing,
        13U,
        static_cast<std::uint32_t>(trailing.size() - simnet::compression_envelope_bytes)
    );
    CHECK_FALSE(simnet::decompress_bytes(decompressor, trailing, limits(), output).valid);
    CHECK(output == original);
}

TEST_CASE("unknown Zstd frame sizes are rejected without poisoning later input", "[compression]")
{
    auto const input = compressible_bytes();
    auto* context = ZSTD_createCCtx();
    REQUIRE(context != nullptr);
    REQUIRE_FALSE(ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_contentSizeFlag, 0)));
    auto frame = std::vector<simnet::Byte>(ZSTD_compressBound(input.size()));
    auto const frame_size =
        ZSTD_compress2(context, frame.data(), frame.size(), input.data(), input.size());
    ZSTD_freeCCtx(context);
    REQUIRE_FALSE(ZSTD_isError(frame_size));
    frame.resize(frame_size);

    auto unknown = std::vector<simnet::Byte>{
        simnet::Byte{'S'},
        simnet::Byte{'N'},
        simnet::Byte{'C'},
        simnet::Byte{'Z'},
        simnet::Byte{0U},
        simnet::Byte{1U},
        simnet::Byte{0U},
        simnet::Byte{1U},
        simnet::Byte{1U},
        simnet::Byte{0U},
        simnet::Byte{0U},
        simnet::Byte{0x10U},
        simnet::Byte{0U},
        simnet::Byte{0U},
        simnet::Byte{0U},
        simnet::Byte{0U},
        simnet::Byte{0U},
    };
    write_u32(unknown, 9U, static_cast<std::uint32_t>(input.size()));
    write_u32(unknown, 13U, static_cast<std::uint32_t>(frame.size()));
    unknown.insert(unknown.end(), frame.begin(), frame.end());

    auto decompressor = simnet::ZstdDecompressor{};
    auto output = std::vector<simnet::Byte>{};
    CHECK_FALSE(simnet::decompress_bytes(decompressor, unknown, limits(), output).valid);

    auto compressor = simnet::ZstdCompressor{};
    auto valid = std::vector<simnet::Byte>{};
    REQUIRE(
        simnet::compress_bytes(
            compressor,
            input,
            1,
            limits(),
            simnet::CompressionEnvelopePolicy::Always,
            valid
        )
            .valid
    );
    REQUIRE(simnet::decompress_bytes(decompressor, valid, limits(), output).valid);
    CHECK(output == input);
}

TEST_CASE(
    "pipeline_v1 dictionary identity and prepared state are reusable",
    "[compression][dictionary]"
)
{
    auto dictionary = pipeline_dictionary();
    CHECK(dictionary.identity().dictionary_id == 0x534E0001U);
    CHECK(dictionary.identity().byte_count == 16384U);
    CHECK(dictionary.identity().content_fingerprint == 0x5fe43e7c3e7804a1ULL);

    auto compressor = simnet::ZstdCompressor{};
    auto decompressor = simnet::ZstdDecompressor{};
    auto const input = compressible_bytes();
    auto first = std::vector<simnet::Byte>{};
    auto second = std::vector<simnet::Byte>{};
    REQUIRE(
        simnet::compress_bytes_with_dictionary(compressor, dictionary, input, limits(), first).valid
    );
    REQUIRE(
        simnet::compress_bytes_with_dictionary(compressor, dictionary, input, limits(), second)
            .valid
    );
    CHECK(second == first);

    auto restored = std::vector<simnet::Byte>{};
    for (auto iteration = 0; iteration < 3; ++iteration)
    {
        auto const report = simnet::decompress_bytes_with_dictionary(
            decompressor,
            dictionary,
            first,
            limits(),
            restored
        );
        REQUIRE(report.valid);
        CHECK(report.encoding == simnet::CompressionEncoding::ZstdDictionary);
        CHECK(restored == input);
    }
}

TEST_CASE(
    "dictionary compression is encoding 2 with an embedded nonzero dictionary ID",
    "[compression][dictionary][wire]"
)
{
    auto dictionary = pipeline_dictionary();
    auto compressor = simnet::ZstdCompressor{};
    auto const input = compressible_bytes();
    auto envelope = std::vector<simnet::Byte>{};
    auto const report =
        simnet::compress_bytes_with_dictionary(compressor, dictionary, input, limits(), envelope);
    REQUIRE(report.valid);
    REQUIRE(report.encoding == simnet::CompressionEncoding::ZstdDictionary);
    REQUIRE(envelope.size() > simnet::compression_envelope_bytes);
    CHECK(envelope[8] == simnet::Byte{2U});
    auto const payload = simnet::ByteSpan{envelope}.subspan(simnet::compression_envelope_bytes);
    CHECK(ZSTD_getDictID_fromFrame(payload.data(), payload.size()) == 0x534E0001U);
}

TEST_CASE("dictionary mode has a truthful Raw envelope fallback", "[compression][dictionary]")
{
    auto dictionary = pipeline_dictionary();
    auto compressor = simnet::ZstdCompressor{};
    auto const input = bytes("x");
    auto envelope = std::vector<simnet::Byte>{};
    auto const report =
        simnet::compress_bytes_with_dictionary(compressor, dictionary, input, limits(), envelope);
    REQUIRE(report.valid);
    CHECK(report.encoding == simnet::CompressionEncoding::Raw);
    CHECK(report.envelope_bytes == simnet::compression_envelope_bytes);
    CHECK(report.encoded_payload_bytes == input.size());
    CHECK(report.output_bytes == input.size() + simnet::compression_envelope_bytes);

    auto decompressor = simnet::ZstdDecompressor{};
    auto restored = std::vector<simnet::Byte>{};
    REQUIRE(
        simnet::decompress_bytes_with_dictionary(
            decompressor,
            dictionary,
            envelope,
            limits(),
            restored
        )
            .valid
    );
    CHECK(restored == input);
}

TEST_CASE(
    "dictionary compression and decompression enforce exact bounds transactionally",
    "[compression][dictionary][bounds][transaction]"
)
{
    auto dictionary = pipeline_dictionary();
    auto compressor = simnet::ZstdCompressor{};
    auto const input = compressible_bytes();
    auto envelope = bytes("unchanged");
    auto const unchanged = envelope;
    CHECK_FALSE(
        simnet::compress_bytes_with_dictionary(
            compressor,
            dictionary,
            input,
            {
                .max_uncompressed_bytes = static_cast<std::uint32_t>(input.size()),
                .max_output_bytes = 1U,
            },
            envelope
        )
            .valid
    );
    CHECK(envelope == unchanged);

    REQUIRE(
        simnet::compress_bytes_with_dictionary(compressor, dictionary, input, limits(), envelope)
            .valid
    );
    auto decompressor = simnet::ZstdDecompressor{};
    auto restored = bytes("unchanged");
    auto const restored_unchanged = restored;
    CHECK_FALSE(
        simnet::decompress_bytes_with_dictionary(
            decompressor,
            dictionary,
            envelope,
            {
                .max_uncompressed_bytes = static_cast<std::uint32_t>(input.size() - 1U),
                .max_output_bytes = static_cast<std::uint32_t>(envelope.size()),
            },
            restored
        )
            .valid
    );
    CHECK(restored == restored_unchanged);
}

TEST_CASE(
    "ordinary and dictionary Zstd decode to deterministic identical bytes",
    "[compression][dictionary][determinism]"
)
{
    auto dictionary = pipeline_dictionary();
    auto compressor = simnet::ZstdCompressor{};
    auto const input = compressible_bytes();
    auto ordinary = std::vector<simnet::Byte>{};
    auto dictionary_encoded = std::vector<simnet::Byte>{};
    REQUIRE(
        simnet::compress_bytes(
            compressor,
            input,
            1,
            limits(),
            simnet::CompressionEnvelopePolicy::Always,
            ordinary
        )
            .valid
    );
    REQUIRE(
        simnet::compress_bytes_with_dictionary(
            compressor,
            dictionary,
            input,
            limits(),
            dictionary_encoded
        )
            .valid
    );

    auto decompressor = simnet::ZstdDecompressor{};
    auto ordinary_result = std::vector<simnet::Byte>{};
    auto dictionary_result = std::vector<simnet::Byte>{};
    REQUIRE(simnet::decompress_bytes(decompressor, ordinary, limits(), ordinary_result).valid);
    REQUIRE(
        simnet::decompress_bytes_with_dictionary(
            decompressor,
            dictionary,
            dictionary_encoded,
            limits(),
            dictionary_result
        )
            .valid
    );
    CHECK(dictionary_result == ordinary_result);
    CHECK(dictionary_result == input);
}

TEST_CASE(
    "dictionary construction rejects missing corrupt oversized and mismatched assets",
    "[compression][dictionary][failure]"
)
{
    auto valid = dictionary_bytes();
    REQUIRE(valid.size() == 16384U);
    auto const expected = dictionary_expectations(valid);

    CHECK_THROWS(simnet::ZstdDictionary({}, 1, expected));
    CHECK_THROWS(
        simnet::ZstdDictionary(
            std::vector<simnet::Byte>(simnet::maximum_zstd_dictionary_bytes + 1U),
            1,
            expected
        )
    );
    auto truncated = valid;
    truncated.pop_back();
    CHECK_THROWS(simnet::ZstdDictionary(std::move(truncated), 1, expected));
    auto corrupt = valid;
    corrupt[32] ^= simnet::Byte{0x80U};
    CHECK_THROWS(simnet::ZstdDictionary(std::move(corrupt), 1, expected));
    auto wrong_id = expected;
    ++wrong_id.dictionary_id;
    CHECK_THROWS(simnet::ZstdDictionary(valid, 1, wrong_id));
    auto wrong_hash = expected;
    ++wrong_hash.content_fingerprint;
    CHECK_THROWS(simnet::ZstdDictionary(valid, 1, wrong_hash));
    CHECK_THROWS(simnet::ZstdDictionary(valid, 0, expected));
}

TEST_CASE(
    "dictionary frames reject absent wrong corrupt truncated and trailing input transactionally",
    "[compression][dictionary][malformed][transaction]"
)
{
    auto dictionary = pipeline_dictionary();
    auto compressor = simnet::ZstdCompressor{};
    auto const input = compressible_bytes();
    auto valid = std::vector<simnet::Byte>{};
    REQUIRE(
        simnet::compress_bytes_with_dictionary(compressor, dictionary, input, limits(), valid).valid
    );

    auto decompressor = simnet::ZstdDecompressor{};
    auto output = bytes("unchanged");
    auto const original = output;
    CHECK_FALSE(simnet::decompress_bytes(decompressor, valid, limits(), output).valid);
    CHECK(output == original);

    auto wrong_bytes = dictionary_bytes();
    wrong_bytes[4] ^= simnet::Byte{0x01U};
    auto wrong_dictionary = simnet::ZstdDictionary{
        wrong_bytes,
        1,
        dictionary_expectations(wrong_bytes),
    };
    CHECK_FALSE(
        simnet::decompress_bytes_with_dictionary(
            decompressor,
            wrong_dictionary,
            valid,
            limits(),
            output
        )
            .valid
    );
    CHECK(output == original);

    auto reject = [&](std::vector<simnet::Byte> malformed, std::uint32_t maximum = 8192U)
    {
        CHECK_FALSE(
            simnet::decompress_bytes_with_dictionary(
                decompressor,
                dictionary,
                malformed,
                limits(maximum),
                output
            )
                .valid
        );
        CHECK(output == original);
    };
    auto corrupt = valid;
    corrupt.back() ^= simnet::Byte{0x80U};
    reject(std::move(corrupt));
    auto truncated = valid;
    truncated.pop_back();
    reject(std::move(truncated));
    auto trailing = valid;
    trailing.push_back(simnet::Byte{});
    write_u32(trailing, 13U, static_cast<std::uint32_t>(trailing.size() - 17U));
    reject(std::move(trailing));
    auto concatenated = valid;
    concatenated.insert(
        concatenated.end(),
        valid.begin() + static_cast<std::ptrdiff_t>(simnet::compression_envelope_bytes),
        valid.end()
    );
    write_u32(
        concatenated,
        13U,
        static_cast<std::uint32_t>(concatenated.size() - simnet::compression_envelope_bytes)
    );
    reject(std::move(concatenated), 16384U);
    auto wrong_size = valid;
    write_u32(wrong_size, 9U, static_cast<std::uint32_t>(input.size() - 1U));
    reject(std::move(wrong_size));
    auto ordinary_encoding = valid;
    ordinary_encoding[8] = simnet::Byte{1U};
    reject(std::move(ordinary_encoding));
}
