#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <utility>
#include <vector>

import simnet.app_protocol;
import simnet.core;

TEST_CASE("application role and pause messages round-trip exactly", "[app_protocol]")
{
    for (auto const message : std::array{
             simnet::app::AppMessage{
                 .kind = simnet::app::AppMessageKind::PauseSetRequest,
                 .paused = true,
             },
             simnet::app::AppMessage{
                 .kind = simnet::app::AppMessageKind::PauseState,
                 .paused = false,
             },
             simnet::app::AppMessage{
                 .kind = simnet::app::AppMessageKind::JoinRequest,
                 .role = simnet::app::ClientRole::Player,
             },
             simnet::app::AppMessage{
                 .kind = simnet::app::AppMessageKind::JoinAccepted,
                 .role = simnet::app::ClientRole::Player,
                 .player_id = 42U,
             },
         }) {
        auto const bytes = simnet::app::encode_app_message(message);
        auto decoded = simnet::app::AppMessage{};
        REQUIRE(simnet::app::decode_app_message(bytes, decoded));
        CHECK(decoded.kind == message.kind);
        CHECK(decoded.role == message.role);
        CHECK(decoded.player_id == message.player_id);
        CHECK(decoded.paused == message.paused);
    }
}

TEST_CASE("application protocol rejects malformed roles and versions", "[app_protocol]")
{
    auto decoded = simnet::app::AppMessage{};
    CHECK_FALSE(
        simnet::app::decode_app_message(
            std::array<simnet::Byte, 3>{
                static_cast<simnet::Byte>(simnet::app::AppMessageKind::JoinRequest),
                simnet::Byte{99U},
                simnet::Byte{0U},
            },
            decoded
        )
    );
    CHECK_FALSE(
        simnet::app::decode_app_message(
            std::array<simnet::Byte, 3>{
                static_cast<simnet::Byte>(simnet::app::AppMessageKind::JoinRequest),
                static_cast<simnet::Byte>(simnet::app::app_message_version),
                simnet::Byte{99U},
            },
            decoded
        )
    );
    auto invalid_stationary_observer = simnet::app::encode_app_message({
        .kind = simnet::app::AppMessageKind::JoinAccepted,
        .role = simnet::app::ClientRole::StationaryObserver,
        .player_id = 1U,
    });
    CHECK_FALSE(simnet::app::decode_app_message(invalid_stationary_observer, decoded));

    auto const stationary_observer = simnet::app::encode_app_message({
        .kind = simnet::app::AppMessageKind::JoinRequest,
        .role = simnet::app::ClientRole::StationaryObserver,
    });
    REQUIRE(stationary_observer.size() == 3U);
    CHECK(stationary_observer[2] == simnet::Byte{0U});
}

TEST_CASE("player input is a versioned one-byte button state", "[app_protocol]")
{
    auto const input = simnet::app::PlayerInputMessage{
        .buttons = static_cast<std::uint8_t>(simnet::app::PlayerButton::W)
            | static_cast<std::uint8_t>(simnet::app::PlayerButton::Shift)
            | static_cast<std::uint8_t>(simnet::app::PlayerButton::LeftMouse),
    };
    auto const bytes = simnet::app::encode_player_input(input);
    REQUIRE(bytes.size() == 3U);
    auto decoded = simnet::app::PlayerInputMessage{};
    REQUIRE(simnet::app::decode_player_input(bytes, decoded));
    CHECK(decoded.buttons == input.buttons);
    CHECK(simnet::app::button_down(decoded, simnet::app::PlayerButton::W));
    CHECK(simnet::app::button_down(decoded, simnet::app::PlayerButton::Shift));
    CHECK(simnet::app::button_down(decoded, simnet::app::PlayerButton::LeftMouse));
    CHECK_FALSE(simnet::app::button_down(decoded, simnet::app::PlayerButton::S));

    auto invalid = bytes;
    invalid[1] = simnet::Byte{2U};
    CHECK_FALSE(simnet::app::decode_player_input(invalid, decoded));
}

