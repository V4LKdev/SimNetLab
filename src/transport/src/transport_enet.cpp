module;

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <enet/enet.h>
#include <span>
#include <string>
#include <utility>
#include <vector>

module simnet.transport:backend;

import :client;
import :server;
import :types;

namespace simnet
{
    namespace transport_backend
    {
        constexpr std::uint32_t session_magic = 0x534E5453U;
        constexpr std::uint16_t session_version = 2;
        constexpr std::uint32_t session_header_bytes = 11;
        constexpr std::uint8_t channel_count = 3;
        constexpr PeerId server_peer_id = 1;

        enum class SessionMessageKind : std::uint8_t
        {
            ClientHello = 1,
            ServerAccept = 2,
            ServerReject = 3,
            SnapshotAck = 4
        };

        struct SessionMessage
        {
            SessionMessageKind kind {};
            SessionIdentity identity {};
            DisconnectCode reject_code {};
            SnapshotAck snapshot_ack {};
        };

        [[nodiscard]] TransportResult ok() noexcept
        {
            return {};
        }

        [[nodiscard]] TransportResult fail(
            TransportErrorCode code,
            std::string message,
            std::uint32_t native_code = 0
        )
        {
            return {
                .ok = false,
                .error = {
                    .code = code,
                    .message = std::move(message),
                    .native_code = native_code,
                },
            };
        }

        [[nodiscard]] DisconnectCode identity_mismatch(
            SessionIdentity const& actual,
            SessionIdentity const& expected
        ) noexcept
        {
            if (actual.application_protocol_version != expected.application_protocol_version) {
                return DisconnectCode::ProtocolMismatch;
            }
            if (actual.compatibility_fingerprint != expected.compatibility_fingerprint) {
                return DisconnectCode::IncompatibleConfig;
            }
            if (actual.pipeline_decode_signature != expected.pipeline_decode_signature) {
                return DisconnectCode::IncompatibleWireProfile;
            }
            if (actual.capabilities != expected.capabilities) {
                return DisconnectCode::UnsupportedCapability;
            }
            return DisconnectCode::None;
        }

        [[nodiscard]] std::uint8_t lane_index(Lane lane) noexcept
        {
            return static_cast<std::uint8_t>(lane);
        }

        [[nodiscard]] bool valid_lane(Lane lane) noexcept
        {
            return lane_index(lane) < channel_count;
        }

        [[nodiscard]] bool valid_delivery(Delivery delivery) noexcept
        {
            switch (delivery) {
            case Delivery::ReliableSequenced:
            case Delivery::UnreliableSequenced:
            case Delivery::UnreliableUnsequenced:
            case Delivery::UnreliableFragmented:
                return true;
            }
            return false;
        }

        [[nodiscard]] ENetPacketFlag delivery_flags(Delivery delivery) noexcept
        {
            switch (delivery) {
            case Delivery::ReliableSequenced:
                return ENET_PACKET_FLAG_RELIABLE;
            case Delivery::UnreliableSequenced:
                return static_cast<ENetPacketFlag>(0);
            case Delivery::UnreliableUnsequenced:
                return ENET_PACKET_FLAG_UNSEQUENCED;
            case Delivery::UnreliableFragmented:
                return ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
            }
            return static_cast<ENetPacketFlag>(0);
        }

        [[nodiscard]] Delivery packet_delivery(ENetPacket const& packet) noexcept
        {
            if ((packet.flags & ENET_PACKET_FLAG_RELIABLE) != 0U) {
                return Delivery::ReliableSequenced;
            }
            if ((packet.flags & ENET_PACKET_FLAG_UNSEQUENCED) != 0U) {
                return Delivery::UnreliableUnsequenced;
            }
            if ((packet.flags & ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT) != 0U) {
                return Delivery::UnreliableFragmented;
            }
            return Delivery::UnreliableSequenced;
        }

        void write_u8(std::vector<Byte>& bytes, std::uint8_t value)
        {
            bytes.push_back(static_cast<Byte>(value));
        }

