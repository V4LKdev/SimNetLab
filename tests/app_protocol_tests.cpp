#include <array>
#include <catch2/catch_test_macros.hpp>

import simnet.app_protocol;
import simnet.core;

TEST_CASE("application role and pause messages round-trip exactly", "[app_protocol]")
{
    for (auto const message : std::array {
        simnet::app::AppMessage {
            .kind = simnet::app::AppMessageKind::PauseSetRequest,
            .paused = true,
        },
        simnet::app::AppMessage {
            .kind = simnet::app::AppMessageKind::PauseState,
            .paused = false,
        },
        simnet::app::AppMessage {
            .kind = simnet::app::AppMessageKind::JoinRequest,
            .role = simnet::app::ClientRole::Player,
        },
        simnet::app::AppMessage {
            .kind = simnet::app::AppMessageKind::JoinAccepted,
            .role = simnet::app::ClientRole::Player,
            .player_id = 42U,
        },
    }) {
        auto const bytes = simnet::app::encode_app_message(message);
        auto decoded = simnet::app::AppMessage {};
        REQUIRE(simnet::app::decode_app_message(bytes, decoded));
        CHECK(decoded.kind == message.kind);
        CHECK(decoded.role == message.role);
        CHECK(decoded.player_id == message.player_id);
        CHECK(decoded.paused == message.paused);
    }
}

TEST_CASE("application protocol rejects malformed roles and versions", "[app_protocol]")
{
    auto decoded = simnet::app::AppMessage {};
    CHECK_FALSE(simnet::app::decode_app_message(
        std::array<simnet::Byte, 3> {
            static_cast<simnet::Byte>(simnet::app::AppMessageKind::JoinRequest),
            simnet::Byte { 99U },
            simnet::Byte { 0U },
        },
        decoded
    ));
    CHECK_FALSE(simnet::app::decode_app_message(
        std::array<simnet::Byte, 3> {
            static_cast<simnet::Byte>(simnet::app::AppMessageKind::JoinRequest),
            static_cast<simnet::Byte>(simnet::app::app_message_version),
            simnet::Byte { 99U },
        },
        decoded
    ));
    auto invalid_observer = simnet::app::encode_app_message({
        .kind = simnet::app::AppMessageKind::JoinAccepted,
        .role = simnet::app::ClientRole::Observer,
        .player_id = 1U,
    });
    CHECK_FALSE(simnet::app::decode_app_message(invalid_observer, decoded));
}

TEST_CASE("player input is a versioned one-byte button state", "[app_protocol]")
{
    auto const input = simnet::app::PlayerInputMessage {
        .buttons = static_cast<std::uint8_t>(simnet::app::PlayerButton::W)
            | static_cast<std::uint8_t>(simnet::app::PlayerButton::Shift)
            | static_cast<std::uint8_t>(simnet::app::PlayerButton::LeftMouse),
    };
    auto const bytes = simnet::app::encode_player_input(input);
    REQUIRE(bytes.size() == 2U);
    auto decoded = simnet::app::PlayerInputMessage {};
    REQUIRE(simnet::app::decode_player_input(bytes, decoded));
    CHECK(decoded.buttons == input.buttons);
    CHECK(simnet::app::button_down(decoded, simnet::app::PlayerButton::W));
    CHECK(simnet::app::button_down(decoded, simnet::app::PlayerButton::Shift));
    CHECK(simnet::app::button_down(decoded, simnet::app::PlayerButton::LeftMouse));
    CHECK_FALSE(simnet::app::button_down(decoded, simnet::app::PlayerButton::S));

    auto invalid = bytes;
    invalid[0] = simnet::Byte { 2U };
    CHECK_FALSE(simnet::app::decode_player_input(invalid, decoded));
}
