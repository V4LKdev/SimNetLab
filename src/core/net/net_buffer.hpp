#pragma once
#include <cstdint>
#include <vector>
#include <bit>
#include <stdexcept>

#include "telemetry/telemetry.hpp"

/**
 * Dynamic, endian‑aware byte stream for network serialization.
 *
 * All multi‑byte values are written/read in network byte order (big‑endian).
 * The buffer grows automatically on write; reading advances an internal offset.
 */
namespace simnet::core::net::internal {
    static constexpr size_t MAX_BUFFER_SIZE = 64 * 1024; // 64 KB

    class NetBuffer {
    public:
        NetBuffer() = default;

        ~NetBuffer() = default;

        NetBuffer(const NetBuffer &) = default;

        NetBuffer &operator=(const NetBuffer &) = default;

        NetBuffer(NetBuffer &&) = default;

        NetBuffer &operator=(NetBuffer &&) = default;

        // --- Write API ---
        /**
        * @brief Write a value in network byte order.
        * @tparam T Primitive type (uint8_t, uint16_t, uint32_t, uint64_t, int8_t, int16_t, int32_t, int64_t, float, bool).
        */
        template<typename T>
        void write(const T &value);

        void write_raw(const uint8_t *data, size_t length)
        {
            if (buffer_.size() + length > MAX_BUFFER_SIZE) {
                throw std::length_error("NetBuffer exceeded max size");
            }
            buffer_.insert(buffer_.end(), data, data + length);

            TELEM_COUNTER_INC("net.bytes_recv_raw", static_cast<int64_t>(length));
        }

        // --- Read API ---
        /**
        * @brief Read a value previously written with write().
        * @tparam T Primitive type.
        * @return The read value.
        * @throws std::out_of_range if less than sizeof(T) bytes remain.
        */
        template<typename T>
        T read();

        // --- Convenience ---
        /** @brief Current size of the underlying data. */
        [[nodiscard]]
        size_t size() const { return buffer_.size(); }

        [[nodiscard]]
        const uint8_t *data() const { return buffer_.data(); }

        void clear()
        {
            buffer_.clear();
            read_pos_ = 0;
        }

        /** @brief Reset read position to the beginning (but keep written data). */
        void reset_data() { read_pos_ = 0; }

        /** @brief Number of unread bytes remaining. */
        [[nodiscard]]
        size_t remaining() const { return buffer_.size() - read_pos_; }

    private:
        std::vector<uint8_t> buffer_;
        size_t read_pos_ = 0;

        // --- Write helpers (Network byte order BE) ---
        static void write_U8(std::vector<uint8_t> &buffer, uint8_t value)
        {
            buffer.push_back(value);
        }

        static void write_U16(std::vector<uint8_t> &buffer, uint16_t value)
        {
            buffer.push_back(static_cast<uint8_t>(value >> 8) & 0xFF);
            buffer.push_back(static_cast<uint8_t>(value & 0xFF));
        }

        static void write_U32(std::vector<uint8_t> &buffer, uint32_t value)
        {
            buffer.push_back(static_cast<uint8_t>(value >> 24) & 0xFF);
            buffer.push_back(static_cast<uint8_t>(value >> 16) & 0xFF);
            buffer.push_back(static_cast<uint8_t>(value >> 8) & 0xFF);
            buffer.push_back(static_cast<uint8_t>(value & 0xFF));
        }

        static void write_U64(std::vector<uint8_t> &buffer, uint64_t value)
        {
            buffer.push_back(static_cast<uint8_t>(value >> 56) & 0xFF);
            buffer.push_back(static_cast<uint8_t>(value >> 48) & 0xFF);
            buffer.push_back(static_cast<uint8_t>(value >> 40) & 0xFF);
            buffer.push_back(static_cast<uint8_t>(value >> 32) & 0xFF);
            buffer.push_back(static_cast<uint8_t>(value >> 24) & 0xFF);
            buffer.push_back(static_cast<uint8_t>(value >> 16) & 0xFF);
            buffer.push_back(static_cast<uint8_t>(value >> 8) & 0xFF);
            buffer.push_back(static_cast<uint8_t>(value & 0xFF));
        }

        // --- Read helpers (BE) ---
        static uint8_t read_U8(const uint8_t *source)
        {
            return *source;
        }

        static uint16_t read_U16(const uint8_t *source)
        {
            return (static_cast<uint16_t>(source[0]) << 8) |
                   static_cast<uint16_t>(source[1]);
        }

