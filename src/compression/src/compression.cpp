module;

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include <zstd.h>

module simnet.compression;

import simnet.core;

namespace
{
    constexpr auto compression_magic = std::uint32_t{0x534E435AU};
    constexpr auto compression_protocol_version = std::uint16_t{1U};
    constexpr auto compression_schema_version = std::uint16_t{1U};

    struct EnvelopeHeader
    {
        std::uint32_t magic{};
        std::uint16_t protocol{};
        std::uint16_t schema{};
        std::uint8_t encoding{};
        std::uint32_t uncompressed_bytes{};
        std::uint32_t payload_bytes{};
    };

    [[nodiscard]] bool read_header(simnet::ByteSpan bytes, EnvelopeHeader& header) noexcept
    {
        auto offset = std::size_t{};
        return simnet::read_big_endian(bytes, offset, header.magic) &&
               simnet::read_big_endian(bytes, offset, header.protocol) &&
               simnet::read_big_endian(bytes, offset, header.schema) &&
               simnet::read_byte(bytes, offset, header.encoding) &&
               simnet::read_big_endian(bytes, offset, header.uncompressed_bytes) &&
               simnet::read_big_endian(bytes, offset, header.payload_bytes) &&
               offset == simnet::compression_envelope_bytes;
    }

    void write_header(
        std::vector<simnet::Byte>& output,
        simnet::CompressionEncoding encoding,
        std::uint32_t uncompressed_bytes,
        std::uint32_t payload_bytes
    )
    {
        simnet::append_big_endian(output, compression_magic);
        simnet::append_big_endian(output, compression_protocol_version);
        simnet::append_big_endian(output, compression_schema_version);
        simnet::append_byte(output, static_cast<std::uint8_t>(encoding));
        simnet::append_big_endian(output, uncompressed_bytes);
        simnet::append_big_endian(output, payload_bytes);
    }

    [[nodiscard]] bool valid_limits(simnet::CompressionLimits limits) noexcept
    {
        return limits.max_uncompressed_bytes != 0U && limits.max_output_bytes != 0U;
    }

    [[nodiscard]] simnet::Nanoseconds now_ns() noexcept
    {
        return std::chrono::duration_cast<simnet::Nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        );
    }

    [[nodiscard]] std::uint64_t fnv1a_64(simnet::ByteSpan bytes) noexcept
    {
        auto hash = std::uint64_t{14695981039346656037ULL};
        for (auto const byte : bytes)
        {
            hash ^= static_cast<std::uint8_t>(byte);
            hash *= 1099511628211ULL;
        }
        return hash;
    }
}

namespace simnet
{
    struct ZstdDictionary::Impl
    {
        Impl(
            std::vector<Byte> dictionary_bytes,
            int compression_level,
            ZstdDictionaryExpectations expectations
        )
            : bytes(std::move(dictionary_bytes))
        {
            if (compression_level < 1 || compression_level > 19)
            {
                throw std::invalid_argument("Zstd dictionary compression level must be in [1, 19]");
            }
            if (bytes.empty() || bytes.size() > maximum_zstd_dictionary_bytes ||
                bytes.size() > std::numeric_limits<std::uint32_t>::max())
            {
                throw std::invalid_argument("Zstd dictionary byte count is outside bounds");
            }
            identity = {
                .dictionary_id = ZSTD_getDictID_fromDict(bytes.data(), bytes.size()),
                .byte_count = static_cast<std::uint32_t>(bytes.size()),
                .content_fingerprint = fnv1a_64(bytes),
            };
            if (identity.dictionary_id == 0U)
            {
                throw std::invalid_argument("Zstd dictionary is corrupt or has no dictionary ID");
            }
            if (identity.dictionary_id != expectations.dictionary_id ||
                identity.byte_count != expectations.byte_count ||
                identity.content_fingerprint != expectations.content_fingerprint)
            {
                throw std::invalid_argument("Zstd dictionary identity does not match expectations");
            }
            compression_dictionary =
                ZSTD_createCDict(bytes.data(), bytes.size(), compression_level);
            decompression_dictionary = ZSTD_createDDict(bytes.data(), bytes.size());
            if (compression_dictionary == nullptr || decompression_dictionary == nullptr)
            {
                ZSTD_freeCDict(compression_dictionary);
                ZSTD_freeDDict(decompression_dictionary);
                throw std::runtime_error("failed to prepare reusable Zstd dictionary state");
            }
        }

        ~Impl()
        {
            ZSTD_freeCDict(compression_dictionary);
            ZSTD_freeDDict(decompression_dictionary);
        }

