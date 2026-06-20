#pragma once
#include "net_types.hpp"
#include "telemetry.hpp"
#include "utils/time_keeper.hpp"

namespace simnet::core::net::internal {
    enum class ConnectionState {
        disconnected,
        connecting,
        handshaking,
        connected,
        disconnecting,
    };

    /**
    * @brief Per‑peer connection state and timing data.
    *
    * Tracks the connection lifecycle, activity timestamps for timeout
    * calculations, and the reason for any pending disconnection.
    *
    * @note This class may be extended with processor‑specific caches
    *       (delta baseline, AoI visibility set) in the future.
     */
    class PeerState {
    public:
        explicit PeerState(PeerID id) : peer_id_(id)
        {
        }

        PeerState(const PeerState &) = default;

        PeerState &operator=(const PeerState &) = default;

        // Allow move - enables proper container handling.
        PeerState(PeerState &&) = default;

        PeerState &operator=(PeerState &&) = default;

        [[nodiscard]] PeerID get_id() const { return peer_id_; }
        [[nodiscard]] ConnectionState get_state() const { return state_; }
        [[nodiscard]] bool is_connected() const { return state_ == ConnectionState::connected; }
        [[nodiscard]] utils::TimePoint last_activity_time() const { return last_activity_time_; }
        [[nodiscard]] utils::TimePoint connect_time() const { return connect_time_; }

        void record_activity(const utils::TimePoint &now) { last_activity_time_ = now; }

        // ----- State transitions -----
        void mark_connecting()
        {
            state_ = ConnectionState::connecting;
            TELEM_LOG_TRACE("PeerState {}: -> connecting", peer_id_);
        }

        void mark_handshaking()
        {
            state_ = ConnectionState::handshaking;
            TELEM_LOG_TRACE("PeerState {}: -> handshaking", peer_id_);
        }

        void mark_connected()
        {
            state_ = ConnectionState::connected;
            TELEM_LOG_TRACE("PeerState {}: -> connected", peer_id_);
            TELEM_COUNTER_INC("net.peer_state_connected", 1);
        }

        void mark_disconnecting()
        {
            state_ = ConnectionState::disconnecting;
            TELEM_LOG_TRACE("PeerState {}: -> disconnecting", peer_id_);
        }

        void mark_disconnected()
        {
            state_ = ConnectionState::disconnected;
            TELEM_LOG_TRACE("PeerState {}: -> disconnected", peer_id_);
            TELEM_COUNTER_INC("net.peer_state_disconnected", 1);
        }

        [[nodiscard]] bool is_handshake_complete() const { return state_ == ConnectionState::connected; }

        void set_pending_disconnect_reason(DisconnectReason reason) { pending_disconnect_reason_ = reason; }
        [[nodiscard]] DisconnectReason get_pending_disconnect_reason() const { return pending_disconnect_reason_; }

        void record_ping_sent(const utils::TimePoint &now) { last_ping_sent_time_ = now; }
        [[nodiscard]] utils::TimePoint last_ping_sent_time() const { return last_ping_sent_time_; }

        void record_connect_time(const utils::TimePoint &now) { connect_time_ = now; }

        [[nodiscard]] float connect_time_seconds(const utils::TimePoint &now) const
        {
            return std::chrono::duration<float>(now - connect_time_).count();
        }

    private:
        PeerID peer_id_;
        ConnectionState state_ = ConnectionState::disconnected;
        utils::TimePoint last_activity_time_{};
        utils::TimePoint last_ping_sent_time_{};
        utils::TimePoint connect_time_{};
        DisconnectReason pending_disconnect_reason_ = DisconnectReason::Other;
    };
}
