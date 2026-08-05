module;

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// @brief Bounded versioned Raw and Zstd byte envelopes.
export module simnet.compression;

import simnet.core;

export namespace simnet
{
    inline constexpr std::uint32_t compression_envelope_bytes = 17U;

    enum class CompressionEncoding : std::uint8_t
    {
        Raw = 0U,
        Zstd = 1U,
    };

    enum class CompressionEnvelopePolicy : std::uint8_t
    {
        Always,
        OnlyWhenSmaller,
    };

    struct CompressionLimits
    {
        std::uint32_t max_uncompressed_bytes{};
        std::uint32_t max_output_bytes{};
    };

    struct CompressionReport
    {
        CompressionEncoding encoding{CompressionEncoding::Raw};
        std::uint32_t input_bytes{};
        std::uint32_t encoded_payload_bytes{};
        std::uint32_t envelope_bytes{};
        std::uint32_t output_bytes{};
        Nanoseconds compression_cpu_time{};
        bool valid{};
        std::string error{};
    };

    struct DecompressionReport
    {
        CompressionEncoding encoding{CompressionEncoding::Raw};
        std::uint32_t input_bytes{};
        std::uint32_t encoded_payload_bytes{};
        std::uint32_t envelope_bytes{};
        std::uint32_t output_bytes{};
        Nanoseconds decompression_cpu_time{};
        bool valid{};
        std::string error{};
    };

    class ZstdCompressor
    {
    public:
        ZstdCompressor();
        ~ZstdCompressor();

        ZstdCompressor(ZstdCompressor&&) noexcept;
        ZstdCompressor& operator=(ZstdCompressor&&) noexcept;

        ZstdCompressor(ZstdCompressor const&) = delete;
        ZstdCompressor& operator=(ZstdCompressor const&) = delete;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        friend CompressionReport compress_bytes(
            ZstdCompressor&,
            ByteSpan,
            int,
            CompressionLimits,
            CompressionEnvelopePolicy,
            std::vector<Byte>&
        );
    };

    class ZstdDecompressor
    {
    public:
        ZstdDecompressor();
        ~ZstdDecompressor();

        ZstdDecompressor(ZstdDecompressor&&) noexcept;
        ZstdDecompressor& operator=(ZstdDecompressor&&) noexcept;

        ZstdDecompressor(ZstdDecompressor const&) = delete;
        ZstdDecompressor& operator=(ZstdDecompressor const&) = delete;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        friend DecompressionReport
        decompress_bytes(ZstdDecompressor&, ByteSpan, CompressionLimits, std::vector<Byte>&);
    };

    [[nodiscard]] bool has_compression_envelope(ByteSpan bytes) noexcept;

    [[nodiscard]] CompressionReport compress_bytes(
        ZstdCompressor& compressor,
        ByteSpan input,
        int level,
        CompressionLimits limits,
        CompressionEnvelopePolicy policy,
        std::vector<Byte>& output
    );

    [[nodiscard]] DecompressionReport decompress_bytes(
        ZstdDecompressor& decompressor,
        ByteSpan input,
        CompressionLimits limits,
        std::vector<Byte>& output
    );
}