        std::vector<Byte> bytes{};
        ZstdDictionaryIdentity identity{};
        ZSTD_CDict* compression_dictionary{};
        ZSTD_DDict* decompression_dictionary{};
    };

    struct ZstdCompressor::Impl
    {
        Impl() : context(ZSTD_createCCtx())
        {
            if (context == nullptr)
            {
                throw std::runtime_error("failed to create Zstd compression context");
            }
        }

        ~Impl()
        {
            ZSTD_freeCCtx(context);
        }

        ZSTD_CCtx* context{};
        std::vector<Byte> frame_scratch{};
    };

    struct ZstdDecompressor::Impl
    {
        Impl() : context(ZSTD_createDCtx())
        {
            if (context == nullptr)
            {
                throw std::runtime_error("failed to create Zstd decompression context");
            }
        }

        ~Impl()
        {
            ZSTD_freeDCtx(context);
        }

        ZSTD_DCtx* context{};
        std::vector<Byte> frame_scratch{};
    };

    ZstdDictionary::ZstdDictionary(
        std::vector<Byte> bytes,
        int compression_level,
        ZstdDictionaryExpectations expectations
    )
        : impl_(std::make_unique<Impl>(std::move(bytes), compression_level, expectations))
    {
    }

    ZstdDictionary::~ZstdDictionary() = default;
    ZstdDictionary::ZstdDictionary(ZstdDictionary&&) noexcept = default;
    ZstdDictionary& ZstdDictionary::operator=(ZstdDictionary&&) noexcept = default;

    ZstdDictionaryIdentity const& ZstdDictionary::identity() const noexcept
    {
        return impl_->identity;
    }

    ZstdCompressor::ZstdCompressor() : impl_(std::make_unique<Impl>())
    {
    }

    ZstdCompressor::~ZstdCompressor() = default;
    ZstdCompressor::ZstdCompressor(ZstdCompressor&&) noexcept = default;
    ZstdCompressor& ZstdCompressor::operator=(ZstdCompressor&&) noexcept = default;

    ZstdDecompressor::ZstdDecompressor() : impl_(std::make_unique<Impl>())
    {
    }

    ZstdDecompressor::~ZstdDecompressor() = default;
    ZstdDecompressor::ZstdDecompressor(ZstdDecompressor&&) noexcept = default;
    ZstdDecompressor& ZstdDecompressor::operator=(ZstdDecompressor&&) noexcept = default;

    bool has_compression_envelope(ByteSpan bytes) noexcept
    {
        if (bytes.size() < sizeof(std::uint32_t))
        {
            return false;
        }
        auto offset = std::size_t{};
        auto magic = std::uint32_t{};
        return simnet::read_big_endian(bytes, offset, magic) && magic == compression_magic;
    }