        void write_u16(std::vector<Byte>& bytes, std::uint16_t value)
        {
            bytes.push_back(static_cast<Byte>((value >> 8U) & 0xFFU));
            bytes.push_back(static_cast<Byte>(value & 0xFFU));
        }

        void write_u32(std::vector<Byte>& bytes, std::uint32_t value)
        {
            bytes.push_back(static_cast<Byte>((value >> 24U) & 0xFFU));
            bytes.push_back(static_cast<Byte>((value >> 16U) & 0xFFU));
            bytes.push_back(static_cast<Byte>((value >> 8U) & 0xFFU));
            bytes.push_back(static_cast<Byte>(value & 0xFFU));
        }

        void write_u64(std::vector<Byte>& bytes, std::uint64_t value)
        {
            write_u32(bytes, static_cast<std::uint32_t>((value >> 32U) & 0xFFFFFFFFULL));
            write_u32(bytes, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
        }

        [[nodiscard]] bool read_u8(Byte const* data, std::size_t size, std::size_t& offset, std::uint8_t& value)
        {
            if (offset + 1U > size) {
                return false;
            }
            value = static_cast<std::uint8_t>(data[offset]);
            ++offset;
            return true;
        }

        [[nodiscard]] bool read_u16(Byte const* data, std::size_t size, std::size_t& offset, std::uint16_t& value)
        {
            std::uint8_t high {};
            std::uint8_t low {};
            if (!read_u8(data, size, offset, high) || !read_u8(data, size, offset, low)) {
                return false;
            }
            value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8U) | low);
            return true;
        }

        [[nodiscard]] bool read_u32(Byte const* data, std::size_t size, std::size_t& offset, std::uint32_t& value)
        {
            std::uint8_t a {};
            std::uint8_t b {};
            std::uint8_t c {};
            std::uint8_t d {};
            if (!read_u8(data, size, offset, a) || !read_u8(data, size, offset, b)
                || !read_u8(data, size, offset, c) || !read_u8(data, size, offset, d)) {
                return false;
            }
            value = (static_cast<std::uint32_t>(a) << 24U)
                | (static_cast<std::uint32_t>(b) << 16U)
                | (static_cast<std::uint32_t>(c) << 8U)
                | static_cast<std::uint32_t>(d);
            return true;
        }

        [[nodiscard]] bool read_u64(Byte const* data, std::size_t size, std::size_t& offset, std::uint64_t& value)
        {
            std::uint32_t high {};
            std::uint32_t low {};
            if (!read_u32(data, size, offset, high) || !read_u32(data, size, offset, low)) {
                return false;
            }
            value = (static_cast<std::uint64_t>(high) << 32U) | low;
            return true;
        }

        [[nodiscard]] std::vector<Byte> encode_session_message(SessionMessage const& message)
        {
            auto payload = std::vector<Byte> {};
            if (message.kind == SessionMessageKind::ClientHello) {
                write_u32(payload, message.identity.application_protocol_version);
                write_u64(payload, message.identity.compatibility_fingerprint);
                write_u64(payload, message.identity.pipeline_decode_signature);
                write_u32(payload, message.identity.capabilities);
            } else if (message.kind == SessionMessageKind::ServerReject) {
                write_u16(payload, static_cast<std::uint16_t>(message.reject_code));
            } else if (message.kind == SessionMessageKind::SnapshotAck) {
                write_u32(payload, message.snapshot_ack.newest_received_snapshot);
                write_u32(payload, message.snapshot_ack.received_mask);
                write_u32(payload, message.snapshot_ack.newest_applied_snapshot);
            }

            auto bytes = std::vector<Byte> {};
            bytes.reserve(session_header_bytes + payload.size());
            write_u32(bytes, session_magic);
            write_u16(bytes, session_version);
            write_u8(bytes, static_cast<std::uint8_t>(message.kind));
            write_u32(bytes, static_cast<std::uint32_t>(payload.size()));
            bytes.insert(bytes.end(), payload.begin(), payload.end());
            return bytes;
        }

