#include <zdict.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace
{

    using Clock = std::chrono::steady_clock;
    using Bytes = std::vector<std::uint8_t>;

    struct Config
    {
        std::size_t entities = 2'000;
        std::size_t train_ticks = 48;
        std::size_t test_ticks = 30;
        std::size_t repeats = 3;
        std::size_t mtu = 1'500;
        std::size_t headroom = 300;
        std::size_t dict_size = 8 * 1024;
        int zstd_level = 1;
        float world_half = 400.0F;
        float speed = 8.0F;
        float dt = 0.05F;
        std::uint32_t seed = 0x51A7B01Du;
        double loss_rate = 0.01;
        std::filesystem::path csv = "network_probe.csv";
    };

    struct Vec3
    {
        float x{};
        float y{};
        float z{};
    };

    struct Boid
    {
        std::uint32_t id{};
        Vec3 position{};
        Vec3 heading{};
        std::uint8_t hue{};
    };

    struct QuantizedBoid
    {
        std::uint32_t id{};
        std::array<std::uint16_t, 3> position{};
        std::array<std::int16_t, 2> heading{};
        std::uint8_t hue{};
    };

    struct EncodedSnapshot
    {
        std::string name;
        Bytes bytes;
        std::vector<std::size_t> record_offsets; // entities + 1
    };

    struct PacketSlice
    {
        std::size_t offset{};
        std::size_t size{};
        std::size_t entities{};
    };

    struct Measurement
    {
        std::string encoding;
        std::string compression;
        std::string scope;
        std::size_t entities{};
        std::size_t source_bytes{};
        std::size_t payload_bytes{};
        std::size_t packets{};
        double ratio{};
        double entities_per_packet{};
        std::size_t min_entities_per_packet{};
        std::size_t max_entities_per_packet{};
        double compress_us{};
        double decompress_us{};
        double complete_probability{};
        double expected_usable_fraction{};
    };

    [[noreturn]] void fail(std::string_view message)
    {
        throw std::runtime_error(std::string(message));
    }

    std::size_t parse_size(std::string_view text, std::string_view option)
    {
        std::size_t pos = 0;
        const auto result = std::stoull(std::string(text), &pos, 0);
        if (pos != text.size()) {
            fail(std::string("Invalid value for ") + std::string(option));
        }
        return static_cast<std::size_t>(result);
    }

    double parse_double(std::string_view text, std::string_view option)
    {
        std::size_t pos = 0;
        const auto result = std::stod(std::string(text), &pos);
        if (pos != text.size()) {
            fail(std::string("Invalid value for ") + std::string(option));
        }
        return result;
    }

    Config parse_args(int argc, char** argv)
    {
        Config cfg;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            auto value = [&](std::string_view option) -> std::string_view {
                if (i + 1 >= argc) {
                    fail(std::string("Missing value after ") + std::string(option));
                }
                return argv[++i];
            };

            if (arg == "--entities")
                cfg.entities = parse_size(value(arg), arg);
            else if (arg == "--train-ticks")
                cfg.train_ticks = parse_size(value(arg), arg);
            else if (arg == "--test-ticks")
                cfg.test_ticks = parse_size(value(arg), arg);
            else if (arg == "--repeats")
                cfg.repeats = parse_size(value(arg), arg);
            else if (arg == "--mtu")
                cfg.mtu = parse_size(value(arg), arg);
            else if (arg == "--headroom")
                cfg.headroom = parse_size(value(arg), arg);
            else if (arg == "--dict-size")
                cfg.dict_size = parse_size(value(arg), arg);
            else if (arg == "--zstd-level")
                cfg.zstd_level = static_cast<int>(parse_size(value(arg), arg));
            else if (arg == "--world-half")
                cfg.world_half = static_cast<float>(parse_double(value(arg), arg));
            else if (arg == "--speed")
                cfg.speed = static_cast<float>(parse_double(value(arg), arg));
            else if (arg == "--dt")
                cfg.dt = static_cast<float>(parse_double(value(arg), arg));
            else if (arg == "--seed")
                cfg.seed = static_cast<std::uint32_t>(parse_size(value(arg), arg));
            else if (arg == "--loss")
                cfg.loss_rate = parse_double(value(arg), arg);
            else if (arg == "--csv")
                cfg.csv = value(arg);
            else if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: network_probe [options]\n"
                          << "  --entities N       boids in each snapshot (default 2000)\n"
                          << "  --train-ticks N    separate samples for dictionary training (48)\n"
                          << "  --test-ticks N     benchmark snapshots (30)\n"
                          << "  --repeats N        compression timing repeats per snapshot (3)\n"
                          << "  --mtu N            packet MTU (1500)\n"
                          << "  --headroom N       reserved IP/UDP/ENet/protocol bytes (300)\n"
                          << "  --dict-size N      trained dictionary bytes (8192)\n"
                          << "  --zstd-level N     Zstd compression level (1)\n"
                          << "  --world-half F     bounded world half extent (400)\n"
                          << "  --speed F          boid speed units/s (8)\n"
                          << "  --dt F             update interval seconds (0.05)\n"
                          << "  --loss F           independent packet-loss estimate (0.01)\n"
                          << "  --csv PATH         summary CSV path\n";
                std::exit(0);
            } else {
                fail(std::string("Unknown option: ") + std::string(arg));
            }
        }

        if (cfg.entities == 0 || cfg.train_ticks == 0 || cfg.test_ticks == 0 || cfg.repeats == 0) {
            fail("entities, train-ticks, test-ticks, and repeats must be positive");
        }
        if (cfg.mtu <= cfg.headroom)
            fail("MTU must be larger than headroom");
        if (cfg.world_half <= 0 || cfg.speed < 0 || cfg.dt <= 0)
            fail("invalid simulation settings");
        if (cfg.loss_rate < 0.0 || cfg.loss_rate >= 1.0)
            fail("loss must be in [0, 1)");
        return cfg;
    }

    Vec3 normalize(Vec3 v)
    {
        const float length_sq = v.x * v.x + v.y * v.y + v.z * v.z;
        if (length_sq <= 1e-12F)
            return {1.0F, 0.0F, 0.0F};
        const float inv = 1.0F / std::sqrt(length_sq);
        return {v.x * inv, v.y * inv, v.z * inv};
    }

    class Simulation
    {
    public:
        explicit Simulation(const Config& cfg)
            : cfg_(cfg)
            , rng_(cfg.seed)
        {
            std::uniform_real_distribution<float> position(-cfg.world_half, cfg.world_half);
            std::uniform_real_distribution<float> direction(-1.0F, 1.0F);
            std::uniform_int_distribution<int> hue(0, 255);
            boids_.reserve(cfg.entities);
            for (std::size_t i = 0; i < cfg.entities; ++i) {
                boids_.push_back(
                    {static_cast<std::uint32_t>(i + 1),
                     {position(rng_), position(rng_), position(rng_)},
                     normalize({direction(rng_), direction(rng_), direction(rng_)}),
                     static_cast<std::uint8_t>(hue(rng_))}
                );
            }
        }

        const std::vector<Boid>& boids() const
        {
            return boids_;
        }

        void tick()
        {
            std::normal_distribution<float> turn(0.0F, 0.018F);
            for (Boid& boid : boids_) {
                boid.heading = normalize(
                    {boid.heading.x + turn(rng_),
                     boid.heading.y + turn(rng_),
                     boid.heading.z + turn(rng_)}
                );

                boid.position.x += boid.heading.x * cfg_.speed * cfg_.dt;
                boid.position.y += boid.heading.y * cfg_.speed * cfg_.dt;
                boid.position.z += boid.heading.z * cfg_.speed * cfg_.dt;

                bounce(boid.position.x, boid.heading.x);
                bounce(boid.position.y, boid.heading.y);
                bounce(boid.position.z, boid.heading.z);
            }
        }

    private:
        void bounce(float& p, float& h) const
        {
            if (p > cfg_.world_half) {
                p = cfg_.world_half - (p - cfg_.world_half);
                h = -std::abs(h);
            } else if (p < -cfg_.world_half) {
                p = -cfg_.world_half + (-cfg_.world_half - p);
                h = std::abs(h);
            }
        }

        Config cfg_;
        std::mt19937 rng_;
        std::vector<Boid> boids_;
    };

    template <typename T> void append_le(Bytes& out, T value)
    {
        static_assert(std::is_integral_v<T> || std::is_floating_point_v<T>);
        std::array<std::uint8_t, sizeof(T)> raw{};
        std::memcpy(raw.data(), &value, sizeof(T));
        if constexpr (std::endian::native == std::endian::big) {
            std::reverse(raw.begin(), raw.end());
        }
        out.insert(out.end(), raw.begin(), raw.end());
    }

    void append_uleb128(Bytes& out, std::uint32_t value)
    {
        do {
            std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7Fu);
            value >>= 7u;
            if (value != 0)
                byte |= 0x80u;
            out.push_back(byte);
        } while (value != 0);
    }

    std::uint32_t zigzag(std::int32_t value)
    {
        return (static_cast<std::uint32_t>(value) << 1u) ^ static_cast<std::uint32_t>(value >> 31u);
    }

    std::uint16_t quantize_position(float value, float world_half)
    {
        const float normalized = std::clamp((value + world_half) / (2.0F * world_half), 0.0F, 1.0F);
        return static_cast<std::uint16_t>(std::lround(normalized * 65535.0F));
    }

    std::array<std::int16_t, 2> oct_encode(Vec3 n)
    {
        n = normalize(n);
        const float inv_l1 = 1.0F / (std::abs(n.x) + std::abs(n.y) + std::abs(n.z));
        float x = n.x * inv_l1;
        float y = n.y * inv_l1;
        const float z = n.z * inv_l1;
        if (z < 0.0F) {
            const float old_x = x;
            x = (1.0F - std::abs(y)) * std::copysign(1.0F, old_x);
            y = (1.0F - std::abs(old_x)) * std::copysign(1.0F, y);
        }
        return {
            static_cast<std::int16_t>(std::lround(std::clamp(x, -1.0F, 1.0F) * 32767.0F)),
            static_cast<std::int16_t>(std::lround(std::clamp(y, -1.0F, 1.0F) * 32767.0F))
        };
    }

    std::vector<QuantizedBoid> quantize(const std::vector<Boid>& boids, float world_half)
    {
        std::vector<QuantizedBoid> result;
        result.reserve(boids.size());
        for (const Boid& b : boids) {
            result.push_back(
                {b.id,
                 {quantize_position(b.position.x, world_half),
                  quantize_position(b.position.y, world_half),
                  quantize_position(b.position.z, world_half)},
                 oct_encode(b.heading),
                 b.hue}
            );
        }
        return result;
    }

    EncodedSnapshot encode_full_f32(const std::vector<Boid>& boids)
    {
        EncodedSnapshot out{"full_f32", {}, {}};
        out.bytes.reserve(boids.size() * 29);
        out.record_offsets.reserve(boids.size() + 1);
        out.record_offsets.push_back(0);
        for (const Boid& b : boids) {
            append_le(out.bytes, b.id);
            append_le(out.bytes, b.position.x);
            append_le(out.bytes, b.position.y);
            append_le(out.bytes, b.position.z);
            append_le(out.bytes, b.heading.x);
            append_le(out.bytes, b.heading.y);
            append_le(out.bytes, b.heading.z);
            out.bytes.push_back(b.hue);
            out.record_offsets.push_back(out.bytes.size());
        }
        return out;
    }

    EncodedSnapshot encode_full_quantized(const std::vector<QuantizedBoid>& boids)
    {
        EncodedSnapshot out{"full_quantized", {}, {}};
        out.bytes.reserve(boids.size() * 15);
        out.record_offsets.reserve(boids.size() + 1);
        out.record_offsets.push_back(0);
        for (const QuantizedBoid& b : boids) {
            append_le(out.bytes, b.id);
            for (const auto p : b.position)
                append_le(out.bytes, p);
            for (const auto h : b.heading)
                append_le(out.bytes, h);
            out.bytes.push_back(b.hue);
            out.record_offsets.push_back(out.bytes.size());
        }
        return out;
    }

    EncodedSnapshot encode_delta_quantized(
        const std::vector<QuantizedBoid>& current,
        const std::vector<QuantizedBoid>& baseline
    )
    {
        if (current.size() != baseline.size())
            fail("delta baseline size mismatch");

        EncodedSnapshot out{"delta_quantized", {}, {}};
        out.bytes.reserve(current.size() * 13);
        out.record_offsets.reserve(current.size() + 1);
        out.record_offsets.push_back(0);

        for (std::size_t i = 0; i < current.size(); ++i) {
            const auto& now = current[i];
            const auto& old = baseline[i];
            if (now.id != old.id)
                fail("delta baseline ID mismatch");

            append_le(out.bytes, now.id);
            std::uint8_t mask = 0;
            if (now.position != old.position)
                mask |= 0x01u;
            if (now.heading != old.heading)
                mask |= 0x02u;
            if (now.hue != old.hue)
                mask |= 0x04u;
            out.bytes.push_back(mask);

            if ((mask & 0x01u) != 0) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    const auto delta = static_cast<std::int32_t>(now.position[axis])
                        - static_cast<std::int32_t>(old.position[axis]);
                    append_uleb128(out.bytes, zigzag(delta));
                }
            }
            if ((mask & 0x02u) != 0) {
                for (std::size_t axis = 0; axis < 2; ++axis) {
                    const auto delta = static_cast<std::int32_t>(now.heading[axis])
                        - static_cast<std::int32_t>(old.heading[axis]);
                    append_uleb128(out.bytes, zigzag(delta));
                }
            }
            if ((mask & 0x04u) != 0)
                out.bytes.push_back(now.hue);
            out.record_offsets.push_back(out.bytes.size());
        }
        return out;
    }

    Bytes train_dictionary(std::size_t capacity, const std::vector<Bytes>& samples)
    {
        std::size_t total = 0;
        for (const auto& sample : samples)
            total += sample.size();
        Bytes joined;
        joined.reserve(total);
        std::vector<std::size_t> sizes;
        sizes.reserve(samples.size());
        for (const auto& sample : samples) {
            joined.insert(joined.end(), sample.begin(), sample.end());
            sizes.push_back(sample.size());
        }

        Bytes dictionary(capacity);
        const std::size_t result = ZDICT_trainFromBuffer(
            dictionary.data(),
            dictionary.size(),
            joined.data(),
            sizes.data(),
            static_cast<unsigned>(sizes.size())
        );
        if (ZDICT_isError(result)) {
            fail(std::string("Zstd dictionary training failed: ") + ZDICT_getErrorName(result));
        }
        dictionary.resize(result);
        return dictionary;
    }

    class ZstdCodec
    {
    public:
        ZstdCodec(int level, Bytes dictionary = {})
            : level_(level)
            , dictionary_(std::move(dictionary))
            , cctx_(ZSTD_createCCtx())
            , dctx_(ZSTD_createDCtx())
        {
            if (!cctx_ || !dctx_)
                fail("failed to create Zstd contexts");
            if (!dictionary_.empty()) {
                cdict_ = ZSTD_createCDict(dictionary_.data(), dictionary_.size(), level_);
                ddict_ = ZSTD_createDDict(dictionary_.data(), dictionary_.size());
                if (!cdict_ || !ddict_)
                    fail("failed to create Zstd dictionary contexts");
            }
        }

        ~ZstdCodec()
        {
            ZSTD_freeCDict(cdict_);
            ZSTD_freeDDict(ddict_);
            ZSTD_freeCCtx(cctx_);
            ZSTD_freeDCtx(dctx_);
        }

        ZstdCodec(const ZstdCodec&) = delete;
        ZstdCodec& operator=(const ZstdCodec&) = delete;

        Bytes compress(std::span<const std::uint8_t> input) const
        {
            Bytes output(ZSTD_compressBound(input.size()));
            std::size_t result{};
            if (cdict_) {
                result = ZSTD_compress_usingCDict(
                    cctx_,
                    output.data(),
                    output.size(),
                    input.data(),
                    input.size(),
                    cdict_
                );
            } else {
                result = ZSTD_compressCCtx(
                    cctx_,
                    output.data(),
                    output.size(),
                    input.data(),
                    input.size(),
                    level_
                );
            }
            if (ZSTD_isError(result)) {
                fail(std::string("Zstd compression failed: ") + ZSTD_getErrorName(result));
            }
            output.resize(result);
            return output;
        }

        Bytes decompress(std::span<const std::uint8_t> input, std::size_t expected_size) const
        {
            Bytes output(expected_size);
            std::size_t result{};
            if (ddict_) {
                result = ZSTD_decompress_usingDDict(
                    dctx_,
                    output.data(),
                    output.size(),
                    input.data(),
                    input.size(),
                    ddict_
                );
            } else {
                result = ZSTD_decompressDCtx(
                    dctx_,
                    output.data(),
                    output.size(),
                    input.data(),
                    input.size()
                );
            }
            if (ZSTD_isError(result)) {
                fail(std::string("Zstd decompression failed: ") + ZSTD_getErrorName(result));
            }
            if (result != expected_size)
                fail("Zstd decompressed size mismatch");
            return output;
        }

    private:
        int level_{};
        Bytes dictionary_;
        mutable ZSTD_CCtx* cctx_{};
        mutable ZSTD_DCtx* dctx_{};
        ZSTD_CDict* cdict_{};
        ZSTD_DDict* ddict_{};
    };

    std::vector<PacketSlice> packetize_raw(const EncodedSnapshot& snapshot, std::size_t payload)
    {
        std::vector<PacketSlice> packets;
        const std::size_t records = snapshot.record_offsets.size() - 1;
        std::size_t begin_record = 0;
        while (begin_record < records) {
            std::size_t end_record = begin_record;
            while (end_record < records) {
                const std::size_t bytes = snapshot.record_offsets[end_record + 1]
                    - snapshot.record_offsets[begin_record];
                if (bytes > payload)
                    break;
                ++end_record;
            }
            if (end_record == begin_record)
                fail("one encoded entity exceeds packet payload");
            const auto offset = snapshot.record_offsets[begin_record];
            const auto end = snapshot.record_offsets[end_record];
            packets.push_back({offset, end - offset, end_record - begin_record});
            begin_record = end_record;
        }
        return packets;
    }

    bool compressed_range_fits(
        const EncodedSnapshot& snapshot,
        std::size_t begin_record,
        std::size_t end_record,
        const ZstdCodec& codec,
        std::size_t payload
    )
    {
        const auto begin = snapshot.record_offsets[begin_record];
        const auto end = snapshot.record_offsets[end_record];
        return codec.compress(std::span(snapshot.bytes).subspan(begin, end - begin)).size()
            <= payload;
    }

    std::vector<PacketSlice> packetize_compressed(
        const EncodedSnapshot& snapshot,
        const ZstdCodec& codec,
        std::size_t payload
    )
    {
        std::vector<PacketSlice> packets;
        const std::size_t records = snapshot.record_offsets.size() - 1;
        std::size_t begin = 0;

        while (begin < records) {
            if (!compressed_range_fits(snapshot, begin, begin + 1, codec, payload)) {
                fail("one compressed entity exceeds packet payload");
            }

            std::size_t low = begin + 1; // known fit
            std::size_t high = low;
            std::size_t step = 1;
            while (high < records) {
                const std::size_t candidate = std::min(records, high + step);
                if (!compressed_range_fits(snapshot, begin, candidate, codec, payload)) {
                    high = candidate;
                    break;
                }
                low = candidate;
                high = candidate;
                step *= 2;
            }

            if (low < records && high > low) {
                std::size_t left = low + 1;
                std::size_t right = high;
                while (left <= right) {
                    const std::size_t mid = left + (right - left) / 2;
                    if (compressed_range_fits(snapshot, begin, mid, codec, payload)) {
                        low = mid;
                        left = mid + 1;
                    } else {
                        if (mid == 0)
                            break;
                        right = mid - 1;
                    }
                }
            }

            const std::size_t offset = snapshot.record_offsets[begin];
            const std::size_t end_offset = snapshot.record_offsets[low];
            packets.push_back({offset, end_offset - offset, low - begin});
            begin = low;
        }
        return packets;
    }

    double micros(Clock::duration duration)
    {
        return std::chrono::duration<double, std::micro>(duration).count();
    }

    Measurement measure_none(
        const EncodedSnapshot& snapshot,
        std::size_t entities,
        std::size_t payload,
        double loss_rate
    )
    {
        const auto packets = packetize_raw(snapshot, payload);
        const auto [min_it, max_it]
            = std::minmax_element(packets.begin(), packets.end(), [](const auto& a, const auto& b) {
                  return a.entities < b.entities;
              });
        const double complete = std::pow(1.0 - loss_rate, static_cast<double>(packets.size()));
        return {
            snapshot.name,
            "none",
            "per_packet",
            entities,
            snapshot.bytes.size(),
            snapshot.bytes.size(),
            packets.size(),
            1.0,
            static_cast<double>(entities) / static_cast<double>(packets.size()),
            min_it->entities,
            max_it->entities,
            0.0,
            0.0,
            complete,
            1.0 - loss_rate
        };
    }

    Measurement measure_whole(
        const EncodedSnapshot& snapshot,
        std::size_t entities,
        std::size_t payload,
        double loss_rate,
        std::string compression_name,
        const ZstdCodec& codec,
        std::size_t repeats
    )
    {
        Bytes compressed;
        double compress_time = 0.0;
        for (std::size_t i = 0; i < repeats; ++i) {
            const auto start = Clock::now();
            compressed = codec.compress(snapshot.bytes);
            compress_time += micros(Clock::now() - start);
        }

        double decompress_time = 0.0;
        for (std::size_t i = 0; i < repeats; ++i) {
            const auto start = Clock::now();
            const auto decoded = codec.decompress(compressed, snapshot.bytes.size());
            decompress_time += micros(Clock::now() - start);
            if (decoded != snapshot.bytes)
                fail("whole-frame roundtrip mismatch");
        }

        const std::size_t packets = (compressed.size() + payload - 1) / payload;
        const double complete = std::pow(1.0 - loss_rate, static_cast<double>(packets));
        return {
            snapshot.name,
            std::move(compression_name),
            "whole_snapshot",
            entities,
            snapshot.bytes.size(),
            compressed.size(),
            packets,
            static_cast<double>(compressed.size()) / static_cast<double>(snapshot.bytes.size()),
            static_cast<double>(entities) / static_cast<double>(packets),
            0,
            0,
            compress_time / static_cast<double>(repeats),
            decompress_time / static_cast<double>(repeats),
            complete,
            complete
        };
    }

    Measurement measure_per_packet(
        const EncodedSnapshot& snapshot,
        std::size_t entities,
        std::size_t payload,
        double loss_rate,
        std::string compression_name,
        const ZstdCodec& codec,
        std::size_t repeats
    )
    {
        const auto packets = packetize_compressed(snapshot, codec, payload);
        std::vector<Bytes> compressed_packets;
        compressed_packets.reserve(packets.size());

        double compress_time = 0.0;
        for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
            compressed_packets.clear();
            const auto start = Clock::now();
            for (const PacketSlice& packet : packets) {
                compressed_packets.push_back(
                    codec.compress(std::span(snapshot.bytes).subspan(packet.offset, packet.size))
                );
            }
            compress_time += micros(Clock::now() - start);
        }

        double decompress_time = 0.0;
        for (std::size_t repeat = 0; repeat < repeats; ++repeat) {
            const auto start = Clock::now();
            for (std::size_t i = 0; i < packets.size(); ++i) {
                const auto decoded = codec.decompress(compressed_packets[i], packets[i].size);
                const auto expected
                    = std::span(snapshot.bytes).subspan(packets[i].offset, packets[i].size);
                if (!std::equal(decoded.begin(), decoded.end(), expected.begin(), expected.end())) {
                    fail("per-packet roundtrip mismatch");
                }
            }
            decompress_time += micros(Clock::now() - start);
        }

        std::size_t payload_bytes = 0;
        for (const auto& packet : compressed_packets)
            payload_bytes += packet.size();
        const auto [min_it, max_it]
            = std::minmax_element(packets.begin(), packets.end(), [](const auto& a, const auto& b) {
                  return a.entities < b.entities;
              });
        const double complete = std::pow(1.0 - loss_rate, static_cast<double>(packets.size()));

        return {
            snapshot.name,
            std::move(compression_name),
            "per_packet",
            entities,
            snapshot.bytes.size(),
            payload_bytes,
            packets.size(),
            static_cast<double>(payload_bytes) / static_cast<double>(snapshot.bytes.size()),
            static_cast<double>(entities) / static_cast<double>(packets.size()),
            min_it->entities,
            max_it->entities,
            compress_time / static_cast<double>(repeats),
            decompress_time / static_cast<double>(repeats),
            complete,
            1.0 - loss_rate
        };
    }

    struct Accumulator
    {
        std::string encoding;
        std::string compression;
        std::string scope;
        std::size_t samples{};
        double entities{};
        double source_bytes{};
        double payload_bytes{};
        double packets{};
        double ratio{};
        double entities_per_packet{};
        double min_entities_per_packet{std::numeric_limits<double>::infinity()};
        double max_entities_per_packet{};
        double compress_us{};
        double decompress_us{};
        double complete_probability{};
        double expected_usable_fraction{};

        void add(const Measurement& m)
        {
            if (samples == 0) {
                encoding = m.encoding;
                compression = m.compression;
                scope = m.scope;
            }
            ++samples;
            entities += static_cast<double>(m.entities);
            source_bytes += static_cast<double>(m.source_bytes);
            payload_bytes += static_cast<double>(m.payload_bytes);
            packets += static_cast<double>(m.packets);
            ratio += m.ratio;
            entities_per_packet += m.entities_per_packet;
            if (m.min_entities_per_packet != 0) {
                min_entities_per_packet = std::min(
                    min_entities_per_packet,
                    static_cast<double>(m.min_entities_per_packet)
                );
            }
            max_entities_per_packet
                = std::max(max_entities_per_packet, static_cast<double>(m.max_entities_per_packet));
            compress_us += m.compress_us;
            decompress_us += m.decompress_us;
            complete_probability += m.complete_probability;
            expected_usable_fraction += m.expected_usable_fraction;
        }

        Measurement mean() const
        {
            const double n = static_cast<double>(samples);
            return {
                encoding,
                compression,
                scope,
                static_cast<std::size_t>(std::llround(entities / n)),
                static_cast<std::size_t>(std::llround(source_bytes / n)),
                static_cast<std::size_t>(std::llround(payload_bytes / n)),
                static_cast<std::size_t>(std::llround(packets / n)),
                ratio / n,
                entities_per_packet / n,
                std::isinf(min_entities_per_packet)
                    ? 0
                    : static_cast<std::size_t>(min_entities_per_packet),
                static_cast<std::size_t>(max_entities_per_packet),
                compress_us / n,
                decompress_us / n,
                complete_probability / n,
                expected_usable_fraction / n
            };
        }
    };

    std::vector<Bytes> make_training_samples(const Config& cfg, const std::string& encoding_name)
    {
        Config training_cfg = cfg;
        training_cfg.seed
            ^= 0xA5A5'9E37u; // keep training trajectories separate from benchmark data
        Simulation sim(training_cfg);
        auto previous = quantize(sim.boids(), cfg.world_half);
        std::vector<Bytes> samples;
        const std::size_t payload = cfg.mtu - cfg.headroom;

        for (std::size_t tick = 0; tick < cfg.train_ticks; ++tick) {
            sim.tick();
            const auto current = quantize(sim.boids(), cfg.world_half);
            EncodedSnapshot snapshot;
            if (encoding_name == "full_f32")
                snapshot = encode_full_f32(sim.boids());
            else if (encoding_name == "full_quantized")
                snapshot = encode_full_quantized(current);
            else
                snapshot = encode_delta_quantized(current, previous);

            const auto raw_packets = packetize_raw(snapshot, payload);
            for (const PacketSlice& packet : raw_packets) {
                samples.emplace_back(
                    snapshot.bytes.begin() + static_cast<std::ptrdiff_t>(packet.offset),
                    snapshot.bytes.begin()
                        + static_cast<std::ptrdiff_t>(packet.offset + packet.size)
                );
            }
            previous = current;
        }
        return samples;
    }

    void print_table(const std::vector<Measurement>& rows, const Config& cfg)
    {
        std::cout << "\nMean results over " << cfg.test_ticks << " snapshots\n";
        std::cout << std::left << std::setw(18) << "encoding" << std::setw(12) << "compression"
                  << std::setw(16) << "scope" << std::right << std::setw(10) << "src B"
                  << std::setw(10) << "out B" << std::setw(9) << "ratio" << std::setw(8) << "pkts"
                  << std::setw(11) << "boids/pkt" << std::setw(12) << "comp us" << std::setw(12)
                  << "decomp us" << std::setw(12) << "complete%" << std::setw(12) << "usable%"
                  << '\n';
        std::cout << std::string(152, '-') << '\n';

        for (const auto& row : rows) {
            std::cout << std::left << std::setw(18) << row.encoding << std::setw(12)
                      << row.compression << std::setw(16) << row.scope << std::right
                      << std::setw(10) << row.source_bytes << std::setw(10) << row.payload_bytes
                      << std::setw(9) << std::fixed << std::setprecision(3) << row.ratio
                      << std::setw(8) << row.packets << std::setw(11) << std::setprecision(1)
                      << row.entities_per_packet << std::setw(12) << std::setprecision(1)
                      << row.compress_us << std::setw(12) << row.decompress_us << std::setw(12)
                      << std::setprecision(2) << row.complete_probability * 100.0 << std::setw(12)
                      << row.expected_usable_fraction * 100.0 << '\n';
        }

        std::cout
            << "\nFor per-packet rows, usable% is the expected independently usable entity fraction.\n"
            << "For whole-snapshot rows, one missing fragment makes that compressed frame incomplete,\n"
            << "so usable% equals the probability that every fragment arrives.\n";
    }

    void write_csv(const std::filesystem::path& path, const std::vector<Measurement>& rows)
    {
        std::ofstream out(path);
        if (!out)
            fail("failed to open CSV output");
        out << "encoding,compression,scope,entities,source_bytes,payload_bytes,ratio,packets,"
               "entities_per_packet,min_entities_per_packet,max_entities_per_packet,compress_us,"
               "decompress_us,complete_probability,expected_usable_fraction\n";
        out << std::setprecision(10);
        for (const auto& row : rows) {
            out << row.encoding << ',' << row.compression << ',' << row.scope << ',' << row.entities
                << ',' << row.source_bytes << ',' << row.payload_bytes << ',' << row.ratio << ','
                << row.packets << ',' << row.entities_per_packet << ','
                << row.min_entities_per_packet << ',' << row.max_entities_per_packet << ','
                << row.compress_us << ',' << row.decompress_us << ',' << row.complete_probability
                << ',' << row.expected_usable_fraction << '\n';
        }
    }

} // namespace

