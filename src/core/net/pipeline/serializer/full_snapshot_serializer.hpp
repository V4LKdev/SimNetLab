#pragma once

#include "../net_pipeline_interfaces.hpp"
#include "core/net/pipeline/serializer/net_serialization_helpers.hpp"
#include "core/net/net_buffer.hpp"
#include "core/net/net_types.hpp"
#include "core/config/sim_config.hpp"

namespace simnet::core::net::internal {
    class FullSnapshotSerializer final : public ISerializer {
    public:
        explicit FullSnapshotSerializer(const config::SimConfig &cfg) : cfg_(cfg)
        {
        }

        void serialize(const PipelineContext &ctx, NetBuffer &out) override
        {
            const auto &snap = ctx.snapshot;
            const uint32_t entity_count = static_cast<uint32_t>(ctx.active_indices.size());

            // --- Packet header ---
            out.write(static_cast<uint8_t>(MessageType::Snapshot));
            out.write(static_cast<uint32_t>(ctx.current_tick));
            out.write(ctx.sequence);
            out.write(static_cast<uint32_t>(0)); // baseline_tick = 0
            out.write(static_cast<uint16_t>(SnapshotFlags::FullSnapshot));
            out.write(entity_count);

            // --- Entity payload ---
            for (uint32_t idx: ctx.active_indices) {
                write_entity(snap, idx, out);
            }

            TELEM_COUNTER_INC("net.full_snapshot_serialized", 1);
        }

        [[nodiscard]] const char *name() const override { return "FullSnapshot"; }

    private:
        const config::SimConfig &cfg_;

        void write_entity(const NetworkSnapshot &snap, uint32_t i, NetBuffer &out) const
        {
            // -- network id --
            out.write(static_cast<uint32_t>(snap.entity_ids[i]));

            // -- position --
            write_float3(snap.pos_x[i], snap.pos_y[i], snap.pos_z[i], out, cfg_.enable_float_quantization);


            // -- heading --
            if (cfg_.enable_octahedral_heading) {
                write_octahedral_heading(out, snap.heading_x[i], snap.heading_y[i], snap.heading_z[i]);
            } else {
                write_float3(snap.heading_x[i], snap.heading_y[i], snap.heading_z[i], out,
                             cfg_.enable_float_quantization);
            }

            // -- hue --
            out.write(snap.hues[i]);

            // future: cfg_.enable_bit_packing could compress all fields into a bitstream
            // currently leaves fields byte‑aligned
        }

        void write_float3(float x, float y, float z, NetBuffer &out, bool quantized) const
        {
            if (quantized) {
                write_quantized_float(out, x, -cfg_.world_half, cfg_.world_half);
                write_quantized_float(out, y, -cfg_.world_half, cfg_.world_half);
                write_quantized_float(out, z, -cfg_.world_half, cfg_.world_half);
            } else {
                out.write(x);
                out.write(y);
                out.write(z);
            }
        }
    };
} // namespace
