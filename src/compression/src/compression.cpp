module;

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
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

    void write_compression_envelope(
        simnet::CompressionReport& report,
        std::vector<simnet::Byte>& output,
        simnet::CompressionEncoding encoding,
        simnet::ByteSpan payload
    )
    {
        report.encoding = encoding;
        report.encoded_payload_bytes = static_cast<std::uint32_t>(payload.size());
        report.envelope_bytes = simnet::compression_envelope_bytes;
        report.output_bytes = report.envelope_bytes + report.encoded_payload_bytes;
        output.clear();
        output.reserve(report.output_bytes);
        write_header(output, report.encoding, report.input_bytes, report.encoded_payload_bytes);
        output.insert(output.end(), payload.begin(), payload.end());
        report.valid = true;
    }

    [[nodiscard]] bool valid_limits(simnet::CompressionLimits limits) noexcept
    {
        return limits.max_uncompressed_bytes != 0U && limits.max_output_bytes != 0U;
    }

    [[nodiscard]] bool validate_compression_input(
        simnet::ByteSpan input,
        simnet::CompressionLimits limits,
        simnet::CompressionReport& report
    )
    {
        if (input.empty() || input.size() > limits.max_uncompressed_bytes ||
            input.size() > std::numeric_limits<std::uint32_t>::max())
        {
            report.error = "compression input byte count is outside contextual bounds";
            return false;
        }
        report.input_bytes = static_cast<std::uint32_t>(input.size());
        return true;
    }

    [[nodiscard]] bool validate_decompression_input(
        simnet::ByteSpan input,
        simnet::CompressionLimits limits,
        simnet::DecompressionReport& report
    )
    {
        if (!valid_limits(limits))
        {
            report.error = "decompression limits must be positive";
            return false;
        }
        if (input.size() < simnet::compression_envelope_bytes ||
            input.size() > limits.max_output_bytes ||
            input.size() > std::numeric_limits<std::uint32_t>::max())
        {
            report.error = "compression envelope byte count is outside contextual bounds";
            return false;
        }
        report.input_bytes = static_cast<std::uint32_t>(input.size());
        return true;
    }

    [[nodiscard]] bool valid_header_identity(EnvelopeHeader const& header) noexcept
    {
        return header.magic == compression_magic &&
               header.protocol == compression_protocol_version &&
               header.schema == compression_schema_version;
    }

    [[nodiscard]] bool envelope_sizes_within_bounds(
        EnvelopeHeader const& header,
        simnet::CompressionLimits limits
    ) noexcept
    {
        return header.uncompressed_bytes != 0U && header.payload_bytes != 0U &&
               header.uncompressed_bytes <= limits.max_uncompressed_bytes;
    }

    [[nodiscard]] bool envelope_payload_size_matches(
        EnvelopeHeader const& header,
        simnet::ByteSpan input,
        simnet::CompressionLimits limits
    ) noexcept
    {
        auto const total_bytes =
            static_cast<std::uint64_t>(simnet::compression_envelope_bytes) + header.payload_bytes;
        return total_bytes == input.size() && total_bytes <= limits.max_output_bytes;
    }

    [[nodiscard]] bool
    frame_content_size_matches(simnet::ByteSpan payload, std::uint32_t expected_bytes) noexcept
    {
        auto const content_size = ZSTD_getFrameContentSize(payload.data(), payload.size());
        return content_size != ZSTD_CONTENTSIZE_ERROR && content_size != ZSTD_CONTENTSIZE_UNKNOWN &&
               content_size == expected_bytes;
    }

    [[nodiscard]] bool contains_one_complete_frame(simnet::ByteSpan payload) noexcept
    {
        auto const frame_size = ZSTD_findFrameCompressedSize(payload.data(), payload.size());
        return ZSTD_isError(frame_size) == 0U && frame_size == payload.size();
    }

    [[nodiscard]] simnet::Nanoseconds now_ns() noexcept
    {
        return std::chrono::duration_cast<simnet::Nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        );
    }
}

namespace simnet
{
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
        if (!validate_compression_input(input, limits, report))
        {
            return report;
        }

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

        auto const payload =
            use_zstd ? ByteSpan{compressor.impl_->frame_scratch}.first(compressed) : input;
        write_compression_envelope(
            report,
            output,
            use_zstd ? CompressionEncoding::Zstd : CompressionEncoding::Raw,
            payload
        );
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
        if (!validate_decompression_input(input, limits, report))
        {
            return report;
        }

        auto header = EnvelopeHeader{};
        if (!read_header(input, header) || !valid_header_identity(header) ||
            (header.encoding != static_cast<std::uint8_t>(CompressionEncoding::Raw) &&
             header.encoding != static_cast<std::uint8_t>(CompressionEncoding::Zstd)))
        {
            report.error = "compression envelope identity or version is invalid";
            return report;
        }
        if (!envelope_sizes_within_bounds(header, limits))
        {
            report.error = "compression envelope sizes are outside contextual bounds";
            return report;
        }
        if (!envelope_payload_size_matches(header, input, limits))
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
        if (!frame_content_size_matches(payload, header.uncompressed_bytes))
        {
            report.error = "Zstd frame content size is invalid or inconsistent";
            return report;
        }
        if (!contains_one_complete_frame(payload))
        {
            report.error = "Zstd frame is truncated or has trailing data";
            return report;
        }

        decompressor.impl_->frame_scratch.resize(header.uncompressed_bytes);
        auto const start = now_ns();
        auto const decompressed = ZSTD_decompressDCtx(
            decompressor.impl_->context,
            decompressor.impl_->frame_scratch.data(),
            decompressor.impl_->frame_scratch.size(),
            payload.data(),
            payload.size()
        );
        report.decompression_cpu_time = now_ns() - start;
        if (ZSTD_isError(decompressed) != 0U || decompressed != header.uncompressed_bytes)
        {
            report.error =
                ZSTD_isError(decompressed) != 0U
                    ? std::string{"Zstd decompression failed: "} + ZSTD_getErrorName(decompressed)
                    : "Zstd decompressed byte count is inconsistent";
            return report;
        }
        output = decompressor.impl_->frame_scratch;
        report.output_bytes = header.uncompressed_bytes;
        report.valid = true;
        return report;
    }
}