int main(int argc, char** argv)
{
    try {
        const Config cfg = parse_args(argc, argv);
        const std::size_t payload = cfg.mtu - cfg.headroom;

        std::cout << "Network snapshot compression probe\n"
                  << "  entities:       " << cfg.entities << '\n'
                  << "  MTU:            " << cfg.mtu << " bytes\n"
                  << "  headroom:       " << cfg.headroom << " bytes\n"
                  << "  max payload:    " << payload << " bytes\n"
                  << "  update interval:" << ' ' << cfg.dt << " s\n"
                  << "  Zstd level:     " << cfg.zstd_level << '\n'
                  << "  packet loss:    " << cfg.loss_rate * 100.0 << "%\n"
                  << "  packed full-f32 wire record: 29 bytes (ID + 6 floats + hue)\n"
                  << "  quantized full wire record:  15 bytes (ID + 3xu16 + oct2xi16 + hue)\n";

        const std::array<std::string, 3> encoding_names
            = {"full_f32", "full_quantized", "delta_quantized"};

        std::vector<Bytes> dictionaries;
        dictionaries.reserve(encoding_names.size());
        for (const auto& encoding : encoding_names) {
            std::cout << "Training " << cfg.dict_size << "-byte dictionary for " << encoding
                      << "...\n";
            const auto samples = make_training_samples(cfg, encoding);
            dictionaries.push_back(train_dictionary(cfg.dict_size, samples));
            std::cout << "  trained dictionary: " << dictionaries.back().size() << " bytes from "
                      << samples.size() << " packet-like samples\n";
        }

        std::array<Accumulator, 15> accumulators{};
        Simulation sim(cfg);
        auto previous = quantize(sim.boids(), cfg.world_half);

        for (std::size_t tick = 0; tick < cfg.test_ticks; ++tick) {
            sim.tick();
            const auto current = quantize(sim.boids(), cfg.world_half);
            const std::array<EncodedSnapshot, 3> snapshots
                = {encode_full_f32(sim.boids()),
                   encode_full_quantized(current),
                   encode_delta_quantized(current, previous)};

            for (std::size_t e = 0; e < snapshots.size(); ++e) {
                ZstdCodec plain(cfg.zstd_level);
                ZstdCodec dict(cfg.zstd_level, dictionaries[e]);
                const auto& snapshot = snapshots[e];
                const std::size_t base = e * 5;
                accumulators[base + 0].add(
                    measure_none(snapshot, cfg.entities, payload, cfg.loss_rate)
                );
                accumulators[base + 1].add(measure_whole(
                    snapshot,
                    cfg.entities,
                    payload,
                    cfg.loss_rate,
                    "zstd",
                    plain,
                    cfg.repeats
                ));
                accumulators[base + 2].add(measure_per_packet(
                    snapshot,
                    cfg.entities,
                    payload,
                    cfg.loss_rate,
                    "zstd",
                    plain,
                    cfg.repeats
                ));
                accumulators[base + 3].add(measure_whole(
                    snapshot,
                    cfg.entities,
                    payload,
                    cfg.loss_rate,
                    "zstd_dict",
                    dict,
                    cfg.repeats
                ));
                accumulators[base + 4].add(measure_per_packet(
                    snapshot,
                    cfg.entities,
                    payload,
                    cfg.loss_rate,
                    "zstd_dict",
                    dict,
                    cfg.repeats
                ));
            }
            previous = current;
        }

        std::vector<Measurement> rows;
        rows.reserve(accumulators.size());
        for (const auto& accumulator : accumulators)
            rows.push_back(accumulator.mean());
        print_table(rows, cfg);
        write_csv(cfg.csv, rows);
        std::cout << "\nWrote " << cfg.csv << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