    CompressionReport compress_bytes(
        ZstdCompressor& compressor,
        ByteSpan input,
        int level,
        CompressionLimits limits,
        CompressionEnvelopePolicy policy,
        std::vector<Byte>& output
    )
    {
        auto report = CompressionReport{};
        if (!valid_limits(limits))
        {
            report.error = "compression limits must be positive";
            return report;
        }
        if (level < 1 || level > 19)
        {
            report.error = "Zstd compression level must be in [1, 19]";
            return report;
        }
        if (input.empty() || input.size() > limits.max_uncompressed_bytes ||
            input.size() > std::numeric_limits<std::uint32_t>::max())
        {
            report.error = "compression input byte count is outside contextual bounds";
            return report;
        }
        report.input_bytes = static_cast<std::uint32_t>(input.size());

        auto const bound = ZSTD_compressBound(input.size());
        if (ZSTD_isError(bound) != 0U || bound > compressor.impl_->frame_scratch.max_size())
        {
            report.error = "Zstd compression bound is invalid";
            return report;
        }

        auto const start = now_ns();
        compressor.impl_->frame_scratch.resize(bound);
        auto const compressed = ZSTD_compressCCtx(
            compressor.impl_->context,
            compressor.impl_->frame_scratch.data(),
            compressor.impl_->frame_scratch.size(),
            input.data(),
            input.size(),
            level
        );
        report.compression_cpu_time = now_ns() - start;
        if (ZSTD_isError(compressed) != 0U)
        {
            report.error = std::string{"Zstd compression failed: "} + ZSTD_getErrorName(compressed);
            return report;
        }
        if (compressed > std::numeric_limits<std::uint32_t>::max())
        {
            report.error = "Zstd payload byte count exceeds envelope capacity";
            return report;
        }

        auto const zstd_total = static_cast<std::uint64_t>(compression_envelope_bytes) + compressed;
        auto const use_zstd =
            policy == CompressionEnvelopePolicy::Always
                ? compressed < input.size()
                : zstd_total < input.size() && zstd_total <= limits.max_output_bytes;

        if (policy == CompressionEnvelopePolicy::OnlyWhenSmaller && !use_zstd)
        {
            if (input.size() > limits.max_output_bytes)
            {
                report.error = "raw compression fallback exceeds contextual output bound";
                return report;
            }
            output.assign(input.begin(), input.end());
            report.encoding = CompressionEncoding::Raw;
            report.encoded_payload_bytes = report.input_bytes;
            report.output_bytes = report.input_bytes;
            report.valid = true;
            return report;
        }

        auto const payload_bytes = use_zstd ? compressed : input.size();
        auto const total_bytes =
            static_cast<std::uint64_t>(compression_envelope_bytes) + payload_bytes;
        if (total_bytes > limits.max_output_bytes ||
            total_bytes > std::numeric_limits<std::uint32_t>::max())
        {
            report.error = "compression envelope exceeds contextual output bound";
            return report;
        }

        report.encoding = use_zstd ? CompressionEncoding::Zstd : CompressionEncoding::Raw;
        report.encoded_payload_bytes = static_cast<std::uint32_t>(payload_bytes);
        report.envelope_bytes = compression_envelope_bytes;
        report.output_bytes = static_cast<std::uint32_t>(total_bytes);
        output.clear();
        output.reserve(report.output_bytes);
        write_header(output, report.encoding, report.input_bytes, report.encoded_payload_bytes);
        if (use_zstd)
        {
            output.insert(
                output.end(),
                compressor.impl_->frame_scratch.begin(),
                compressor.impl_->frame_scratch.begin() + static_cast<std::ptrdiff_t>(compressed)
            );
        }
        else
        {
            output.insert(output.end(), input.begin(), input.end());
        }
        report.valid = true;
        return report;
    }

    CompressionReport compress_bytes_with_dictionary(
        ZstdCompressor& compressor,
        ZstdDictionary const& dictionary,
        ByteSpan input,
        CompressionLimits limits,
        std::vector<Byte>& output
    )
    {
        auto report = CompressionReport{};
        if (!valid_limits(limits))
        {
            report.error = "compression limits must be positive";
            return report;
        }
        if (input.empty() || input.size() > limits.max_uncompressed_bytes ||
            input.size() > std::numeric_limits<std::uint32_t>::max())
        {
            report.error = "compression input byte count is outside contextual bounds";
            return report;
        }
        report.input_bytes = static_cast<std::uint32_t>(input.size());

        auto const bound = ZSTD_compressBound(input.size());
        if (ZSTD_isError(bound) != 0U || bound > compressor.impl_->frame_scratch.max_size())
        {
            report.error = "Zstd dictionary compression bound is invalid";
            return report;
        }

        auto const start = now_ns();
        compressor.impl_->frame_scratch.resize(bound);
        auto const compressed = ZSTD_compress_usingCDict(
            compressor.impl_->context,
            compressor.impl_->frame_scratch.data(),
            compressor.impl_->frame_scratch.size(),
            input.data(),
            input.size(),
            dictionary.impl_->compression_dictionary
        );
        report.compression_cpu_time = now_ns() - start;
        if (ZSTD_isError(compressed) != 0U)
        {
            report.error =
                std::string{"Zstd dictionary compression failed: "} + ZSTD_getErrorName(compressed);
            return report;
        }
        if (compressed > std::numeric_limits<std::uint32_t>::max())
        {
            report.error = "Zstd dictionary payload byte count exceeds envelope capacity";
            return report;
        }

        auto const dictionary_total =
            static_cast<std::uint64_t>(compression_envelope_bytes) + compressed;
        auto const use_dictionary =
            dictionary_total < input.size() && dictionary_total <= limits.max_output_bytes;
        auto const payload_bytes = use_dictionary ? compressed : input.size();
        auto const total_bytes =
            static_cast<std::uint64_t>(compression_envelope_bytes) + payload_bytes;
        if (total_bytes > limits.max_output_bytes ||
            total_bytes > std::numeric_limits<std::uint32_t>::max())
        {
            report.error = "dictionary compression envelope exceeds contextual output bound";
            return report;
        }

        report.encoding =
            use_dictionary ? CompressionEncoding::ZstdDictionary : CompressionEncoding::Raw;
        report.encoded_payload_bytes = static_cast<std::uint32_t>(payload_bytes);
        report.envelope_bytes = compression_envelope_bytes;
        report.output_bytes = static_cast<std::uint32_t>(total_bytes);
        output.clear();
        output.reserve(report.output_bytes);
        write_header(output, report.encoding, report.input_bytes, report.encoded_payload_bytes);
        if (use_dictionary)
        {
            output.insert(
                output.end(),
                compressor.impl_->frame_scratch.begin(),
                compressor.impl_->frame_scratch.begin() + static_cast<std::ptrdiff_t>(compressed)
            );
        }
        else
        {
            output.insert(output.end(), input.begin(), input.end());
        }
        report.valid = true;
        return report;
    }