        [[nodiscard]] bool decode_session_message(
            Byte const* data,
            std::size_t size,
            SessionMessage& message
        )
        {
            std::size_t offset {};
            std::uint32_t magic {};
            std::uint16_t version {};
            std::uint8_t kind {};
            std::uint32_t payload_size {};
            if (!read_u32(data, size, offset, magic) || !read_u16(data, size, offset, version)
                || !read_u8(data, size, offset, kind) || !read_u32(data, size, offset, payload_size)) {
                return false;
            }
            if (magic != session_magic || version != session_version || offset + payload_size != size) {
                return false;
            }

            message.kind = static_cast<SessionMessageKind>(kind);
            if (message.kind == SessionMessageKind::ClientHello) {
                return payload_size == 24U
                    && read_u32(data, size, offset, message.identity.application_protocol_version)
                    && read_u64(data, size, offset, message.identity.compatibility_fingerprint)
                    && read_u64(data, size, offset, message.identity.pipeline_decode_signature)
                    && read_u32(data, size, offset, message.identity.capabilities);
            }
            if (message.kind == SessionMessageKind::ServerAccept) {
                return payload_size == 0U;
            }
            if (message.kind == SessionMessageKind::ServerReject) {
                auto code = std::uint16_t {};
                if (payload_size != 2U || !read_u16(data, size, offset, code)) {
                    return false;
                }
                message.reject_code = static_cast<DisconnectCode>(code);
                return true;
            }
            if (message.kind == SessionMessageKind::SnapshotAck) {
                return payload_size == 12U
                    && read_u32(data, size, offset, message.snapshot_ack.newest_received_snapshot)
                    && read_u32(data, size, offset, message.snapshot_ack.received_mask)
                    && read_u32(data, size, offset, message.snapshot_ack.newest_applied_snapshot);
            }
            return false;
        }

        [[nodiscard]] PeerId event_peer_id(ENetPeer const* peer) noexcept
        {
            return static_cast<PeerId>(reinterpret_cast<std::uintptr_t>(peer->data));
        }

        void set_peer_id(ENetPeer* peer, PeerId id) noexcept
        {
            peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
        }

        struct EnetRuntime
        {
            EnetRuntime()
            {
                initialized = enet_initialize() == 0;
            }

            ~EnetRuntime()
            {
                if (initialized) {
                    enet_deinitialize();
                }
            }

            bool initialized {};
        };

        [[nodiscard]] EnetRuntime& enet_runtime()
        {
            static auto runtime = EnetRuntime {};
            return runtime;
        }

        [[nodiscard]] TransportResult require_enet()
        {
            return enet_runtime().initialized ? ok() : fail(TransportErrorCode::BackendError, "ENet initialization failed");
        }

        [[nodiscard]] bool set_enet_address_host(ENetAddress& address, std::string const& host)
        {
            return enet_address_set_host_ip(&address, host.c_str()) == 0
                || enet_address_set_host(&address, host.c_str()) == 0;
        }

        void add_send_stats(TransportStats& stats, Lane lane, std::size_t bytes)
        {
            auto const index = lane_index(lane);
            ++stats.packets_sent;
            stats.bytes_sent += bytes;
            ++stats.lane_packets_sent[index];
            stats.lane_bytes_sent[index] += bytes;
        }

        void add_receive_stats(TransportStats& stats, Lane lane, std::size_t bytes)
        {
            auto const index = lane_index(lane);
            ++stats.packets_received;
            stats.bytes_received += bytes;
            ++stats.lane_packets_received[index];
            stats.lane_bytes_received[index] += bytes;
        }

