module;

#include <chrono>
#include <cstdint>
#include <optional>

/// @brief Client-owned latest-state Player input delivery scheduling.
export module simnet.app_player_input;

import simnet.app_protocol;
import simnet.core;

export namespace simnet::app
{
    inline constexpr Nanoseconds player_input_heartbeat_interval{100'000'000};

    enum class PlayerInputSubmissionCause : std::uint8_t
    {
        StateChange,
        Heartbeat,
    };

    struct PlayerInputSubmission
    {
        PlayerInputMessage input{};
        PlayerInputSubmissionCause cause{PlayerInputSubmissionCause::StateChange};
    };

    /// Per-session authority for latest-state Player input delivery.
    struct PlayerInputDeliveryState
    {
        PlayerInputMessage desired{};
        PlayerInputMessage last_submitted{};
        bool has_submitted{};
        Nanoseconds last_submission_time{};
        std::uint64_t state_change_submission_count{};
        std::uint64_t heartbeat_submission_count{};
        std::uint64_t failed_submission_count{};
    };

    /// Clears all desired, committed, timing, and diagnostic state for a new session.
    void reset_player_input_delivery(PlayerInputDeliveryState& state) noexcept
    {
        state = {};
    }

    /// Replaces the latest input desired by the Client without committing a submission.
    void
    set_desired_player_input(PlayerInputDeliveryState& state, PlayerInputMessage desired) noexcept
    {
        state.desired = desired;
    }

    /// Plans an immediate state change or one heartbeat without changing delivery state.
    [[nodiscard]] std::optional<PlayerInputSubmission> plan_player_input_submission(
        PlayerInputDeliveryState const& state,
        bool accepted_player_session,
        Nanoseconds now
    ) noexcept
    {
        if (!accepted_player_session)
        {
            return std::nullopt;
        }
        if (!state.has_submitted || state.desired.buttons != state.last_submitted.buttons)
        {
            return PlayerInputSubmission{
                .input = state.desired,
                .cause = PlayerInputSubmissionCause::StateChange,
            };
        }
        if (now >= state.last_submission_time &&
            now - state.last_submission_time >= player_input_heartbeat_interval)
        {
            return PlayerInputSubmission{
                .input = state.desired,
                .cause = PlayerInputSubmissionCause::Heartbeat,
            };
        }
        return std::nullopt;
    }

    /// Commits a locally successful transport submission and its submission time.
    void record_player_input_submission(
        PlayerInputDeliveryState& state,
        PlayerInputSubmission const& submission,
        Nanoseconds now
    ) noexcept
    {
        state.last_submitted = submission.input;
        state.has_submitted = true;
        state.last_submission_time = now;
        if (submission.cause == PlayerInputSubmissionCause::StateChange)
        {
            ++state.state_change_submission_count;
        }
        else
        {
            ++state.heartbeat_submission_count;
        }
    }

    /// Records a failed transport submission without committing its input or timestamp.
    void record_player_input_submission_failure(PlayerInputDeliveryState& state) noexcept
    {
        ++state.failed_submission_count;
    }
}
