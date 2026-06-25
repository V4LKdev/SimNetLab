#pragma once

#include "net_types.hpp"
#include "core/math/vec3.hpp"
#include "telemetry/telemetry.hpp"
#include "core/utils/time_keeper.hpp"

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
    * Tracks the connection lifecycle, activity timestamps, and the reason for any pending disconnection.
    */
    class PeerState {
    public:
        explicit PeerState(PeerID id) : peer_id_(id)
        {
        }

        PeerState(const PeerState &) = default;

        PeerState &operator=(const PeerState &) = default;

        PeerState(PeerState &&) = default;

        PeerState &operator=(PeerState &&) = default;


        void record_connect_time(const utils::TimePoint &now) { connect_time_ = now; }

        // --- State transitions ---
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

        // --- Accessors ---
        [[nodiscard]] PeerID get_id() const { return peer_id_; }
        [[nodiscard]] utils::TimePoint connect_time() const { return connect_time_; }
        [[nodiscard]] bool is_handshake_complete() const { return state_ == ConnectionState::connected; }

        [[nodiscard]] uint32_t next_sequence() const { return next_sequence_; }
        void increment_sequence() { ++next_sequence_; }

        [[nodiscard]] uint64_t last_acknowledged_tick() const { return last_acknowledged_tick_; }
        void set_last_acknowledged_tick(uint64_t tick) { last_acknowledged_tick_ = tick; }

        [[nodiscard]] const math::Vec3& viewport_center() const { return viewport_center_; }
        void set_viewport_center(const math::Vec3& center) { viewport_center_ = center; }

        [[nodiscard]] const std::vector<uint32_t>& visible_indices() const { return visible_indices_; }
        void set_visible_indices(std::vector<uint32_t> indices) { visible_indices_ = std::move(indices); }


    private:
        PeerID peer_id_;
        ConnectionState state_ = ConnectionState::disconnected;
        utils::TimePoint connect_time_{};

        uint32_t next_sequence_ = 0;
        uint64_t last_acknowledged_tick_ = 0;
        math::Vec3 viewport_center_ = math::Vec3::zero();
        std::vector<uint32_t> visible_indices_;
    };
}
