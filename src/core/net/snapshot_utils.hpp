#pragma once

#include <cstdint>
#include <cstddef>
#include <enet/enet.h>

#include "protocol.hpp"
#include "serialization.hpp"
#include "wire_types.hpp"

/**
 *  Functions to build and parse snapshot-related ENet packets.
 *
 *  This file handles FULL STATE snapshots only. Other modules transform it before handing it to the serializer.
 */
namespace simnet::net {
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

    /// Builder
    [[nodiscard]]
    inline ENetPacket *create_snapshot(const ReplicationSnapshot &snapshot,
                                       enet_uint32 flags = ENET_PACKET_FLAG_UNSEQUENCED)
    {
        const auto entity_count = snapshot.entities.size();
        const size_t total_size =
                PACKET_HEADER_SIZE +
                SNAPSHOT_HEADER_SIZE +
                entity_count * ENTITY_PAYLOAD_SIZE;

        ENetPacket *pkt = enet_packet_create(nullptr, total_size, flags);
        if (!pkt) { return nullptr; }

        uint8_t *data = pkt->data;
        size_t offset = 0;

        // 1. Message type
        write_u8(data + offset, static_cast<uint8_t>(MessageType::Snapshot));
        offset += PACKET_HEADER_SIZE;

        // 2. Snapshot header
        write_u32(data + offset, snapshot.tick);
        offset += 4;
        write_u32(data + offset, snapshot.sequence);
        offset += 4;
        write_u32(data + offset, 0); /* no baseline tick for full snap */
        offset += 4;

        const uint16_t flags_field = static_cast<uint16_t>(SnapshotFlags::FullSnapshot);
        write_u16(data + offset, flags_field);
        offset += 2;

        const auto count_u32 = static_cast<uint32_t>(entity_count);
        write_u32(data + offset, count_u32);
        offset += 4;

        // 3. Entities
        for (const auto &entity: snapshot.entities) {
            write_u32(data + offset, entity.network_id);
            offset += 4;

            write_float(data + offset, entity.position.x());
            offset += 4;
            write_float(data + offset, entity.position.y());
            offset += 4;
            write_float(data + offset, entity.position.z());
            offset += 4;

            write_float(data + offset, entity.heading.x());
            offset += 4;
            write_float(data + offset, entity.heading.y());
            offset += 4;
            write_float(data + offset, entity.heading.z());
            offset += 4;

            write_u8(data + offset, entity.hue);
        }

        return pkt;
    }

    /// Parser
    inline bool parse_snapshot(const ENetPacket *pkt, ReplicationSnapshot &out)
    {
        if (!pkt || pkt->dataLength < PACKET_HEADER_SIZE + SNAPSHOT_HEADER_SIZE) { return false; }

        const uint8_t *data = pkt->data;
        size_t offset = 0;

        // 1. Message type (extra verification)
        const auto msg_type = static_cast<MessageType>(read_u8(data + offset));
        if (msg_type != MessageType::Snapshot) { return false; }
        offset += PACKET_HEADER_SIZE;

        // 2. Snapshot header
        out.tick = read_u32(data + offset);
        offset += 4;
        out.sequence = read_u32(data + offset);
        offset += 4;
        // baseline tick
        read_u32(data + offset);
        offset += 4;
        // flags
        read_u16(data + offset);
        offset += 2;
        const uint32_t entity_count = read_u32(data + offset);
        offset += 4;

        // Check remaining payload matches the expected size
        if (pkt->dataLength < offset + entity_count * ENTITY_PAYLOAD_SIZE) { return false; }

        // 3. Entities
        out.entities.clear();
        out.entities.reserve(entity_count);

        for (uint16_t i = 0; i < entity_count; ++i) {
            ReplicatedEntity entity;

            entity.network_id = read_u32(data + offset);
            offset += 4;

            const float px = read_float(data + offset);
            offset += 4;
            const float py = read_float(data + offset);
            offset += 4;
            const float pz = read_float(data + offset);
            offset += 4;

            entity.position = Vec3(px, py, pz);

            const float hx = read_float(data + offset);
            offset += 4;
            const float hy = read_float(data + offset);
            offset += 4;
            const float hz = read_float(data + offset);
            offset += 4;

            entity.heading = Vec3(hx, hy, hz);

            entity.hue = read_u8(data + offset);
            offset += 1;

            out.entities.push_back(entity);
        }
        return true;
    }

    /// Returns the exact number of bytes needed for the snapshot's wire format.
    [[nodiscard]]
    inline size_t get_snapshot_size(const ReplicationSnapshot &snapshot) noexcept
    {
        return PACKET_HEADER_SIZE +
               SNAPSHOT_HEADER_SIZE +
               snapshot.entities.size() * ENTITY_PAYLOAD_SIZE;
    }

    /// Writes the snapshot into a pre-allocated buffer of at least get_snapshot_size() bytes.
    inline void serialize_snapshot(const ReplicationSnapshot &snapshot, uint8_t *dest) noexcept
    {
        size_t offset = 0;
        write_u8(dest + offset, static_cast<uint8_t>(MessageType::Snapshot));
        offset += PACKET_HEADER_SIZE;

        write_u32(dest + offset, snapshot.tick);
        offset += 4;
        write_u32(dest + offset, snapshot.sequence);
        offset += 4;
        write_u32(dest + offset, 0); // no baseline
        offset += 4;

        const uint16_t flags = static_cast<uint16_t>(SnapshotFlags::FullSnapshot);
        write_u16(dest + offset, flags);
        offset += 2;

        const uint32_t cnt = static_cast<uint32_t>(snapshot.entities.size());
        write_u32(dest + offset, cnt);
        offset += 4;

        for (const auto &e: snapshot.entities) {
            write_u32(dest + offset, e.network_id);
            offset += 4;
            write_float(dest + offset, e.position.x());
            offset += 4;
            write_float(dest + offset, e.position.y());
            offset += 4;
            write_float(dest + offset, e.position.z());
            offset += 4;
            write_float(dest + offset, e.heading.x());
            offset += 4;
            write_float(dest + offset, e.heading.y());
            offset += 4;
            write_float(dest + offset, e.heading.z());
            offset += 4;
            write_u8(dest + offset, e.hue);
            offset += 1;
        }
    }
}