        static uint32_t read_U32(const uint8_t *source)
        {
            return (static_cast<uint32_t>(source[0]) << 24) |
                   static_cast<uint32_t>(source[1]) << 16 |
                   static_cast<uint32_t>(source[2]) << 8 |
                   static_cast<uint32_t>(source[3]);
        }

        static uint64_t read_U64(const uint8_t *source)
        {
            return (static_cast<uint64_t>(source[0]) << 56) |
                   static_cast<uint64_t>(source[1]) << 48 |
                   static_cast<uint64_t>(source[2]) << 40 |
                   static_cast<uint64_t>(source[3]) << 32 |
                   static_cast<uint64_t>(source[4]) << 24 |
                   static_cast<uint64_t>(source[5]) << 16 |
                   static_cast<uint64_t>(source[6]) << 8 |
                   static_cast<uint64_t>(source[7]);
        }

        // --- Internal read with bounds check ---
        template<size_t N>
        const uint8_t *read_bytes()
        {
            if (read_pos_ + N > buffer_.size()) {
                throw std::out_of_range("NetBuffer read past end");
            }
            const uint8_t *ptr = buffer_.data() + read_pos_;
            read_pos_ += N;
            return ptr;
        }
    };

    // --- Write specializations ---
    template<>
    inline void NetBuffer::write<uint8_t>(const uint8_t &value)
    {
        write_U8(buffer_, value);
    }

    template<>
    inline void NetBuffer::write<uint16_t>(const uint16_t &value)
    {
        write_U16(buffer_, value);
    }

    template<>
    inline void NetBuffer::write<uint32_t>(const uint32_t &value)
    {
        write_U32(buffer_, value);
    }

    template<>
    inline void NetBuffer::write<uint64_t>(const uint64_t &value)
    {
        write_U64(buffer_, value);
    }

    template<>
    inline void NetBuffer::write<int8_t>(const int8_t &value)
    {
        write_U8(buffer_, static_cast<uint8_t>(value));
    }

    template<>
    inline void NetBuffer::write<int16_t>(const int16_t &value)
    {
        write_U16(buffer_, static_cast<uint16_t>(value));
    }

    template<>
    inline void NetBuffer::write<int32_t>(const int32_t &value)
    {
        write_U32(buffer_, static_cast<uint32_t>(value));
    }

    template<>
    inline void NetBuffer::write<int64_t>(const int64_t &value)
    {
        write_U64(buffer_, static_cast<uint64_t>(value));
    }

    template<>
    inline void NetBuffer::write<float>(const float &value)
    {
        // Cpp20 std::bit_cast or memcpy alternatively
        uint32_t bits = std::bit_cast<uint32_t>(value);
        write_U32(buffer_, bits);
    }

    template<>
    inline void NetBuffer::write<bool>(const bool &value)
    {
        write_U8(buffer_, value ? 1 : 0);
    }

    // --- Read specializations ---
    template<>
    inline uint8_t NetBuffer::read<uint8_t>()
    {
        const auto *p = read_bytes<1>();
        return read_U8(p);
    }

    template<>
    inline uint16_t NetBuffer::read<uint16_t>()
    {
        const auto *p = read_bytes<2>();
        return read_U16(p);
    }

    template<>
    inline uint32_t NetBuffer::read<uint32_t>()
    {
        const auto *p = read_bytes<4>();
        return read_U32(p);
    }

    template<>
    inline uint64_t NetBuffer::read<uint64_t>()
    {
        const auto *p = read_bytes<8>();
        return read_U64(p);
    }

    template<>
    inline int8_t NetBuffer::read<int8_t>()
    {
        return static_cast<int8_t>(read<uint8_t>());
    }

    template<>
    inline int16_t NetBuffer::read<int16_t>()
    {
        return static_cast<int16_t>(read<uint16_t>());
    }

    template<>
    inline int32_t NetBuffer::read<int32_t>()
    {
        return static_cast<int32_t>(read<uint32_t>());
    }

    template<>
    inline int64_t NetBuffer::read<int64_t>()
    {
        return static_cast<int64_t>(read<uint64_t>());
    }

    template<>
    inline float NetBuffer::read<float>()
    {
        uint32_t bits = read<uint32_t>();
        float value = std::bit_cast<float>(bits);
        return value;
    }

    template<>
    inline bool NetBuffer::read<bool>()
    {
        return read<uint8_t>() != 0;
    }
}
