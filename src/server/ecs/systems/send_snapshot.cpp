#include <flecs.h>

#include "network_server.hpp"
#include "../../../core/net/snapshot_utils.hpp"
#include "telemetry.hpp"
#include "../../../core/net/wire_types.hpp"
#include "../../../game/shared/components.hpp"

namespace simnet::server::ecs {
    void send_snapshot_system(flecs::iter &it)
    {
        const auto srv = it.world().get<simnet::ecs::ServerContext>();

        net::ReplicationSnapshot snapshot;
        static uint32_t s_tick = 0;
        snapshot.tick = s_tick++;
        static uint32_t s_sequence = 0;
        snapshot.sequence = s_sequence++;

        while (it.next()) {
            auto pos = it.field<const simnet::ecs::Position>(0);
            auto hdg = it.field<const simnet::ecs::Heading>(1);
            auto hue = it.field<const simnet::ecs::Hue>(2);
            auto nid = it.field<const simnet::ecs::NetworkId>(3);

            for (int i = 0; i < it.count(); ++i) {
                net::ReplicatedEntity re;
                re.network_id = nid[i].value;
                re.position = pos[i].value;
                re.heading = hdg[i].value;
                re.hue = hue[i].value;
                snapshot.entities.push_back(re);
            }
        }

        // Serialise once
        auto len = simnet::net::get_snapshot_size(snapshot);
        std::vector<uint8_t> buffer(len);
        net::serialize_snapshot(snapshot, buffer.data());

        // Send to all connected peers
        srv.server->for_each_connected_peer([&buffer](ENetPeer *peer) {
            ENetPacket *pkt = enet_packet_create(
                buffer.data(), buffer.size(), ENET_PACKET_FLAG_UNSEQUENCED);
            if (pkt) enet_peer_send(peer, 0, pkt);
        });

        TELEM_LOG_INFO("Snapshot send: tick {}; sequence {}; entities {};", snapshot.tick, snapshot.sequence,
                       snapshot.entities.size());
    }
}
