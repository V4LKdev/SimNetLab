module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

module simnet.pipeline:wire;

import :types;
import simnet.core;
import simnet.snapshot;

/**
 * Private wire encoding primitives. All integer fields are written in network byte order (MSB first).
 * Encoded update headers and records are serialized field-by-field. No memcopies.
 */
namespace simnet::pipeline_wire
{
    // --- Constants ---

    // Magic 'SNPL' (SimNet Pipeline Layout) used to reject invalid encoded updates.
    inline constexpr std::uint32_t encoded_update_magic = 0x534E504Cu; // S N P L
    inline constexpr std::uint16_t protocol_version = 1;
    inline constexpr std::uint16_t schema_version = 5;
    inline constexpr std::uint16_t field_mask_schema_version = 6;

    inline constexpr std::uint8_t classification_field_mask = 0x01U;
    inline constexpr std::uint8_t position_field_mask = 0x02U;
    inline constexpr std::uint8_t heading_field_mask = 0x04U;
    inline constexpr std::uint8_t hue_field_mask = 0x08U;
    inline constexpr std::uint8_t existing_field_mask =
        classification_field_mask | position_field_mask | heading_field_mask | hue_field_mask;
    inline constexpr std::uint8_t spawn_record_selector = 0x80U;

    // Field sizes
    inline constexpr std::uint32_t u8_bytes = 1;
    inline constexpr std::uint32_t u16_bytes = 2;
    inline constexpr std::uint32_t u32_bytes = 4;
    inline constexpr std::uint32_t u64_bytes = 8;
    inline constexpr std::uint32_t f32_bytes = 4;
    inline constexpr std::uint32_t vec3_bytes = 3 * f32_bytes;

    // Record sizes for each supported layout
    inline constexpr std::uint32_t raw_record_bytes = u32_bytes    // id
                                                      + u8_bytes   // classification
                                                      + vec3_bytes // position
                                                      + vec3_bytes // heading
                                                      + u8_bytes;  // hue
    // 30

    inline constexpr std::uint32_t quantized_record_bytes = u32_bytes         //  id
                                                            + u8_bytes        //  classification
                                                            + (3 * u16_bytes) //  position
                                                            + (3 * u16_bytes) //  heading
                                                            + u8_bytes;       //  hue
    // 18

    inline constexpr std::uint32_t quantized_oct_record_bytes = u32_bytes         //  id
                                                                + u8_bytes        //  classification
                                                                + (3 * u16_bytes) //  position
                                                                + (2 * u16_bytes) //  oct heading
                                                                + u8_bytes;       //  hue
    // 16

    inline constexpr std::uint32_t bitpacked_quantized_oct_record_bits = 32  //  id
                                                                         + 8 //  classification
                                                                         + (3 * 16) //  position
                                                                         + (2 * 16) //  oct heading
                                                                         + 8;       //  hue
    // 128 bit

    inline constexpr std::uint32_t bitpacked_quantized_oct_record_bytes =
        (bitpacked_quantized_oct_record_bits + 7) / 8;
    // 16

    inline constexpr std::uint32_t delete_record_bytes = u32_bytes; // 4
    inline constexpr std::uint32_t masked_upsert_minimum_bytes = u32_bytes + u8_bytes + u8_bytes;

    inline constexpr std::uint32_t header_bytes = u32_bytes    //  magic
                                                  + u16_bytes  //  protocol
                                                  + u16_bytes  //  schema
                                                  + u64_bytes  //  decode_signature
                                                  + u8_bytes   //  snapshot_kind
                                                  + u64_bytes  //  tick
                                                  + u32_bytes  //  sequence
                                                  + u32_bytes  //  baseline_sequence
                                                  + u32_bytes  //  upsert_count
                                                  + u32_bytes  //  delete_count
                                                  + u32_bytes; //  payload_bytes
    // 45

