#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

import simnet.app_player_input;
import simnet.app_protocol;
import simnet.core;

namespace
{
    struct ObservedSubmission
    {
        simnet::app::PlayerInputSubmissionCause cause{};
        std::uint8_t buttons{};

        [[nodiscard]] bool operator==(ObservedSubmission const&) const noexcept = default;
    };

    [[nodiscard]] std::vector<ObservedSubmission> sampled_schedule(std::uint64_t samples_per_second)
    {
        auto state = simnet::app::PlayerInputDeliveryState{};
        auto observed = std::vector<ObservedSubmission>{};
        for (auto sample = std::uint64_t{};; ++sample)
        {
            auto const now = simnet::Nanoseconds{
                static_cast<std::int64_t>(sample * 1'000'000'000ULL / samples_per_second)
            };
            if (now > simnet::Nanoseconds{900'000'000})
            {
                break;
            }
            auto const active =
                now >= simnet::Nanoseconds{250'000'000} && now < simnet::Nanoseconds{550'000'000};
            simnet::app::set_desired_player_input(
                state,
                {.buttons = active ? static_cast<std::uint8_t>(simnet::app::PlayerButton::W)
                                   : std::uint8_t{}}
            );
            auto const submission = simnet::app::plan_player_input_submission(state, true, now);
            if (!submission.has_value())
            {
                continue;
            }
            observed.push_back({
                .cause = submission->cause,
                .buttons = submission->input.buttons,
            });
            simnet::app::record_player_input_submission(state, *submission, now);
        }
        return observed;
    }
}

TEST_CASE(
    "Player input scheduler sends state changes and repeats every latest state",
    "[app_protocol][player][delivery]"
)
{
    using simnet::app::PlayerButton;
    using simnet::app::PlayerInputSubmissionCause;

    auto state = simnet::app::PlayerInputDeliveryState{};
    CHECK_FALSE(
        simnet::app::plan_player_input_submission(state, false, simnet::Nanoseconds{}).has_value()
    );

    auto initial = simnet::app::plan_player_input_submission(state, true, simnet::Nanoseconds{});
    REQUIRE(initial.has_value());
    CHECK(initial->cause == PlayerInputSubmissionCause::StateChange);
    CHECK(initial->input.buttons == 0U);
    simnet::app::record_player_input_submission(state, *initial, simnet::Nanoseconds{});
    CHECK(state.state_change_submission_count == 1U);
    CHECK(state.heartbeat_submission_count == 0U);

    CHECK_FALSE(
        simnet::app::plan_player_input_submission(
            state,
            true,
            simnet::app::player_input_heartbeat_interval - simnet::Nanoseconds{1}
        )
            .has_value()
    );
    auto neutral_heartbeat = simnet::app::plan_player_input_submission(
        state,
        true,
        simnet::app::player_input_heartbeat_interval
    );
    REQUIRE(neutral_heartbeat.has_value());
    CHECK(neutral_heartbeat->cause == PlayerInputSubmissionCause::Heartbeat);
    CHECK(neutral_heartbeat->input.buttons == 0U);
    simnet::app::record_player_input_submission(
        state,
        *neutral_heartbeat,
        simnet::app::player_input_heartbeat_interval
    );

    auto const active_buttons = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(PlayerButton::W) |
        static_cast<std::uint8_t>(PlayerButton::Shift) |
        static_cast<std::uint8_t>(PlayerButton::LeftMouse)
    );
    simnet::app::set_desired_player_input(state, {.buttons = active_buttons});
    auto active_change = simnet::app::plan_player_input_submission(
        state,
        true,
        simnet::app::player_input_heartbeat_interval + simnet::Nanoseconds{1}
    );
    REQUIRE(active_change.has_value());
    CHECK(active_change->cause == PlayerInputSubmissionCause::StateChange);
    CHECK(active_change->input.buttons == active_buttons);

    simnet::app::record_player_input_submission_failure(state);
    CHECK(state.failed_submission_count == 1U);
    CHECK(state.desired.buttons == active_buttons);
    CHECK(state.last_submitted.buttons == 0U);
    CHECK(state.has_submitted);
    CHECK(state.last_submission_time == simnet::app::player_input_heartbeat_interval);
    CHECK(state.state_change_submission_count == 1U);
    CHECK(state.heartbeat_submission_count == 1U);
    active_change = simnet::app::plan_player_input_submission(
        state,
        true,
        simnet::app::player_input_heartbeat_interval + simnet::Nanoseconds{1}
    );
    REQUIRE(active_change.has_value());
    simnet::app::record_player_input_submission(
        state,
        *active_change,
        simnet::app::player_input_heartbeat_interval + simnet::Nanoseconds{1}
    );

