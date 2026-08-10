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

module simnet.transport;

import :protocol;

namespace simnet
{
    using namespace transport_protocol;

    namespace
    {
        [[nodiscard]] TransportResult ok() noexcept
        {
            return {};
        }

        [[nodiscard]] TransportResult
        fail(TransportErrorCode code, std::string message, std::uint32_t native_code = 0)
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

        constexpr PeerId server_peer_id = 1;
        constexpr auto handshake_timeout = std::chrono::seconds(5);

        [[nodiscard]] DisconnectCode disconnect_code(std::uint32_t native_code) noexcept
        {
            auto const code = static_cast<DisconnectCode>(native_code);
            return valid_disconnect_code(code) ? code : DisconnectCode::TransportError;
        }

        [[nodiscard]] bool
        payload_size_allowed(std::size_t size, TransportLimits const& limits) noexcept
        {
            if (size > max_reassembled_payload_bytes)
            {
                return false;
            }
            return limits.size_policy != SendSizePolicy::EnforceLimit ||
                   size <= limits.max_payload_bytes;
        }

        [[nodiscard]] enet_uint32 delivery_flags(TransportDelivery delivery) noexcept
        {
            switch (delivery)
            {
                case TransportDelivery::ReliableSequenced:
                    return static_cast<enet_uint32>(ENET_PACKET_FLAG_RELIABLE);
                case TransportDelivery::UnreliableSequenced:
                    return 0;
            }
            return 0;
        }

        [[nodiscard]] TransportDelivery packet_delivery(ENetPacket const& packet) noexcept
        {
            if ((packet.flags & ENET_PACKET_FLAG_RELIABLE) != 0U)
            {
                return TransportDelivery::ReliableSequenced;
            }
            return TransportDelivery::UnreliableSequenced;
        }

        [[nodiscard]] ReceivedPacket
        received_packet(PeerId peer, TransportLane lane, ENetPacket const& packet)
        {
            auto payload = std::vector<Byte>(packet.dataLength);
            std::memcpy(payload.data(), packet.data, packet.dataLength);
            return {
                .peer = peer,
                .lane = lane,
                .delivery = packet_delivery(packet),
                .payload = std::move(payload),
            };
        }

        [[nodiscard]] std::size_t unfragmented_payload_limit(ENetPeer const& peer) noexcept
        {
            auto overhead = sizeof(ENetProtocolHeader) + sizeof(ENetProtocolSendFragment);
            if (peer.host->checksum != nullptr)
            {
                overhead += sizeof(enet_uint32);
            }
            return peer.mtu > overhead ? peer.mtu - overhead : 0U;
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
                if (initialized)
                {
                    enet_deinitialize();
                }
            }

            bool initialized{};
        };

        [[nodiscard]] EnetRuntime& enet_runtime()
        {
            static auto runtime = EnetRuntime{};
            return runtime;
        }

        [[nodiscard]] TransportResult require_enet()
        {
            return enet_runtime().initialized
                       ? ok()
                       : fail(TransportErrorCode::BackendError, "ENet initialization failed");
        }

        [[nodiscard]] bool set_enet_address_host(ENetAddress& address, std::string const& host)
        {
            return enet_address_set_host_ip(&address, host.c_str()) == 0 ||
                   enet_address_set_host(&address, host.c_str()) == 0;
        }

        void add_send_stats(TransportStats& stats, TransportLane lane, std::size_t bytes)
        {
            auto const index = lane_index(lane);
            ++stats.packets_sent;
            stats.bytes_sent += bytes;
            ++stats.lane_packets_sent[index];
            stats.lane_bytes_sent[index] += bytes;
        }

