#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "core/net/net_buffer.hpp"

namespace simnet::core::net::internal {
    // --- Quantization ---
    /// Writes a float value quantized to `bits` bits over [min, max].
    inline void write_quantized_float(NetBuffer &out, float value, float min, float max, uint8_t bits = 12)
    {
        // TODO: implement mapping and write as unsigned integer
        (void) out;
        (void) value;
        (void) min;
        (void) max;
        (void) bits;
    }

    inline float read_quantized_float(NetBuffer &in, float min, float max, uint8_t bits = 12)
    {
        (void) in;
        (void) min;
        (void) max;
        (void) bits;
        return 0.0f; // TODO
    }

    // --- Octahedral Heading ---
    /// Encodes a normalized direction into 2 bytes (u8, v8).
    inline void write_octahedral_heading(NetBuffer &out, float x, float y, float z)
    {
        // TODO: implement projection and fold
        (void) out;
        (void) x;
        (void) y;
        (void) z;
    }

    inline void read_octahedral_heading(NetBuffer &in, float &x, float &y, float &z)
    {
        (void) in;
        x = 0;
        y = 0;
        z = 1; // TODO
    }

    // --- Variable-bit encoding (future delta use) ---
    inline void write_varint(NetBuffer &out, uint32_t value)
    {
        (void) out;
        (void) value; // TODO
    }

    inline uint32_t read_varint(NetBuffer &in)
    {
        (void) in;
        return 0; // TODO
    }
} // namespace
