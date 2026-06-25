#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <array>

#include "core/net/net_buffer.hpp"
#include "core/net/net_types.hpp"
#include "core/net/net_peer_state.hpp"

namespace simnet::core::net::internal {
    // ============================================
    //  NetworkSnapshot
    // ============================================
    struct NetworkSnapshot {
        uint64_t tick = 0;

        std::vector<uint64_t> entity_ids;

        std::vector<float> pos_x;
        std::vector<float> pos_y;
        std::vector<float> pos_z;

        std::vector<float> heading_x;
        std::vector<float> heading_y;
        std::vector<float> heading_z;

        std::vector<uint8_t> hues;

        void clear() noexcept
        {
            entity_ids.clear();
            pos_x.clear();
            pos_y.clear();
            pos_z.clear();
            heading_x.clear();
            heading_y.clear();
            heading_z.clear();
            hues.clear();
        }
    };

    // ============================================
    //  PipelineContext
    // ============================================
    struct PipelineContext {
        uint32_t peer_id;
        uint32_t sequence;
        uint64_t current_tick;

        const NetworkSnapshot &snapshot;
        std::shared_ptr<const NetworkSnapshot> baseline; // from SnapshotHistory

        PeerState &peer_state;
        std::vector<uint32_t> &active_indices; // mutated by filters
    };

    // ============================================
    //  Pipeline stage interfaces
    // ============================================

    class IEntityFilter {
    public:
        virtual ~IEntityFilter() = default;

        virtual void filter(PipelineContext &ctx) = 0;

        [[nodiscard]] virtual const char *name() const = 0;
    };

    class ISerializer {
    public:
        virtual ~ISerializer() = default;

        virtual void serialize(const PipelineContext &ctx, NetBuffer &out_buffer) = 0;

        [[nodiscard]] virtual const char *name() const = 0;
    };

    class IEncoder {
    public:
        virtual ~IEncoder() = default;

        virtual void encode(const NetBuffer &in_buffer, NetBuffer &out_buffer) = 0;

        [[nodiscard]] virtual const char *name() const = 0;
    };
}