        [[nodiscard]] TransportResult send_to_peer(
            ENetPeer* peer,
            TransportStats& stats,
            TransportLimits const& limits,
            Lane lane,
            Delivery delivery,
            std::span<Byte const> payload
        )
        {
            if (!valid_lane(lane)) {
                ++stats.send_failures;
                return fail(TransportErrorCode::InvalidLane, "invalid transport lane");
            }
            if (!valid_delivery(delivery)) {
                ++stats.send_failures;
                return fail(TransportErrorCode::InvalidDelivery, "invalid transport delivery mode");
            }
            if (limits.size_policy == SendSizePolicy::EnforceLimit && payload.size() > limits.max_payload_bytes) {
                ++stats.send_failures;
                ++stats.oversize_drops;
                return fail(TransportErrorCode::PayloadTooLarge, "transport payload exceeds configured limit");
            }

            auto* packet = enet_packet_create(
                payload.data(),
                payload.size(),
                delivery_flags(delivery)
            );
            if (packet == nullptr) {
                ++stats.send_failures;
                return fail(TransportErrorCode::BackendError, "ENet packet allocation failed");
            }
            if (enet_peer_send(peer, lane_index(lane), packet) != 0) {
                enet_packet_destroy(packet);
                ++stats.send_failures;
                return fail(TransportErrorCode::BackendError, "ENet send failed");
            }

            add_send_stats(stats, lane, payload.size());
            return ok();
        }

        [[nodiscard]] PeerStats make_peer_stats(ENetPeer const* peer) noexcept
        {
            if (peer == nullptr) {
                return {};
            }
            return {
                .rtt_ms = static_cast<double>(peer->roundTripTime),
                .packet_loss_ratio = static_cast<double>(peer->packetLoss)
                    / static_cast<double>(ENET_PEER_PACKET_LOSS_SCALE),
                .reliable_bytes_in_flight = peer->reliableDataInTransit,
                .waiting_bytes = peer->outgoingDataTotal,
                .mtu = peer->mtu,
            };
        }

