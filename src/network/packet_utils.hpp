#pragma once
#include <enet/enet.h>

#include "protocol.hpp"
#include "serialization.hpp"

namespace simnet::net {
    // --- Builders ---
    /// Creates a Hello packet
    inline ENetPacket *create_hello(ProtocolVersion version)
    {
        constexpr std::size_t total = PACKET_HEADER_SIZE + HELLO_SIZE;

        auto *pkt = enet_packet_create(nullptr, total, ENET_PACKET_FLAG_RELIABLE);
        if (!pkt) { return nullptr; }

        std::uint8_t *data = pkt->data;

        write_u8(data, static_cast<std::uint8_t>(MessageType::Hello));
        write_u16(data + PACKET_HEADER_SIZE, version);

        return pkt;
    }

    inline ENetPacket *create_welcome()
    {
        auto *pkt = enet_packet_create(nullptr, PACKET_HEADER_SIZE, ENET_PACKET_FLAG_RELIABLE);
        if (!pkt) { return nullptr; }

        write_u8(pkt->data, static_cast<std::uint8_t>(MessageType::Welcome));
        return pkt;
    }

    inline ENetPacket *create_reject(RejectReason reason)
    {
        constexpr std::size_t total = PACKET_HEADER_SIZE + REJECT_SIZE;
        auto *pkt = enet_packet_create(nullptr, total, ENET_PACKET_FLAG_RELIABLE);
        if (!pkt) { return nullptr; }

        std::uint8_t *data = pkt->data;

        write_u8(data, static_cast<std::uint8_t>(MessageType::Reject));
        write_u8(data + PACKET_HEADER_SIZE, static_cast<std::uint8_t>(reason));
        return pkt;
    }

    inline ENetPacket *create_ping()
    {
        auto *pkt = enet_packet_create(nullptr, PACKET_HEADER_SIZE, ENET_PACKET_FLAG_UNSEQUENCED);

        if (!pkt) { return nullptr; }

        write_u8(pkt->data, static_cast<std::uint8_t>(MessageType::Ping));
        return pkt;
    }

    inline ENetPacket *create_pong()
    {
        auto *pkt = enet_packet_create(nullptr, PACKET_HEADER_SIZE, ENET_PACKET_FLAG_UNSEQUENCED);
        if (!pkt) { return nullptr; }

        write_u8(pkt->data, static_cast<std::uint8_t>(MessageType::Pong));
        return pkt;
    }

    // --- Parsing Helpers ---
    inline MessageType read_message_type(const ENetPacket *pkt)
    {
        if (!pkt || pkt->dataLength < PACKET_HEADER_SIZE) { return MessageType::Invalid; }
        return static_cast<MessageType>(pkt->data[0]);
    }

    inline ProtocolVersion read_hello_version(const ENetPacket *pkt)
    {
        if (!pkt || pkt->dataLength < PACKET_HEADER_SIZE + HELLO_SIZE) { return 0; }

        return read_u16(pkt->data + PACKET_HEADER_SIZE);
    }

    inline RejectReason read_reject_reason(const ENetPacket *pkt)
    {
        if (!pkt || pkt->dataLength < PACKET_HEADER_SIZE + REJECT_SIZE) { return RejectReason::Other; }

        return static_cast<RejectReason>(read_u8(pkt->data + PACKET_HEADER_SIZE));
    }
}
