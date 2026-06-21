#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>

#include "net_buffer.hpp"
#include "net_types.hpp"
#include "core/math/vec3.hpp"

namespace simnet::core::net::internal {
    // --- DTOs ---

    /// One replicated entity's state for a snapshot
    struct ReplicatedEntity {
        uint32_t network_id = 0;
        math::Vec3 position = math::Vec3::zero();
        math::Vec3 heading = math::Vec3::zero();
        uint8_t hue = 0;
    };

    /**
    * @brief Flat wire‑format container for a full world‑state snapshot.
    *
    * Holds a tick number, a sequence counter, and a list of replicated entities.
    * Serializes directly into a NetBuffer; used by the network pipeline and
    * the game's translation layer.
    */
    struct ReplicationSnapshot {
        uint32_t tick = 0;
        uint32_t sequence = 0;
        std::vector<ReplicatedEntity> entities;

        /// Serialises the full snapshot into a NetBuffer (raw wire format).
        void write(NetBuffer &buffer) const;

        /// Deserializes a full snapshot from a NetBuffer.
        static ReplicationSnapshot read(NetBuffer &buffer);
    };

    namespace detail {
        /// Per-entity layout constants
        constexpr size_t ENTITY_NETWORK_ID_SIZE = sizeof(uint32_t);
        constexpr size_t ENTITY_POSITION_SIZE = sizeof(float) * 3;
        constexpr size_t ENTITY_HEADING_SIZE = sizeof(float) * 3;
        constexpr size_t ENTITY_HUE_SIZE = sizeof(uint8_t);

        constexpr size_t ENTITY_PAYLOAD_SIZE =
                ENTITY_NETWORK_ID_SIZE +
                ENTITY_POSITION_SIZE +
                ENTITY_HEADING_SIZE +
                ENTITY_HUE_SIZE;

        static_assert(ENTITY_PAYLOAD_SIZE == 29);
    }

    // --- Inline Implementations ---
    inline void ReplicationSnapshot::write(NetBuffer &buffer) const
    {
        // 1. Message Header
        buffer.write(static_cast<uint8_t>(MessageType::Snapshot));

        // 2. Snapshot header
        buffer.write(tick);
        buffer.write(sequence);
        buffer.write(static_cast<uint32_t>(0)); // Baseline tick by default 0 without deltas
        buffer.write(static_cast<uint16_t>(SnapshotFlags::FullSnapshot));
        buffer.write(static_cast<uint32_t>(entities.size()));

        // 3. Entities
        for (const auto &e: entities) {
            buffer.write(e.network_id);
            buffer.write(e.position.x());
            buffer.write(e.position.y());
            buffer.write(e.position.z());
            buffer.write(e.heading.x());
            buffer.write(e.heading.y());
            buffer.write(e.heading.z());
            buffer.write(e.hue);
        }

        // Telemetry
        TELEM_COUNTER_INC("net.snapshot_serialized", 1);
        TELEM_TRACY_PLOT("net.snapshot_entity_count_write", static_cast<int64_t>(entities.size()));
    }

    inline ReplicationSnapshot ReplicationSnapshot::read(NetBuffer &buffer)
    {
        // Verify message type
        if (static_cast<MessageType>(buffer.read<uint8_t>()) != MessageType::Snapshot) {
            throw std::runtime_error("ReplicationSnapshot read unexpected message");
        }

        ReplicationSnapshot snapshot;
        snapshot.tick = buffer.read<uint32_t>();
        snapshot.sequence = buffer.read<uint32_t>();

        // Discard baseline_tick and flags for now
        (void) buffer.read<uint32_t>();
        (void) buffer.read<uint16_t>();

        const uint32_t entity_count = buffer.read<uint32_t>();
        snapshot.entities.reserve(entity_count);

        for (uint32_t i = 0; i < entity_count; i++) {
            ReplicatedEntity e;
            e.network_id = buffer.read<uint32_t>();

            const float px = buffer.read<float>();
            const float py = buffer.read<float>();
            const float pz = buffer.read<float>();
            e.position = math::Vec3(px, py, pz);

            const float hx = buffer.read<float>();
            const float hy = buffer.read<float>();
            const float hz = buffer.read<float>();
            e.heading = math::Vec3(hx, hy, hz);

            e.hue = buffer.read<uint8_t>();
            snapshot.entities.push_back(std::move(e));
        }

        // Telemetry
        TELEM_COUNTER_INC("net.snapshot_deserialized", 1);
        TELEM_TRACY_PLOT("net.snapshot_entity_count_read", static_cast<int64_t>(snapshot.entities.size()));

        return snapshot;
    }
}