        struct PeerSlot
        {
            PeerId id {};
            ENetPeer* peer {};
            bool session_ready {};
        };
    }

    using namespace transport_backend;

    struct TransportServer::Impl
    {
        ENetHost* host {};
        TransportServerSettings settings {};
        TransportStats counters {};
        std::vector<PeerSlot> peers;
        PeerId next_peer_id { 1 };

        [[nodiscard]] PeerSlot* find(PeerId id) noexcept
        {
            auto found = std::ranges::find_if(peers, [id](PeerSlot const& slot) { return slot.id == id; });
            return found == peers.end() ? nullptr : &*found;
        }

        [[nodiscard]] TransportResult send_session(ENetPeer* peer, SessionMessage const& message)
        {
            auto bytes = encode_session_message(message);
            return send_to_peer(
                peer,
                counters,
                { .max_payload_bytes = settings.limits.max_payload_bytes,
                  .size_policy = SendSizePolicy::AllowBackendFragmentation },
                Lane::Control,
                Delivery::ReliableSequenced,
                bytes
            );
        }

        void handle_client_hello(PeerSlot& slot, SessionMessage const& message, std::vector<TransportEvent>& events)
        {
            if (slot.session_ready) {
                events.push_back(TransportErrorEvent { .message = "duplicate ClientHello after session ready" });
                disconnect(slot.id, DisconnectCode::ProtocolMismatch);
                return;
            }

            auto const mismatch = identity_mismatch(message.identity, settings.expected_identity);
            if (mismatch != DisconnectCode::None) {
                static_cast<void>(send_session(slot.peer, {
                    .kind = SessionMessageKind::ServerReject,
                    .reject_code = mismatch,
                }));
                events.push_back(TransportErrorEvent { .message = "client session identity mismatch" });
                disconnect(slot.id, mismatch);
                return;
            }

            auto accepted = send_session(slot.peer, { .kind = SessionMessageKind::ServerAccept });
            if (!accepted.ok) {
                events.push_back(TransportErrorEvent { .message = accepted.error.message });
                disconnect(slot.id, DisconnectCode::TransportError);
                return;
            }
            slot.session_ready = true;
            events.push_back(PeerSessionReady { .peer = slot.id });
        }

        void disconnect(PeerId id, DisconnectCode code) noexcept
        {
            if (auto* slot = find(id); slot != nullptr && slot->peer != nullptr) {
                enet_peer_disconnect_later(slot->peer, static_cast<std::uint32_t>(code));
            }
        }
    };

    TransportServer::TransportServer() : impl_(new Impl {}) {}

    TransportServer::~TransportServer()
    {
        stop();
        delete impl_;
    }

    TransportServer::TransportServer(TransportServer&& other) noexcept : impl_(std::exchange(other.impl_, nullptr)) {}

    TransportServer& TransportServer::operator=(TransportServer&& other) noexcept
    {
        if (this != &other) {
            stop();
            delete impl_;
            impl_ = std::exchange(other.impl_, nullptr);
        }
        return *this;
    }

    TransportResult TransportServer::start(TransportServerSettings const& settings)
    {
        if (impl_ == nullptr || impl_->host != nullptr) {
            return fail(TransportErrorCode::AlreadyStarted, "transport server is already started");
        }
        if (settings.backend != TransportBackend::ENet) {
            return fail(TransportErrorCode::UnsupportedBackend, "requested transport server backend is not implemented");
        }
        if (auto ready = require_enet(); !ready.ok) {
            return ready;
        }
        if (settings.port == 0 || settings.max_peers == 0) {
            return fail(TransportErrorCode::InvalidAddress, "server transport port and max_peers must be non-zero");
        }

        auto address = ENetAddress { .host = ENET_HOST_ANY, .port = settings.port };
        if (!settings.bind_address.empty() && !set_enet_address_host(address, settings.bind_address)) {
            return fail(TransportErrorCode::InvalidAddress, "invalid server bind address");
        }

        impl_->host = enet_host_create(&address, settings.max_peers, channel_count, 0, 0);
        if (impl_->host == nullptr) {
            return fail(TransportErrorCode::BackendError, "failed to create ENet server host");
        }
        impl_->settings = settings;
        impl_->counters = {};
        impl_->peers.clear();
        impl_->next_peer_id = 1;
        return ok();
    }

    void TransportServer::stop() noexcept
    {
        if (impl_ == nullptr) {
            return;
        }
        if (impl_->host != nullptr) {
            enet_host_destroy(impl_->host);
            impl_->host = nullptr;
        }
        impl_->peers.clear();
    }

    bool TransportServer::is_running() const noexcept
    {
        return impl_ != nullptr && impl_->host != nullptr;
    }

    TransportResult TransportServer::poll(std::vector<TransportEvent>& out_events, std::uint32_t timeout_ms)
    {
        if (impl_ == nullptr || impl_->host == nullptr) {
            return fail(TransportErrorCode::NotStarted, "transport server is not started");
        }

        auto event = ENetEvent {};
        auto wait_ms = timeout_ms;
        for (;;) {
            auto const service_result = enet_host_service(impl_->host, &event, wait_ms);
            if (service_result < 0) {
                return fail(TransportErrorCode::BackendError, "ENet server service failed");
            }
            if (service_result == 0) {
                break;
            }
            wait_ms = 0;
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                auto const id = impl_->next_peer_id++;
                set_peer_id(event.peer, id);
                impl_->peers.push_back({ .id = id, .peer = event.peer });
                out_events.push_back(PeerConnected { .peer = id });
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                auto const peer = event_peer_id(event.peer);
                auto const lane = static_cast<Lane>(event.channelID);
                if (!valid_lane(lane)) {
                    enet_packet_destroy(event.packet);
                    out_events.push_back(TransportErrorEvent { .message = "received packet on invalid ENet channel" });
                    continue;
                }
                add_receive_stats(impl_->counters, lane, event.packet->dataLength);

                auto* slot = impl_->find(peer);
                if (slot != nullptr && lane == Lane::Control) {
                    auto message = SessionMessage {};
                    auto const* data = reinterpret_cast<Byte const*>(event.packet->data);
                    if (!decode_session_message(data, event.packet->dataLength, message)
                        || message.kind != SessionMessageKind::ClientHello) {
                        out_events.push_back(TransportErrorEvent { .message = "invalid client session message" });
                        impl_->disconnect(peer, DisconnectCode::ProtocolMismatch);
                    } else {
                        impl_->handle_client_hello(*slot, message, out_events);
                    }
                } else if (slot != nullptr && lane == Lane::Input) {
                    auto message = SessionMessage {};
                    auto const* data = reinterpret_cast<Byte const*>(event.packet->data);
                    if (!slot->session_ready
                        || !decode_session_message(data, event.packet->dataLength, message)
                        || message.kind != SessionMessageKind::SnapshotAck) {
                        out_events.push_back(TransportErrorEvent { .message = "invalid snapshot ACK message" });
                        impl_->disconnect(peer, DisconnectCode::ProtocolMismatch);
                    } else {
                        out_events.push_back(SnapshotAckReceived {
                            .peer = peer,
                            .ack = message.snapshot_ack,
                        });
                    }
                } else if (slot != nullptr && slot->session_ready && valid_lane(lane)) {
                    auto payload = std::vector<Byte>(event.packet->dataLength);
                    std::memcpy(payload.data(), event.packet->data, event.packet->dataLength);
                    out_events.push_back(ReceivedPacket {
                        .peer = peer,
                        .lane = lane,
                        .delivery = packet_delivery(*event.packet),
                        .payload = std::move(payload),
                    });
                }
                enet_packet_destroy(event.packet);
            } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                auto const id = event_peer_id(event.peer);
                ++impl_->counters.disconnects;
                out_events.push_back(PeerDisconnected {
                    .peer = id,
                    .code = static_cast<DisconnectCode>(event.data),
                    .native_reason = event.data,
                });
                impl_->peers.erase(
                    std::remove_if(impl_->peers.begin(), impl_->peers.end(), [id](PeerSlot const& slot) {
                        return slot.id == id;
                    }),
                    impl_->peers.end()
                );
                event.peer->data = nullptr;
            }
        }
        return ok();
    }

    TransportResult TransportServer::send(SendPacket const& packet)
    {
        if (impl_ == nullptr || impl_->host == nullptr) {
            return fail(TransportErrorCode::NotStarted, "transport server is not started");
        }
        auto* slot = impl_->find(packet.peer);
        if (slot == nullptr) {
            return fail(TransportErrorCode::PeerNotFound, "transport peer was not found");
        }
        if (!slot->session_ready) {
            return fail(TransportErrorCode::PeerNotReady, "transport peer session is not ready");
        }
        return send_to_peer(slot->peer, impl_->counters, impl_->settings.limits, packet.lane, packet.delivery, packet.payload);
    }

    void TransportServer::disconnect(PeerId peer, DisconnectCode code) noexcept
    {
        if (impl_ != nullptr) {
            impl_->disconnect(peer, code);
        }
    }

    TransportStats TransportServer::stats() const
    {
        return impl_ == nullptr ? TransportStats {} : impl_->counters;
    }

    PeerStats TransportServer::peer_stats(PeerId peer) const
    {
        if (impl_ == nullptr) {
            return {};
        }
        auto* slot = impl_->find(peer);
        return slot == nullptr ? PeerStats {} : make_peer_stats(slot->peer);
    }

    struct TransportClient::Impl
    {
        ENetHost* host {};
        ENetPeer* server {};
        TransportClientSettings settings {};
        TransportStats counters {};
        bool transport_connected {};
        bool session_ready {};

        [[nodiscard]] TransportResult send_session(
            SessionMessage const& message,
            Lane lane = Lane::Control
        )
        {
            auto bytes = encode_session_message(message);
            return send_to_peer(
                server,
                counters,
                { .max_payload_bytes = settings.limits.max_payload_bytes,
                  .size_policy = SendSizePolicy::AllowBackendFragmentation },
                lane,
                Delivery::ReliableSequenced,
                bytes
            );
        }
    };

    TransportClient::TransportClient() : impl_(new Impl {}) {}

    TransportClient::~TransportClient()
    {
        disconnect(DisconnectCode::None);
        delete impl_;
    }

    TransportClient::TransportClient(TransportClient&& other) noexcept : impl_(std::exchange(other.impl_, nullptr)) {}

    TransportClient& TransportClient::operator=(TransportClient&& other) noexcept
    {
        if (this != &other) {
            disconnect(DisconnectCode::None);
            delete impl_;
            impl_ = std::exchange(other.impl_, nullptr);
        }
        return *this;
    }

    TransportResult TransportClient::connect(TransportClientSettings const& settings)
    {
        if (impl_ == nullptr || impl_->host != nullptr) {
            return fail(TransportErrorCode::AlreadyStarted, "transport client is already connected or connecting");
        }
        if (settings.backend != TransportBackend::ENet) {
            return fail(TransportErrorCode::UnsupportedBackend, "requested transport client backend is not implemented");
        }
        if (auto ready = require_enet(); !ready.ok) {
            return ready;
        }
        if (settings.server_port == 0 || settings.server_address.empty()) {
            return fail(TransportErrorCode::InvalidAddress, "client server address and port are required");
        }

        impl_->host = enet_host_create(nullptr, 1, channel_count, 0, 0);
        if (impl_->host == nullptr) {
            return fail(TransportErrorCode::BackendError, "failed to create ENet client host");
        }

        auto address = ENetAddress { .host = 0, .port = settings.server_port };
        if (!set_enet_address_host(address, settings.server_address)) {
            disconnect(DisconnectCode::None);
            return fail(TransportErrorCode::InvalidAddress, "invalid server address");
        }

        impl_->server = enet_host_connect(impl_->host, &address, channel_count, 0);
        if (impl_->server == nullptr) {
            disconnect(DisconnectCode::None);
            return fail(TransportErrorCode::ConnectionFailed, "failed to create ENet server peer");
        }
        set_peer_id(impl_->server, server_peer_id);
        impl_->settings = settings;
        impl_->counters = {};
        impl_->transport_connected = false;
        impl_->session_ready = false;
        return ok();
    }

    void TransportClient::disconnect(DisconnectCode code) noexcept
    {
        if (impl_ != nullptr && impl_->server != nullptr) {
            auto* disconnecting_peer = impl_->server;
            enet_peer_disconnect(disconnecting_peer, static_cast<std::uint32_t>(code));
            enet_host_flush(impl_->host);

            auto event = ENetEvent {};
            auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            while (std::chrono::steady_clock::now() < deadline) {
                auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()
                );
                auto const timeout = static_cast<std::uint32_t>(std::max<std::int64_t>(remaining.count(), 0));
                auto const service_result = enet_host_service(impl_->host, &event, timeout);
                if (service_result <= 0) {
                    break;
                }
                if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                    enet_packet_destroy(event.packet);
                }
                if (event.type == ENET_EVENT_TYPE_DISCONNECT && event.peer == disconnecting_peer) {
                    break;
                }
            }
            impl_->server = nullptr;
        }
        if (impl_ != nullptr && impl_->host != nullptr) {
            enet_host_destroy(impl_->host);
            impl_->host = nullptr;
        }
        if (impl_ != nullptr) {
            impl_->transport_connected = false;
            impl_->session_ready = false;
        }
    }

    bool TransportClient::is_connected() const noexcept
    {
        return impl_ != nullptr && impl_->transport_connected;
    }

    bool TransportClient::is_session_ready() const noexcept
    {
        return impl_ != nullptr && impl_->session_ready;
    }

    TransportResult TransportClient::poll(std::vector<TransportEvent>& out_events, std::uint32_t timeout_ms)
    {
        if (impl_ == nullptr || impl_->host == nullptr) {
            return fail(TransportErrorCode::NotStarted, "transport client is not connected");
        }

        auto event = ENetEvent {};
        auto wait_ms = timeout_ms;
        for (;;) {
            auto const service_result = enet_host_service(impl_->host, &event, wait_ms);
            if (service_result < 0) {
                return fail(TransportErrorCode::BackendError, "ENet client service failed");
            }
            if (service_result == 0) {
                break;
            }
            wait_ms = 0;
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                impl_->transport_connected = true;
                out_events.push_back(PeerConnected { .peer = server_peer_id });
                auto sent = impl_->send_session({
                    .kind = SessionMessageKind::ClientHello,
                    .identity = impl_->settings.identity,
                });
                if (!sent.ok) {
                    out_events.push_back(TransportErrorEvent { .message = sent.error.message });
                }
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                auto const lane = static_cast<Lane>(event.channelID);
                if (!valid_lane(lane)) {
                    enet_packet_destroy(event.packet);
                    out_events.push_back(TransportErrorEvent { .message = "received packet on invalid ENet channel" });
                    continue;
                }
                add_receive_stats(impl_->counters, lane, event.packet->dataLength);

                if (lane == Lane::Control) {
                    auto message = SessionMessage {};
                    auto const* data = reinterpret_cast<Byte const*>(event.packet->data);
                    if (!decode_session_message(data, event.packet->dataLength, message)) {
                        out_events.push_back(TransportErrorEvent { .message = "invalid server session message" });
                    } else if (message.kind == SessionMessageKind::ServerAccept) {
                        if (!impl_->session_ready) {
                            impl_->session_ready = true;
                            out_events.push_back(PeerSessionReady { .peer = server_peer_id });
                        }
                    } else if (message.kind == SessionMessageKind::ServerReject) {
                        out_events.push_back(TransportErrorEvent { .message = "server rejected transport session" });
                        out_events.push_back(PeerDisconnected {
                            .peer = server_peer_id,
                            .code = message.reject_code,
                            .native_reason = static_cast<std::uint32_t>(message.reject_code),
                        });
                        ++impl_->counters.disconnects;
                        enet_peer_disconnect_now(event.peer, static_cast<std::uint32_t>(message.reject_code));
                        impl_->server = nullptr;
                        impl_->transport_connected = false;
                        impl_->session_ready = false;
                    }
                } else if (impl_->session_ready && valid_lane(lane)) {
                    auto payload = std::vector<Byte>(event.packet->dataLength);
                    std::memcpy(payload.data(), event.packet->data, event.packet->dataLength);
                    out_events.push_back(ReceivedPacket {
                        .peer = server_peer_id,
                        .lane = lane,
                        .delivery = packet_delivery(*event.packet),
                        .payload = std::move(payload),
                    });
                }
                enet_packet_destroy(event.packet);
            } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                ++impl_->counters.disconnects;
                out_events.push_back(PeerDisconnected {
                    .peer = server_peer_id,
                    .code = static_cast<DisconnectCode>(event.data),
                    .native_reason = event.data,
                });
                impl_->server = nullptr;
                impl_->transport_connected = false;
                impl_->session_ready = false;
            }
        }
        return ok();
    }

    TransportResult TransportClient::send(
        Lane lane,
        Delivery delivery,
        std::span<Byte const> payload
    )
    {
        if (impl_ == nullptr || impl_->host == nullptr || impl_->server == nullptr) {
            return fail(TransportErrorCode::NotStarted, "transport client is not connected");
        }
        if (!impl_->session_ready) {
            return fail(TransportErrorCode::PeerNotReady, "transport server session is not ready");
        }
        return send_to_peer(impl_->server, impl_->counters, impl_->settings.limits, lane, delivery, payload);
    }

    TransportResult TransportClient::send_snapshot_ack(SnapshotAck const& ack)
    {
        if (impl_ == nullptr || impl_->host == nullptr || impl_->server == nullptr) {
            return fail(TransportErrorCode::NotStarted, "transport client is not connected");
        }
        if (!impl_->session_ready) {
            return fail(TransportErrorCode::PeerNotReady, "transport server session is not ready");
        }
        return impl_->send_session({
            .kind = SessionMessageKind::SnapshotAck,
            .snapshot_ack = ack,
        }, Lane::Input);
    }

    TransportStats TransportClient::stats() const
    {
        return impl_ == nullptr ? TransportStats {} : impl_->counters;
    }

    PeerStats TransportClient::server_stats() const
    {
        return impl_ == nullptr ? PeerStats {} : make_peer_stats(impl_->server);
    }
}