    static_assert(raw_record_bytes == 30);
    static_assert(quantized_record_bytes == 18);
    static_assert(quantized_oct_record_bytes == 16);
    static_assert(bitpacked_quantized_oct_record_bits == 128);
    static_assert(bitpacked_quantized_oct_record_bytes == 16);
    static_assert(delete_record_bytes == 4);
    static_assert(masked_upsert_minimum_bytes == 6);
    static_assert(existing_field_mask == 0x0FU);
    static_assert((spawn_record_selector & existing_field_mask) == 0U);
    static_assert(header_bytes == 45);
    static_assert(sizeof(float) == f32_bytes);
    static_assert(std::numeric_limits<float>::is_iec559);

    // --- Header struct decoded ---

    /// Private encoded update header serialized field-by-field in network byte order.
    struct EncodedUpdateHeader
    {
        std::uint32_t magic{};
        std::uint16_t protocol{};
        std::uint16_t schema{};
        std::uint64_t decode_signature{};
        SnapshotKind snapshot_kind{SnapshotKind::FullReplace};
        Tick tick{};
        SequenceId sequence{};
        SequenceId baseline_sequence{};
        std::uint32_t upsert_count{};
        std::uint32_t delete_count{};
        std::uint32_t payload_bytes{};
    };

    [[nodiscard]] constexpr bool field_mask_enabled(PipelineDefinition const& pipeline) noexcept
    {
        return has_all_flags(pipeline.techniques, PipelineTechniqueFlags::DeltaFieldMask);
    }

    [[nodiscard]] constexpr std::uint16_t
    encoded_update_schema(PipelineDefinition const& pipeline) noexcept
    {
        return field_mask_enabled(pipeline) ? field_mask_schema_version : schema_version;
    }

    /// Serializes the full encoded update header.
    void write_header(std::vector<Byte>& bytes, EncodedUpdateHeader const& header)
    {
        append_big_endian(bytes, header.magic);
        append_big_endian(bytes, header.protocol);
        append_big_endian(bytes, header.schema);
        append_big_endian(bytes, header.decode_signature);
        append_byte(bytes, static_cast<std::uint8_t>(header.snapshot_kind));
        append_big_endian(bytes, header.tick);
        append_big_endian(bytes, header.sequence);
        append_big_endian(bytes, header.baseline_sequence);
        append_big_endian(bytes, header.upsert_count);
        append_big_endian(bytes, header.delete_count);
        append_big_endian(bytes, header.payload_bytes);
    }

    /// Serializes a 3D vector as three floats.
    void write_vec3(std::vector<Byte>& bytes, Vec3f value)
    {
        append_float32_big_endian(bytes, value.x);
        append_float32_big_endian(bytes, value.y);
        append_float32_big_endian(bytes, value.z);
    }

    /// Reads the full encoded update header, advancing offset. Returns false on truncation.
    bool read_header(ByteSpan bytes, EncodedUpdateHeader& header)
    {
        auto offset = std::size_t{};
        auto snapshot_kind = std::uint8_t{};

        if (!read_big_endian(bytes, offset, header.magic) ||
            !read_big_endian(bytes, offset, header.protocol) ||
            !read_big_endian(bytes, offset, header.schema) ||
            !read_big_endian(bytes, offset, header.decode_signature) ||
            !read_byte(bytes, offset, snapshot_kind) ||
            !read_big_endian(bytes, offset, header.tick) ||
            !read_big_endian(bytes, offset, header.sequence) ||
            !read_big_endian(bytes, offset, header.baseline_sequence) ||
            !read_big_endian(bytes, offset, header.upsert_count) ||
            !read_big_endian(bytes, offset, header.delete_count) ||
            !read_big_endian(bytes, offset, header.payload_bytes))
        {
            return false;
        }

        header.snapshot_kind = static_cast<SnapshotKind>(snapshot_kind);
        return true;
    }

    /// Reads a 3D vector as three floats in big-endian.
    bool read_vec3(ByteSpan bytes, std::size_t& offset, Vec3f& value)
    {
        return read_float32_big_endian(bytes, offset, value.x) &&
               read_float32_big_endian(bytes, offset, value.y) &&
               read_float32_big_endian(bytes, offset, value.z);
    }
}
