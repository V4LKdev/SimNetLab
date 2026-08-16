#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

import simnet.app_protocol;
import simnet.core;

TEST_CASE("application join protocol preserves role and peer identity", "[app_protocol][join]")
{
    CHECK(simnet::app::application_protocol_version == 7U);
    CHECK(simnet::app::app_message_version == 2U);

    for (auto const role :
         {simnet::app::ClientRole::StationaryObserver, simnet::app::ClientRole::Player})
    {
        auto const request = simnet::app::AppMessage{
            .kind = simnet::app::AppMessageKind::JoinRequest,
            .role = role,
        };
        auto decoded_request = simnet::app::AppMessage{};
        REQUIRE(
            simnet::app::decode_app_message(
                simnet::app::encode_app_message(request),
                decoded_request
            )
        );
        CHECK(decoded_request.kind == request.kind);
        CHECK(decoded_request.role == role);

        auto const accepted = simnet::app::AppMessage{
            .kind = simnet::app::AppMessageKind::JoinAccepted,
            .role = role,
            .peer_id = 0x1234U,
            .player_id = role == simnet::app::ClientRole::Player ? 0x10203040U : 0U,
        };
        auto const bytes = simnet::app::encode_app_message(accepted);
        auto decoded_accepted = simnet::app::AppMessage{};
        REQUIRE(simnet::app::decode_app_message(bytes, decoded_accepted));
        CHECK(decoded_accepted.kind == accepted.kind);
        CHECK(decoded_accepted.role == role);
        CHECK(decoded_accepted.peer_id == accepted.peer_id);
        CHECK(decoded_accepted.player_id == accepted.player_id);
    }

    auto incompatible = simnet::app::encode_app_message({
        .kind = simnet::app::AppMessageKind::JoinRequest,
        .role = simnet::app::ClientRole::Player,
    });
    incompatible[1] = simnet::Byte{1U};

    auto decoded = simnet::app::AppMessage{};
    CHECK_FALSE(simnet::app::decode_app_message(incompatible, decoded));
}

TEST_CASE("snapshot ACK round-trips replication progress", "[app_protocol][ack]")
{
    auto const expected = simnet::app::SnapshotAck{
        .newest_received_snapshot = 9U,
        .received_mask = 0x12345678U,
        .newest_applied_snapshot = 8U,
    };
    auto const bytes = simnet::app::encode_snapshot_ack(expected);

    CHECK(bytes.size() == 14U);
    CHECK(simnet::app::decode_app_message_kind(bytes) == simnet::app::AppMessageKind::SnapshotAck);

    auto decoded = simnet::app::SnapshotAck{};
    REQUIRE(simnet::app::decode_snapshot_ack(bytes, decoded));
    CHECK(decoded.newest_received_snapshot == expected.newest_received_snapshot);
    CHECK(decoded.received_mask == expected.received_mask);
    CHECK(decoded.newest_applied_snapshot == expected.newest_applied_snapshot);

    auto malformed = bytes;
    malformed.push_back(simnet::Byte{});
    CHECK_FALSE(simnet::app::decode_snapshot_ack(malformed, decoded));
}

TEST_CASE("Player input round-trips every keyboard and mouse button", "[app_protocol][player]")
{
    using simnet::app::PlayerButton;
    auto const buttons = std::array{
        PlayerButton::W,
        PlayerButton::A,
        PlayerButton::S,
        PlayerButton::D,
        PlayerButton::Shift,
        PlayerButton::Control,
        PlayerButton::LeftMouse,
        PlayerButton::RightMouse,
    };
    auto mask = std::uint8_t{};
    for (auto const button : buttons)
    {
        mask |= static_cast<std::uint8_t>(button);
    }

    auto const bytes = simnet::app::encode_player_input({.buttons = mask});
    CHECK(bytes.size() == 3U);
    CHECK(
        simnet::app::decode_app_message_kind(bytes) == simnet::app::AppMessageKind::PlayerInput
    );

    auto decoded = simnet::app::PlayerInputMessage{};
    REQUIRE(simnet::app::decode_player_input(bytes, decoded));
    CHECK(decoded.buttons == mask);
    for (auto const button : buttons)
    {
        CHECK(simnet::app::button_down(decoded, button));
    }

    auto malformed = bytes;
    malformed.pop_back();
    CHECK_FALSE(simnet::app::decode_player_input(malformed, decoded));
    CHECK(decoded.buttons == mask);
}

TEST_CASE("snapshot recovery request round-trips missing baseline identity", "[app_protocol][recovery]")
{
    auto const expected = simnet::app::SnapshotRecoveryRequest{
        .rejected_update_sequence = 19U,
        .missing_baseline_sequence = 12U,
    };
    auto const bytes = simnet::app::encode_snapshot_recovery_request(expected);

    CHECK(bytes.size() == 10U);
    CHECK(
        simnet::app::decode_app_message_kind(bytes) ==
        simnet::app::AppMessageKind::SnapshotRecoveryRequest
    );

    auto decoded = simnet::app::SnapshotRecoveryRequest{};
    REQUIRE(simnet::app::decode_snapshot_recovery_request(bytes, decoded));
    CHECK(decoded.rejected_update_sequence == expected.rejected_update_sequence);
    CHECK(decoded.missing_baseline_sequence == expected.missing_baseline_sequence);

    auto malformed = bytes;
    malformed.pop_back();
    CHECK_FALSE(simnet::app::decode_snapshot_recovery_request(malformed, decoded));
}

TEST_CASE("stationary observer interest round-trips the AOI source", "[app_protocol][aoi]")
{
    auto const message = simnet::app::StationaryObserverInterestMessage{
        .position = {1.0F, -2.0F, 3.0F},
        .forward = {0.0F, 0.0F, 2.0F},
    };
    auto const bytes = simnet::app::encode_stationary_observer_interest(message);

    CHECK(bytes.size() == 26U);

    auto decoded = simnet::app::StationaryObserverInterestMessage{};
    REQUIRE(simnet::app::decode_stationary_observer_interest(bytes, decoded));
    CHECK(decoded.position.x == 1.0F);
    CHECK(decoded.position.y == -2.0F);
    CHECK(decoded.position.z == 3.0F);
    CHECK(decoded.forward.x == 0.0F);
    CHECK(decoded.forward.y == 0.0F);
    CHECK(decoded.forward.z == 1.0F);

    auto invalid_direction = simnet::app::encode_stationary_observer_interest({
        .position = {},
        .forward = {},
    });
    CHECK_FALSE(simnet::app::decode_stationary_observer_interest(invalid_direction, decoded));
}