    DecompressionReport decompress_bytes(
        ZstdDecompressor& decompressor,
        ByteSpan input,
        CompressionLimits limits,
        std::vector<Byte>& output
    )
    {
        auto report = DecompressionReport{};
        if (!valid_limits(limits))
        {
            report.error = "decompression limits must be positive";
            return report;
        }
        if (input.size() < compression_envelope_bytes || input.size() > limits.max_output_bytes ||
            input.size() > std::numeric_limits<std::uint32_t>::max())
        {
            report.error = "compression envelope byte count is outside contextual bounds";
            return report;
        }
        report.input_bytes = static_cast<std::uint32_t>(input.size());

        auto header = EnvelopeHeader{};
        if (!read_header(input, header) || header.magic != compression_magic ||
            header.protocol != compression_protocol_version ||
            header.schema != compression_schema_version ||
            (header.encoding != static_cast<std::uint8_t>(CompressionEncoding::Raw) &&
             header.encoding != static_cast<std::uint8_t>(CompressionEncoding::Zstd) &&
             header.encoding != static_cast<std::uint8_t>(CompressionEncoding::ZstdDictionary)))
        {
            report.error = "compression envelope identity or version is invalid";
            return report;
        }
        if (header.uncompressed_bytes == 0U || header.payload_bytes == 0U ||
            header.uncompressed_bytes > limits.max_uncompressed_bytes)
        {
            report.error = "compression envelope sizes are outside contextual bounds";
            return report;
        }
        auto const total_bytes =
            static_cast<std::uint64_t>(compression_envelope_bytes) + header.payload_bytes;
        if (total_bytes != input.size() || total_bytes > limits.max_output_bytes)
        {
            report.error = "compression envelope payload size is inconsistent";
            return report;
        }

        report.encoding = static_cast<CompressionEncoding>(header.encoding);
        report.encoded_payload_bytes = header.payload_bytes;
        report.envelope_bytes = compression_envelope_bytes;
        auto const payload = input.subspan(compression_envelope_bytes);
        if (report.encoding == CompressionEncoding::Raw)
        {
            if (header.payload_bytes != header.uncompressed_bytes)
            {
                report.error = "Raw envelope sizes must match";
                return report;
            }
            auto const start = now_ns();
            output.assign(payload.begin(), payload.end());
            report.decompression_cpu_time = now_ns() - start;
            report.output_bytes = header.uncompressed_bytes;
            report.valid = true;
            return report;
        }
        if (report.encoding == CompressionEncoding::ZstdDictionary)
        {
            report.error = "Zstd dictionary envelope requires a dictionary context";
            return report;
        }

        auto const content_size = ZSTD_getFrameContentSize(payload.data(), payload.size());
        if (content_size == ZSTD_CONTENTSIZE_ERROR || content_size == ZSTD_CONTENTSIZE_UNKNOWN ||
            content_size != header.uncompressed_bytes)
        {
            report.error = "Zstd frame content size is invalid or inconsistent";
            return report;
        }
        auto const frame_size = ZSTD_findFrameCompressedSize(payload.data(), payload.size());
        if (ZSTD_isError(frame_size) != 0U || frame_size != payload.size())
        {
            report.error = "Zstd frame is truncated or has trailing data";
            return report;
        }

        output.resize(header.uncompressed_bytes);
        auto const start = now_ns();
        auto const decompressed = ZSTD_decompressDCtx(
            decompressor.impl_->context,
            output.data(),
            output.size(),
            payload.data(),
            payload.size()
        );
        report.decompression_cpu_time = now_ns() - start;
        if (ZSTD_isError(decompressed) != 0U || decompressed != header.uncompressed_bytes)
        {
            output.clear();
            report.error =
                ZSTD_isError(decompressed) != 0U
                    ? std::string{"Zstd decompression failed: "} + ZSTD_getErrorName(decompressed)
                    : "Zstd decompressed byte count is inconsistent";
            return report;
        }
        report.output_bytes = header.uncompressed_bytes;
        report.valid = true;
        return report;
    }

