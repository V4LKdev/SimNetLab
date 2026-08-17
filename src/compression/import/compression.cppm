module;

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// @brief Bounded versioned byte envelopes for Raw and Zstd payloads.
export module simnet.compression;

import simnet.core;

export namespace simnet
{
    /// SNCZ envelope byte size including header-only framing.
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

    /// Bounded byte limits shared by compressor and decompressor call sites.
    struct CompressionLimits
    {
        std::uint32_t max_uncompressed_bytes{};
        std::uint32_t max_output_bytes{};
    };

    /// Per-call compression outcome and timing contract.
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

    /// Per-call decompression outcome and timing contract.
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

    class ZstdCompressor;
    class ZstdDecompressor;

    /// Owns reusable standard Zstd compression context and frame scratch.
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

    /// Owns reusable standard Zstd decompression context and frame scratch.
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

    /// Compresses bounded bytes into raw or ordinary Zstd envelopes, selecting
    /// the active encoding by policy and bound safety. Output is rewritten only
    /// for a successful report.
    [[nodiscard]] CompressionReport compress_bytes(
        ZstdCompressor& compressor,
        ByteSpan input,
        int level,
        CompressionLimits limits,
        CompressionEnvelopePolicy policy,
        std::vector<Byte>& output
    );

    /// Decodes Raw and ordinary Zstd envelopes after complete envelope,
    /// size, frame, and content-size validation.
    /// Commits caller output only when the report is valid.
    [[nodiscard]] DecompressionReport decompress_bytes(
        ZstdDecompressor& decompressor,
        ByteSpan input,
        CompressionLimits limits,
        std::vector<Byte>& output
    );
}