        void add_receive_stats(TransportStats& stats, TransportLane lane, std::size_t bytes)
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
            TransportLane lane,
            TransportDelivery delivery,
            std::span<Byte const> payload
        )
        {
            if (!valid_lane(lane))
            {
                ++stats.send_failures;
                return fail(TransportErrorCode::InvalidLane, "invalid transport lane");
            }
            if (!valid_delivery(delivery))
            {
                ++stats.send_failures;
                return fail(TransportErrorCode::InvalidDelivery, "invalid transport delivery mode");
            }
            if (limits.size_policy == SendSizePolicy::EnforceLimit &&
                payload.size() > limits.max_payload_bytes)
            {
                ++stats.send_failures;
                ++stats.oversize_drops;
                return fail(
                    TransportErrorCode::PayloadTooLarge,
                    "transport payload exceeds configured limit"
                );
            }
            if (payload.size() > max_reassembled_payload_bytes)
            {
                ++stats.send_failures;
                ++stats.oversize_drops;
                return fail(
                    TransportErrorCode::PayloadTooLarge,
                    "transport payload exceeds hard payload limit"
                );
            }
            if (delivery == TransportDelivery::UnreliableSequenced &&
                payload.size() > unfragmented_payload_limit(*peer))
            {
                ++stats.send_failures;
                ++stats.oversize_drops;
                return fail(
                    TransportErrorCode::PayloadTooLarge,
                    "unreliable sequenced payload would require ENet fragmentation"
                );
            }

            auto* packet =
                enet_packet_create(payload.data(), payload.size(), delivery_flags(delivery));
            if (packet == nullptr)
            {
                ++stats.send_failures;
                return fail(TransportErrorCode::BackendError, "ENet packet allocation failed");
            }
            if (enet_peer_send(peer, lane_index(lane), packet) != 0)
            {
                enet_packet_destroy(packet);
                ++stats.send_failures;
                return fail(TransportErrorCode::BackendError, "ENet send failed");
            }

            add_send_stats(stats, lane, payload.size());
            return ok();
        }

        [[nodiscard]] PeerStats make_peer_stats(ENetPeer const* peer) noexcept
        {
            if (peer == nullptr)
            {
                return {};
            }
            return {
                .rtt_ms = static_cast<double>(peer->roundTripTime),
                .packet_loss_ratio = static_cast<double>(peer->packetLoss) /
                                     static_cast<double>(ENET_PEER_PACKET_LOSS_SCALE),
                .reliable_bytes_in_flight = peer->reliableDataInTransit,
                .waiting_bytes = peer->totalWaitingData,
                .mtu = peer->mtu,
            };
        }

        struct PeerSlot
        {
            PeerId id{};
            ENetPeer* peer{};
            bool session_ready{};
            std::chrono::steady_clock::time_point connected_at{};
        };

    } // namespace

    struct TransportServer::Impl
    {
      public:
        ~Impl()
        {
            stop();
        }

        TransportResult start(TransportServerSettings const& settings)
        {
            if (host_ != nullptr)
            {
                return fail(
                    TransportErrorCode::AlreadyStarted,
                    "transport server is already started"
                );
            }
            settings_ = settings;
            counters_ = {};
            peers_.clear();
            expired_peer_ids_.clear();
            next_peer_id_ = 1;

            if (auto ready = require_enet(); !ready.ok)
            {
                return ready;
            }
            if (settings.port == 0 || settings.max_peers == 0)
            {
                return fail(
                    TransportErrorCode::InvalidAddress,
                    "server transport port and max_peers must be non-zero"
                );
            }
            expired_peer_ids_.reserve(settings.max_peers);

            auto address = ENetAddress{.host = ENET_HOST_ANY, .port = settings.port};
            if (!settings.bind_address.empty() &&
                !set_enet_address_host(address, settings.bind_address))
            {
                return fail(TransportErrorCode::InvalidAddress, "invalid server bind address");
            }

            host_ = enet_host_create(&address, settings.max_peers, channel_count, 0, 0);
            if (host_ == nullptr)
            {
                return fail(TransportErrorCode::BackendError, "failed to create ENet server host");
            }
            return ok();
        }

        void stop() noexcept
        {
            if (host_ != nullptr)
            {
                enet_host_destroy(host_);
                host_ = nullptr;
            }
            peers_.clear();
        }

        bool is_running() const noexcept
        {
            return host_ != nullptr;
        }

        TransportResult poll(std::vector<TransportEvent>& out_events, std::uint32_t timeout_ms)
        {
            if (host_ == nullptr)
            {
                return fail(TransportErrorCode::NotStarted, "transport server is not started");
            }

            auto event = ENetEvent{};
            auto wait_ms = timeout_ms;
            for (;;)
            {
                auto const service_result = enet_host_service(host_, &event, wait_ms);
                if (service_result < 0)
                {
                    return fail(TransportErrorCode::BackendError, "ENet server service failed");
                }
                if (service_result == 0)
                {
                    break;
                }
                wait_ms = 0;
                if (event.type == ENET_EVENT_TYPE_CONNECT)
                {
                    auto const id = next_peer_id_++;
                    set_peer_id(event.peer, id);
                    peers_.push_back({
                        .id = id,
                        .peer = event.peer,
                        .connected_at = std::chrono::steady_clock::now(),
                    });
                    out_events.push_back(PeerConnected{.peer = id});
                }
                else if (event.type == ENET_EVENT_TYPE_RECEIVE)
                {
                    handle_receive(event, out_events);
                    enet_packet_destroy(event.packet);
                }
                else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
                {
                    auto const id = event_peer_id(event.peer);
                    ++counters_.disconnects;
                    out_events.push_back(
                        PeerDisconnected{
                            .peer = id,
                            .code = disconnect_code(event.data),
                            .native_reason = event.data,
                        }
                    );
                    peers_.erase(
                        std::remove_if(
                            peers_.begin(),
                            peers_.end(),
                            [id](PeerSlot const& slot)
                            {
                                return slot.id == id;
                            }
                        ),
                        peers_.end()
                    );
                    event.peer->data = nullptr;
                }
            }
            expire_pending_sessions(out_events);
            return ok();
        }

        TransportResult send(SendPacket const& packet)
        {
            if (host_ == nullptr)
            {
                return fail(TransportErrorCode::NotStarted, "transport server is not started");
            }
            auto* slot = find(packet.peer);
            if (slot == nullptr)
            {
                return fail(TransportErrorCode::PeerNotFound, "transport peer was not found");
            }
            if (!slot->session_ready)
            {
                return fail(
                    TransportErrorCode::PeerNotReady,
                    "transport peer session is not ready"
                );
            }
            return send_to_peer(
                slot->peer,
                counters_,
                settings_.limits,
                packet.lane,
                packet.delivery,
                packet.payload
            );
        }

        void disconnect(PeerId peer, DisconnectCode code) noexcept
        {
            if (auto* slot = find(peer); slot != nullptr && slot->peer != nullptr)
            {
                auto const safe_code =
                    valid_disconnect_code(code) ? code : DisconnectCode::TransportError;
                enet_peer_disconnect_later(slot->peer, static_cast<std::uint32_t>(safe_code));
            }
        }

        TransportStats stats() const
        {
            return counters_;
        }

        PeerStats peer_stats(PeerId peer) const
        {
            auto const* slot = find(peer);
            return slot == nullptr ? PeerStats{} : make_peer_stats(slot->peer);
        }

      private:
        [[nodiscard]] PeerSlot* find(PeerId id) noexcept
        {
            auto found = std::ranges::find_if(
                peers_,
                [id](PeerSlot const& slot)
                {
                    return slot.id == id;
                }
            );
            return found == peers_.end() ? nullptr : &*found;
        }

        [[nodiscard]] PeerSlot const* find(PeerId id) const noexcept
        {
            auto found = std::ranges::find_if(
                peers_,
                [id](PeerSlot const& slot)
                {
                    return slot.id == id;
                }
            );
            return found == peers_.end() ? nullptr : &*found;
        }

        [[nodiscard]] TransportResult send_session(ENetPeer* peer, SessionMessage const& message)
        {
            auto bytes = encode_session_message(message);
            return send_to_peer(
                peer,
                counters_,
                {
                    .max_payload_bytes = settings_.limits.max_payload_bytes,
                    .size_policy = SendSizePolicy::AllowBackendFragmentation,
                },
                TransportLane::Lane0,
                TransportDelivery::ReliableSequenced,
                bytes
            );
        }

        void handle_client_hello(
            PeerSlot& slot,
            SessionMessage const& message,
            std::vector<TransportEvent>& events
        )
        {
            if (slot.session_ready)
            {
                events.push_back(
                    TransportErrorEvent{
                        .message = "duplicate ClientHello after session ready",
                    }
                );
                disconnect(slot.id, DisconnectCode::ProtocolMismatch);
                return;
            }

            auto const mismatch = identity_mismatch(message.identity, settings_.expected_identity);
            if (mismatch != DisconnectCode::None)
            {
                static_cast<void>(send_session(
                    slot.peer,
                    {
                        .kind = SessionMessageKind::ServerReject,
                        .reject_code = mismatch,
                    }
                ));
                events.push_back(
                    TransportErrorEvent{
                        .message = "client session identity mismatch",
                    }
                );
                disconnect(slot.id, mismatch);
                return;
            }

            auto accepted = send_session(slot.peer, {.kind = SessionMessageKind::ServerAccept});
            if (!accepted.ok)
            {
                events.push_back(
                    TransportErrorEvent{
                        .message = accepted.error.message,
                    }
                );
                disconnect(slot.id, DisconnectCode::TransportError);
                return;
            }
            slot.session_ready = true;
            events.push_back(PeerSessionReady{.peer = slot.id});
        }

        void handle_receive(ENetEvent const& event, std::vector<TransportEvent>& out_events)
        {
            auto const peer = event_peer_id(event.peer);
            auto const lane = static_cast<TransportLane>(event.channelID);
            if (!valid_lane(lane))
            {
                out_events.push_back(
                    TransportErrorEvent{
                        .message = "received packet on invalid ENet channel",
                    }
                );
                return;
            }
            auto* slot = find(peer);
            auto const handshake_packet =
                slot != nullptr && !slot->session_ready && lane == TransportLane::Lane0;
            auto const size_allowed =
                handshake_packet ? event.packet->dataLength <= max_session_message_bytes
                                 : payload_size_allowed(event.packet->dataLength, settings_.limits);
            if (!size_allowed)
            {
                ++counters_.oversize_drops;
                out_events.push_back(
                    TransportErrorEvent{
                        .message = "received ENet packet exceeds transport receive limit"
                    }
                );
                disconnect(peer, DisconnectCode::ProtocolMismatch);
                return;
            }
            add_receive_stats(counters_, lane, event.packet->dataLength);

            if (slot == nullptr)
            {
                return;
            }
            if (!slot->session_ready)
            {
                auto message = SessionMessage{};
                auto const* data = reinterpret_cast<Byte const*>(event.packet->data);
                if (lane != TransportLane::Lane0 ||
                    !decode_session_message(ByteSpan{data, event.packet->dataLength}, message) ||
                    message.kind != SessionMessageKind::ClientHello)
                {
                    out_events.push_back(
                        TransportErrorEvent{
                            .message = "invalid client session message",
                        }
                    );
                    disconnect(peer, DisconnectCode::ProtocolMismatch);
                }
                else
                {
                    handle_client_hello(*slot, message, out_events);
                }
            }
            else
            {
                out_events.push_back(received_packet(peer, lane, *event.packet));
            }
        }

        void expire_pending_sessions(std::vector<TransportEvent>& out_events)
        {
            auto const now = std::chrono::steady_clock::now();
            expired_peer_ids_.clear();
            for (auto const& slot : peers_)
            {
                if (!slot.session_ready && now - slot.connected_at >= handshake_timeout)
                {
                    enet_peer_disconnect_now(
                        slot.peer,
                        static_cast<std::uint32_t>(DisconnectCode::Timeout)
                    );
                    slot.peer->data = nullptr;
                    ++counters_.disconnects;
                    out_events.push_back(
                        PeerDisconnected{
                            .peer = slot.id,
                            .code = DisconnectCode::Timeout,
                            .native_reason = static_cast<std::uint32_t>(DisconnectCode::Timeout),
                        }
                    );
                    expired_peer_ids_.push_back(slot.id);
                }
            }
            peers_.erase(
                std::remove_if(
                    peers_.begin(),
                    peers_.end(),
                    [this](PeerSlot const& slot)
                    {
                        return std::ranges::find(expired_peer_ids_, slot.id) !=
                               expired_peer_ids_.end();
                    }
                ),
                peers_.end()
            );
        }

        ENetHost* host_{};
        TransportServerSettings settings_{};
        TransportStats counters_{};
        std::vector<PeerSlot> peers_;
        std::vector<PeerId> expired_peer_ids_;
        PeerId next_peer_id_{1};
    };

    struct TransportClient::Impl
    {
      public:
        ~Impl()
        {
            disconnect(DisconnectCode::None);
        }

        TransportResult connect(TransportClientSettings const& settings)
        {
            if (host_ != nullptr)
            {
                return fail(
                    TransportErrorCode::AlreadyStarted,
                    "transport client is already connected or connecting"
                );
            }
            settings_ = settings;
            counters_ = {};
            transport_connected_ = false;
            session_ready_ = false;

            if (auto ready = require_enet(); !ready.ok)
            {
                return ready;
            }
            if (settings.server_port == 0 || settings.server_address.empty())
            {
                return fail(
                    TransportErrorCode::InvalidAddress,
                    "client server address and port are required"
                );
            }

            host_ = enet_host_create(nullptr, 1, channel_count, 0, 0);
            if (host_ == nullptr)
            {
                return fail(TransportErrorCode::BackendError, "failed to create ENet client host");
            }

            auto address = ENetAddress{
                .host = 0,
                .port = settings.server_port,
            };
            if (!set_enet_address_host(address, settings.server_address))
            {
                disconnect(DisconnectCode::None);
                return fail(TransportErrorCode::InvalidAddress, "invalid server address");
            }

            server_ = enet_host_connect(host_, &address, channel_count, 0);
            if (server_ == nullptr)
            {
                disconnect(DisconnectCode::None);
                return fail(
                    TransportErrorCode::ConnectionFailed,
                    "failed to create ENet server peer"
                );
            }
            set_peer_id(server_, server_peer_id);
            connect_started_at_ = std::chrono::steady_clock::now();
            return ok();
        }

        void disconnect(DisconnectCode code) noexcept
        {
            if (server_ != nullptr)
            {
                auto* disconnecting_peer = server_;
                auto const safe_code =
                    valid_disconnect_code(code) ? code : DisconnectCode::TransportError;
                enet_peer_disconnect(disconnecting_peer, static_cast<std::uint32_t>(safe_code));
                enet_host_flush(host_);

                auto event = ENetEvent{};
                auto const deadline =
                    std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
                while (std::chrono::steady_clock::now() < deadline)
                {
                    auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                        deadline - std::chrono::steady_clock::now()
                    );
                    auto const timeout =
                        static_cast<std::uint32_t>(std::max<std::int64_t>(remaining.count(), 0));
                    auto const service_result = enet_host_service(host_, &event, timeout);
                    if (service_result <= 0)
                    {
                        break;
                    }
                    if (event.type == ENET_EVENT_TYPE_RECEIVE)
                    {
                        enet_packet_destroy(event.packet);
                    }
                    if (event.type == ENET_EVENT_TYPE_DISCONNECT &&
                        event.peer == disconnecting_peer)
                    {
                        break;
                    }
                }
                server_ = nullptr;
            }
            if (host_ != nullptr)
            {
                enet_host_destroy(host_);
                host_ = nullptr;
            }
            transport_connected_ = false;
            session_ready_ = false;
        }

        bool is_connected() const noexcept
        {
            return transport_connected_;
        }

        bool is_started() const noexcept
        {
            return host_ != nullptr;
        }

        bool is_session_ready() const noexcept
        {
            return session_ready_;
        }

        TransportResult poll(std::vector<TransportEvent>& out_events, std::uint32_t timeout_ms)
        {
            if (host_ == nullptr)
            {
                return fail(TransportErrorCode::NotStarted, "transport client is not connected");
            }

            auto event = ENetEvent{};
            auto wait_ms = timeout_ms;
            for (;;)
            {
                auto const service_result = enet_host_service(host_, &event, wait_ms);
                if (service_result < 0)
                {
                    return fail(TransportErrorCode::BackendError, "ENet client service failed");
                }
                if (service_result == 0)
                {
                    break;
                }
                wait_ms = 0;
                if (event.type == ENET_EVENT_TYPE_CONNECT)
                {
                    transport_connected_ = true;
                    out_events.push_back(
                        PeerConnected{
                            .peer = server_peer_id,
                        }
                    );
                    auto sent = send_session({
                        .kind = SessionMessageKind::ClientHello,
                        .identity = settings_.identity,
                    });
                    if (!sent.ok)
                    {
                        out_events.push_back(
                            TransportErrorEvent{
                                .message = sent.error.message,
                            }
                        );
                    }
                }
                else if (event.type == ENET_EVENT_TYPE_RECEIVE)
                {
                    handle_receive(event, out_events);
                    enet_packet_destroy(event.packet);
                }
                else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
                {
                    ++counters_.disconnects;
                    out_events.push_back(
                        PeerDisconnected{
                            .peer = server_peer_id,
                            .code = disconnect_code(event.data),
                            .native_reason = event.data,
                        }
                    );
                    server_ = nullptr;
                    transport_connected_ = false;
                    session_ready_ = false;
                }
            }
            expire_pending_session(out_events);
            return ok();
        }

        TransportResult
        send(TransportLane lane, TransportDelivery delivery, std::span<Byte const> payload)
        {
            if (host_ == nullptr || server_ == nullptr)
            {
                return fail(TransportErrorCode::NotStarted, "transport client is not connected");
            }
            if (!session_ready_)
            {
                return fail(
                    TransportErrorCode::PeerNotReady,
                    "transport server session is not ready"
                );
            }
            return send_to_peer(server_, counters_, settings_.limits, lane, delivery, payload);
        }

        TransportStats stats() const
        {
            return counters_;
        }

        PeerStats server_stats() const
        {
            return make_peer_stats(server_);
        }

      private:
        [[nodiscard]] TransportResult
        send_session(SessionMessage const& message, TransportLane lane = TransportLane::Lane0)
        {
            auto bytes = encode_session_message(message);
            return send_to_peer(
                server_,
                counters_,
                {
                    .max_payload_bytes = settings_.limits.max_payload_bytes,
                    .size_policy = SendSizePolicy::AllowBackendFragmentation,
                },
                lane,
                TransportDelivery::ReliableSequenced,
                bytes
            );
        }

        void handle_session_receive(
            ENetEvent const& event,
            TransportLane lane,
            std::vector<TransportEvent>& out_events
        )
        {
            auto message = SessionMessage{};
            auto const* data = reinterpret_cast<Byte const*>(event.packet->data);
            if (lane != TransportLane::Lane0 ||
                !decode_session_message(ByteSpan{data, event.packet->dataLength}, message))
            {
                out_events.push_back(
                    TransportErrorEvent{
                        .message = "invalid server session message",
                    }
                );
                return;
            }
            if (message.kind == SessionMessageKind::ServerAccept)
            {
                session_ready_ = true;
                out_events.push_back(
                    PeerSessionReady{
                        .peer = server_peer_id,
                    }
                );
                return;
            }
            if (message.kind == SessionMessageKind::ServerReject)
            {
                out_events.push_back(
                    TransportErrorEvent{
                        .message = "server rejected transport session",
                    }
                );
                out_events.push_back(
                    PeerDisconnected{
                        .peer = server_peer_id,
                        .code = message.reject_code,
                        .native_reason = static_cast<std::uint32_t>(message.reject_code),
                    }
                );
                ++counters_.disconnects;
                enet_peer_disconnect_now(
                    event.peer,
                    static_cast<std::uint32_t>(message.reject_code)
                );
                server_ = nullptr;
                transport_connected_ = false;
                session_ready_ = false;
                return;
            }

            out_events.push_back(
                TransportErrorEvent{
                    .message = "invalid server control message",
                }
            );
            enet_peer_disconnect_now(
                event.peer,
                static_cast<std::uint32_t>(DisconnectCode::ProtocolMismatch)
            );
            server_ = nullptr;
            transport_connected_ = false;
            session_ready_ = false;
        }

        void handle_receive(ENetEvent const& event, std::vector<TransportEvent>& out_events)
        {
            auto const lane = static_cast<TransportLane>(event.channelID);
            if (!valid_lane(lane))
            {
                out_events.push_back(
                    TransportErrorEvent{
                        .message = "received packet on invalid ENet channel",
                    }
                );
                return;
            }
            auto const handshake_packet = !session_ready_ && lane == TransportLane::Lane0;
            auto const size_allowed =
                handshake_packet ? event.packet->dataLength <= max_session_message_bytes
                                 : payload_size_allowed(event.packet->dataLength, settings_.limits);
            if (!size_allowed)
            {
                ++counters_.oversize_drops;
                out_events.push_back(
                    TransportErrorEvent{
                        .message = "received ENet packet exceeds transport receive limit"
                    }
                );
                out_events.push_back(
                    PeerDisconnected{
                        .peer = server_peer_id,
                        .code = DisconnectCode::ProtocolMismatch,
                        .native_reason =
                            static_cast<std::uint32_t>(DisconnectCode::ProtocolMismatch),
                    }
                );
                ++counters_.disconnects;
                enet_peer_disconnect_now(
                    event.peer,
                    static_cast<std::uint32_t>(DisconnectCode::ProtocolMismatch)
                );
                server_ = nullptr;
                transport_connected_ = false;
                session_ready_ = false;
                return;
            }
            add_receive_stats(counters_, lane, event.packet->dataLength);

            if (!session_ready_)
            {
                handle_session_receive(event, lane, out_events);
                return;
            }
            out_events.push_back(received_packet(server_peer_id, lane, *event.packet));
        }

        void expire_pending_session(std::vector<TransportEvent>& out_events)
        {
            if (server_ == nullptr || session_ready_ ||
                std::chrono::steady_clock::now() - connect_started_at_ < handshake_timeout)
            {
                return;
            }
            enet_peer_disconnect_now(server_, static_cast<std::uint32_t>(DisconnectCode::Timeout));
            server_ = nullptr;
            transport_connected_ = false;
            session_ready_ = false;
            ++counters_.disconnects;
            out_events.push_back(
                PeerDisconnected{
                    .peer = server_peer_id,
                    .code = DisconnectCode::Timeout,
                    .native_reason = static_cast<std::uint32_t>(DisconnectCode::Timeout),
                }
            );
        }

        ENetHost* host_{};
        ENetPeer* server_{};
        TransportClientSettings settings_{};
        TransportStats counters_{};
        bool transport_connected_{};
        bool session_ready_{};
        std::chrono::steady_clock::time_point connect_started_at_{};
    };

    TransportServer::TransportServer() : impl_(new Impl{})
    {
    }

    TransportServer::~TransportServer()
    {
        stop();
        delete impl_;
    }

    TransportServer::TransportServer(TransportServer&& other) noexcept
        : impl_(std::exchange(other.impl_, nullptr))
    {
    }

    TransportServer& TransportServer::operator=(TransportServer&& other) noexcept
    {
        if (this != &other)
        {
            stop();
            delete impl_;
            impl_ = std::exchange(other.impl_, nullptr);
        }
        return *this;
    }

    TransportResult TransportServer::start(TransportServerSettings const& settings)
    {
        if (impl_ == nullptr || impl_->is_running())
        {
            return fail(TransportErrorCode::AlreadyStarted, "transport server is already started");
        }
        return impl_->start(settings);
    }

    void TransportServer::stop() noexcept
    {
        if (impl_ != nullptr)
        {
            impl_->stop();
        }
    }

    bool TransportServer::is_running() const noexcept
    {
        return impl_ != nullptr && impl_->is_running();
    }

    TransportResult
    TransportServer::poll(std::vector<TransportEvent>& out_events, std::uint32_t timeout_ms)
    {
        if (impl_ == nullptr)
        {
            return fail(TransportErrorCode::NotStarted, "transport server is not started");
        }
        return impl_->poll(out_events, timeout_ms);
    }

    TransportResult TransportServer::send(SendPacket const& packet)
    {
        if (impl_ == nullptr)
        {
            return fail(TransportErrorCode::NotStarted, "transport server is not started");
        }
        return impl_->send(packet);
    }

    void TransportServer::disconnect(PeerId peer, DisconnectCode code) noexcept
    {
        if (impl_ != nullptr)
        {
            impl_->disconnect(peer, code);
        }
    }

    TransportStats TransportServer::stats() const
    {
        return impl_ == nullptr ? TransportStats{} : impl_->stats();
    }

    PeerStats TransportServer::peer_stats(PeerId peer) const
    {
        return impl_ == nullptr ? PeerStats{} : impl_->peer_stats(peer);
    }

    TransportClient::TransportClient() : impl_(new Impl{})
    {
    }

    TransportClient::~TransportClient()
    {
        disconnect(DisconnectCode::None);
        delete impl_;
    }

    TransportClient::TransportClient(TransportClient&& other) noexcept
        : impl_(std::exchange(other.impl_, nullptr))
    {
    }

    TransportClient& TransportClient::operator=(TransportClient&& other) noexcept
    {
        if (this != &other)
        {
            disconnect(DisconnectCode::None);
            delete impl_;
            impl_ = std::exchange(other.impl_, nullptr);
        }
        return *this;
    }

    TransportResult TransportClient::connect(TransportClientSettings const& settings)
    {
        if (impl_ == nullptr || impl_->is_started())
        {
            return fail(
                TransportErrorCode::AlreadyStarted,
                "transport client is already connected or connecting"
            );
        }
        return impl_->connect(settings);
    }

    void TransportClient::disconnect(DisconnectCode code) noexcept
    {
        if (impl_ != nullptr)
        {
            impl_->disconnect(code);
        }
    }

    bool TransportClient::is_connected() const noexcept
    {
        return impl_ != nullptr && impl_->is_connected();
    }

    bool TransportClient::is_session_ready() const noexcept
    {
        return impl_ != nullptr && impl_->is_session_ready();
    }

    TransportResult
    TransportClient::poll(std::vector<TransportEvent>& out_events, std::uint32_t timeout_ms)
    {
        if (impl_ == nullptr)
        {
            return fail(TransportErrorCode::NotStarted, "transport client is not connected");
        }
        return impl_->poll(out_events, timeout_ms);
    }

    TransportResult TransportClient::send(
        TransportLane lane,
        TransportDelivery delivery,
        std::span<Byte const> payload
    )
    {
        if (impl_ == nullptr)
        {
            return fail(TransportErrorCode::NotStarted, "transport client is not connected");
        }
        return impl_->send(lane, delivery, payload);
    }

    TransportStats TransportClient::stats() const
    {
        return impl_ == nullptr ? TransportStats{} : impl_->stats();
    }

    PeerStats TransportClient::server_stats() const
    {
        return impl_ == nullptr ? PeerStats{} : impl_->server_stats();
    }
} // namespace simnet