TEST_CASE("snapshot ACK uses the application-owned envelope", "[app_protocol][ack]")
{
    auto const expected = simnet::app::SnapshotAck{
        .newest_received_snapshot = 9U,
        .received_mask = 0x12345678U,
        .newest_applied_snapshot = 8U,
    };
    auto const bytes = simnet::app::encode_snapshot_ack(expected);
    REQUIRE(bytes.size() == 14U);
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

TEST_CASE("snapshot recovery request is versioned and transactional", "[app_protocol][recovery]")
{
    auto const expected = simnet::app::SnapshotRecoveryRequest{
        .rejected_update_sequence = 19U,
        .missing_baseline_sequence = 12U,
    };
    auto const bytes = simnet::app::encode_snapshot_recovery_request(expected);
    REQUIRE(bytes.size() == 10U);
    CHECK(
        simnet::app::decode_app_message_kind(bytes)
        == simnet::app::AppMessageKind::SnapshotRecoveryRequest
    );
    auto decoded = simnet::app::SnapshotRecoveryRequest{};
    REQUIRE(simnet::app::decode_snapshot_recovery_request(bytes, decoded));
    CHECK(decoded.rejected_update_sequence == expected.rejected_update_sequence);
    CHECK(decoded.missing_baseline_sequence == expected.missing_baseline_sequence);

    auto malformed = bytes;
    malformed.pop_back();
    CHECK_FALSE(simnet::app::decode_snapshot_recovery_request(malformed, decoded));
    malformed = bytes;
    malformed[2] = simnet::Byte{};
    malformed[3] = simnet::Byte{};
    malformed[4] = simnet::Byte{};
    malformed[5] = simnet::Byte{1U};
    CHECK_FALSE(simnet::app::decode_snapshot_recovery_request(malformed, decoded));
    CHECK(decoded.rejected_update_sequence == expected.rejected_update_sequence);
}

TEST_CASE(
    "stationary observer interest is finite normalized and transactionally decoded",
    "[app_protocol][aoi]"
)
{
    auto const message = simnet::app::StationaryObserverInterestMessage{
        .position = {1.0F, -2.0F, 3.0F},
        .forward = {0.0F, 0.0F, 2.0F},
    };
    auto const bytes = simnet::app::encode_stationary_observer_interest(message);
    REQUIRE(bytes.size() == 26U);
    auto decoded = simnet::app::StationaryObserverInterestMessage{};
    REQUIRE(simnet::app::decode_stationary_observer_interest(bytes, decoded));
    CHECK(decoded.position.x == 1.0F);
    CHECK(decoded.position.y == -2.0F);
    CHECK(decoded.position.z == 3.0F);
    CHECK(decoded.forward.x == 0.0F);
    CHECK(decoded.forward.y == 0.0F);
    CHECK(decoded.forward.z == 1.0F);

    auto reject = [&](std::vector<simnet::Byte> malformed) {
        auto destination = simnet::app::StationaryObserverInterestMessage{
            .position = {9.0F, 8.0F, 7.0F},
            .forward = {.x = 1.0F},
        };
        CHECK_FALSE(simnet::app::decode_stationary_observer_interest(malformed, destination));
        CHECK(destination.position.x == 9.0F);
        CHECK(destination.forward.x == 1.0F);
    };

    auto truncated = bytes;
    truncated.pop_back();
    reject(std::move(truncated));
    auto trailing = bytes;
    trailing.push_back(simnet::Byte{});
    reject(std::move(trailing));
    auto incompatible = bytes;
    incompatible[1] = simnet::Byte{2U};
    reject(std::move(incompatible));
    reject(
        simnet::app::encode_stationary_observer_interest({
            .position = {},
            .forward = {},
        })
    );
    reject(
        simnet::app::encode_stationary_observer_interest({
            .position = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
            .forward = {.z = 1.0F},
        })
    );
}

TEST_CASE(
    "stationary observer interest locks position and bounds update rate",
    "[app_protocol][aoi]"
)
{
    auto state = simnet::app::StationaryObserverInterestState{};
    auto const initial = simnet::app::StationaryObserverInterestMessage{
        .position = {1.0F, 2.0F, 3.0F},
        .forward = {.z = 1.0F},
    };
    CHECK(
        simnet::app::accept_stationary_observer_interest(
            state,
            initial,
            simnet::Nanoseconds{100'000'000}
        )
        == simnet::app::StationaryObserverInterestResult::Accepted
    );
    auto rotated = initial;
    rotated.forward = {.x = 1.0F};
    CHECK(
        simnet::app::accept_stationary_observer_interest(
            state,
            rotated,
            simnet::Nanoseconds{120'000'000}
        )
        == simnet::app::StationaryObserverInterestResult::RateLimited
    );
    CHECK(state.forward.z == 1.0F);
    CHECK(
        simnet::app::accept_stationary_observer_interest(
            state,
            rotated,
            simnet::Nanoseconds{150'000'000}
        )
        == simnet::app::StationaryObserverInterestResult::Accepted
    );
    auto translated = rotated;
    translated.position.x = 2.0F;
    CHECK(
        simnet::app::accept_stationary_observer_interest(
            state,
            translated,
            simnet::Nanoseconds{250'000'000}
        )
        == simnet::app::StationaryObserverInterestResult::PositionChanged
    );
    CHECK(state.position.x == 1.0F);
}