    DecompressionReport decompress_bytes_with_dictionary(
        ZstdDecompressor& decompressor,
        ZstdDictionary const& dictionary,
        ByteSpan input,
        CompressionLimits limits,
        std::vector<Byte>& output
    )
    {
        auto report = DecompressionReport{};
        if (!valid_limits(limits))
        {
            report.error = "decompression limits must be positive";
            return report;
        }
        if (input.size() < compression_envelope_bytes || input.size() > limits.max_output_bytes ||
            input.size() > std::numeric_limits<std::uint32_t>::max())
        {
            report.error = "compression envelope byte count is outside contextual bounds";
            return report;
        }
        report.input_bytes = static_cast<std::uint32_t>(input.size());

        auto header = EnvelopeHeader{};
        if (!read_header(input, header) || header.magic != compression_magic ||
            header.protocol != compression_protocol_version ||
            header.schema != compression_schema_version ||
            (header.encoding != static_cast<std::uint8_t>(CompressionEncoding::Raw) &&
             header.encoding != static_cast<std::uint8_t>(CompressionEncoding::ZstdDictionary)))
        {
            report.error = "dictionary compression envelope identity or version is invalid";
            return report;
        }
        if (header.uncompressed_bytes == 0U || header.payload_bytes == 0U ||
            header.uncompressed_bytes > limits.max_uncompressed_bytes)
        {
            report.error = "dictionary compression envelope sizes are outside contextual bounds";
            return report;
        }
        auto const total_bytes =
            static_cast<std::uint64_t>(compression_envelope_bytes) + header.payload_bytes;
        if (total_bytes != input.size() || total_bytes > limits.max_output_bytes)
        {
            report.error = "dictionary compression envelope payload size is inconsistent";
            return report;
        }

        report.encoding = static_cast<CompressionEncoding>(header.encoding);
        report.encoded_payload_bytes = header.payload_bytes;
        report.envelope_bytes = compression_envelope_bytes;
        auto const payload = input.subspan(compression_envelope_bytes);
        if (report.encoding == CompressionEncoding::Raw)
        {
            if (header.payload_bytes != header.uncompressed_bytes)
            {
                report.error = "Raw envelope sizes must match";
                return report;
            }
            auto const start = now_ns();
            decompressor.impl_->frame_scratch.assign(payload.begin(), payload.end());
            report.decompression_cpu_time = now_ns() - start;
            output = decompressor.impl_->frame_scratch;
            report.output_bytes = header.uncompressed_bytes;
            report.valid = true;
            return report;
        }

        auto const content_size = ZSTD_getFrameContentSize(payload.data(), payload.size());
        if (content_size == ZSTD_CONTENTSIZE_ERROR || content_size == ZSTD_CONTENTSIZE_UNKNOWN ||
            content_size != header.uncompressed_bytes)
        {
            report.error = "Zstd dictionary frame content size is invalid or inconsistent";
            return report;
        }
        auto const frame_size = ZSTD_findFrameCompressedSize(payload.data(), payload.size());
        if (ZSTD_isError(frame_size) != 0U || frame_size != payload.size())
        {
            report.error = "Zstd dictionary frame is truncated or has trailing data";
            return report;
        }
        auto const frame_dictionary_id = ZSTD_getDictID_fromFrame(payload.data(), payload.size());
        if (frame_dictionary_id == 0U ||
            frame_dictionary_id != dictionary.impl_->identity.dictionary_id)
        {
            report.error = "Zstd frame dictionary ID does not match the supplied dictionary";
            return report;
        }

        decompressor.impl_->frame_scratch.resize(header.uncompressed_bytes);
        auto const start = now_ns();
        auto const decompressed = ZSTD_decompress_usingDDict(
            decompressor.impl_->context,
            decompressor.impl_->frame_scratch.data(),
            decompressor.impl_->frame_scratch.size(),
            payload.data(),
            payload.size(),
            dictionary.impl_->decompression_dictionary
        );
        report.decompression_cpu_time = now_ns() - start;
        if (ZSTD_isError(decompressed) != 0U || decompressed != header.uncompressed_bytes)
        {
            report.error = ZSTD_isError(decompressed) != 0U
                               ? std::string{"Zstd dictionary decompression failed: "} +
                                     ZSTD_getErrorName(decompressed)
                               : "Zstd dictionary decompressed byte count is inconsistent";
            return report;
        }
        output = decompressor.impl_->frame_scratch;
        report.output_bytes = header.uncompressed_bytes;
        report.valid = true;
        return report;
    }
}
