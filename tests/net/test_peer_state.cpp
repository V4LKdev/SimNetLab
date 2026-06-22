#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "core/net/peer_state.hpp"
#include "core/utils/time_keeper.hpp"

using simnet::core::net::internal::PeerState;
using simnet::core::net::internal::ConnectionState;
using simnet::core::net::internal::DisconnectReason;
using simnet::core::utils::TimePoint;

TEST_CASE("PeerState: initial state", "[peer_state]")
{
    PeerState ps(1);
    REQUIRE(ps.get_id() == 1);
    REQUIRE(ps.get_state() == ConnectionState::disconnected);
    REQUIRE(!ps.is_handshake_complete());
}

TEST_CASE("PeerState: state transitions", "[peer_state]")
{
    PeerState ps(2);
    ps.mark_connecting();
    REQUIRE(ps.get_state() == ConnectionState::connecting);
    ps.mark_handshaking();
    REQUIRE(ps.get_state() == ConnectionState::handshaking);
    ps.mark_connected();
    REQUIRE(ps.get_state() == ConnectionState::connected);
    REQUIRE(ps.is_handshake_complete());
    ps.mark_disconnecting();
    REQUIRE(ps.get_state() == ConnectionState::disconnecting);
    ps.mark_disconnected();
    REQUIRE(ps.get_state() == ConnectionState::disconnected);
}

TEST_CASE("PeerState: connect time", "[peer_state]")
{
    PeerState ps(5);
    auto start = TimePoint{} + std::chrono::seconds(1);
    ps.record_connect_time(start);
    REQUIRE(ps.connect_time() == start);
    auto later = start + std::chrono::seconds(5);
    REQUIRE(ps.connect_time_seconds(later) == Catch::Approx(5.0f));
}