    auto const active_heartbeat_time =
        simnet::app::player_input_heartbeat_interval * 2 + simnet::Nanoseconds{1};
    CHECK_FALSE(
        simnet::app::plan_player_input_submission(
            state,
            true,
            active_heartbeat_time - simnet::Nanoseconds{1}
        )
            .has_value()
    );
    auto active_heartbeat =
        simnet::app::plan_player_input_submission(state, true, active_heartbeat_time);
    REQUIRE(active_heartbeat.has_value());
    CHECK(active_heartbeat->cause == PlayerInputSubmissionCause::Heartbeat);
    CHECK(active_heartbeat->input.buttons == active_buttons);
    simnet::app::record_player_input_submission(state, *active_heartbeat, active_heartbeat_time);

    auto authoritative_input = active_heartbeat->input;
    auto const neutral_change_time = active_heartbeat_time + simnet::Nanoseconds{1};
    simnet::app::set_desired_player_input(state, {});
    auto neutral_change =
        simnet::app::plan_player_input_submission(state, true, neutral_change_time);
    REQUIRE(neutral_change.has_value());
    CHECK(neutral_change->cause == PlayerInputSubmissionCause::StateChange);
    CHECK(neutral_change->input.buttons == 0U);
    simnet::app::record_player_input_submission(state, *neutral_change, neutral_change_time);

    CHECK(authoritative_input.buttons == active_buttons);
    CHECK_FALSE(
        simnet::app::plan_player_input_submission(
            state,
            true,
            neutral_change_time + simnet::app::player_input_heartbeat_interval -
                simnet::Nanoseconds{1}
        )
            .has_value()
    );
    auto recovery_heartbeat = simnet::app::plan_player_input_submission(
        state,
        true,
        neutral_change_time + simnet::app::player_input_heartbeat_interval
    );
    REQUIRE(recovery_heartbeat.has_value());
    CHECK(recovery_heartbeat->cause == PlayerInputSubmissionCause::Heartbeat);
    authoritative_input = recovery_heartbeat->input;
    CHECK(authoritative_input.buttons == 0U);
}

TEST_CASE(
    "Player input scheduler stalls and reconnects without inherited delivery state",
    "[app_protocol][player][peer][delivery]"
)
{
    auto state = simnet::app::PlayerInputDeliveryState{};
    simnet::app::set_desired_player_input(
        state,
        {.buttons = static_cast<std::uint8_t>(simnet::app::PlayerButton::D)}
    );
    auto initial = simnet::app::plan_player_input_submission(state, true, simnet::Nanoseconds{});
    REQUIRE(initial.has_value());
    simnet::app::record_player_input_submission(state, *initial, simnet::Nanoseconds{});

    auto const after_stall = simnet::Nanoseconds{5'000'000'000};
    auto heartbeat = simnet::app::plan_player_input_submission(state, true, after_stall);
    REQUIRE(heartbeat.has_value());
    CHECK(heartbeat->cause == simnet::app::PlayerInputSubmissionCause::Heartbeat);
    simnet::app::record_player_input_submission(state, *heartbeat, after_stall);
    CHECK_FALSE(simnet::app::plan_player_input_submission(state, true, after_stall).has_value());
    CHECK(state.heartbeat_submission_count == 1U);

    simnet::app::reset_player_input_delivery(state);
    CHECK_FALSE(state.has_submitted);
    CHECK(state.desired.buttons == 0U);
    CHECK(state.last_submitted.buttons == 0U);
    CHECK(state.last_submission_time == simnet::Nanoseconds{});
    CHECK(state.state_change_submission_count == 0U);
    CHECK(state.heartbeat_submission_count == 0U);
    CHECK(state.failed_submission_count == 0U);

    auto reconnected = simnet::app::plan_player_input_submission(state, true, after_stall);
    REQUIRE(reconnected.has_value());
    CHECK(reconnected->cause == simnet::app::PlayerInputSubmissionCause::StateChange);
    CHECK(reconnected->input.buttons == 0U);
}

TEST_CASE(
    "Player input schedule is independent of render sampling frequency",
    "[app_protocol][player][determinism]"
)
{
    auto const expected = sampled_schedule(30U);
    REQUIRE(expected.size() == 10U);
    CHECK(sampled_schedule(60U) == expected);
    CHECK(sampled_schedule(120U) == expected);
    CHECK(sampled_schedule(144U) == expected);
}

TEST_CASE("Every Player input semantic byte schedules immediate delivery", "[app_protocol][player]")
{
    auto state = simnet::app::PlayerInputDeliveryState{};
    auto initial = simnet::app::plan_player_input_submission(state, true, simnet::Nanoseconds{});
    REQUIRE(initial.has_value());
    simnet::app::record_player_input_submission(state, *initial, simnet::Nanoseconds{});

    for (auto button_value = std::uint16_t{1U}; button_value <= 255U; ++button_value)
    {
        auto const buttons = static_cast<std::uint8_t>(button_value);
        simnet::app::set_desired_player_input(state, {.buttons = buttons});
        auto submission =
            simnet::app::plan_player_input_submission(state, true, state.last_submission_time);
        REQUIRE(submission.has_value());
        CHECK(submission->cause == simnet::app::PlayerInputSubmissionCause::StateChange);
        CHECK(submission->input.buttons == buttons);
        simnet::app::record_player_input_submission(state, *submission, state.last_submission_time);
    }
}
