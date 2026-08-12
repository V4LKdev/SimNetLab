module;

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

export module simnet.app_snapshot_delivery;

import simnet.core;
import simnet.snapshot;

export namespace simnet::app
{
    /// Per-peer exact-result bounds. Capacity accounting uses actual SoA vector capacities.
    inline constexpr std::size_t maximum_retained_results = 64U;
    inline constexpr std::uint64_t maximum_retained_capacity_bytes = 128ULL * 1024ULL * 1024ULL;
    inline constexpr Nanoseconds maximum_retained_result_age = std::chrono::seconds(5);
    /// Covers the maintained 100k workload plus application-owned entities with headroom.
    inline constexpr std::size_t maximum_recovery_upserts = 131'072U;

    enum class SnapshotRecoveryReason : std::uint8_t
    {
        None,
        NoAcknowledgedBaseline,
        ClientRequest,
        MissingRetainedResult,
        AckStalled,
        RetentionPressure,
        BaselineExpired
    };

    [[nodiscard]] constexpr std::string_view
    snapshot_recovery_reason_name(SnapshotRecoveryReason reason) noexcept
    {
        switch (reason)
        {
            case SnapshotRecoveryReason::None:
                return "none";
            case SnapshotRecoveryReason::NoAcknowledgedBaseline:
                return "no_acknowledged_baseline";
            case SnapshotRecoveryReason::ClientRequest:
                return "client_request";
            case SnapshotRecoveryReason::MissingRetainedResult:
                return "missing_retained_result";
            case SnapshotRecoveryReason::AckStalled:
                return "ack_stalled";
            case SnapshotRecoveryReason::RetentionPressure:
                return "retention_pressure";
            case SnapshotRecoveryReason::BaselineExpired:
                return "baseline_expired";
        }
        return "unknown";
    }

    struct SubmittedSnapshotResult
    {
        SequenceId sequence{};
        WorldSnapshot snapshot{};
        SnapshotKind kind{SnapshotKind::FullReplace};
        Nanoseconds submitted_at{};
        std::uint64_t capacity_bytes{};
    };

    struct SnapshotRetentionPlan
    {
        bool valid{};
        std::size_t evict_count{};
        std::uint64_t result_capacity_bytes{};
    };

    enum class AckPromotionOutcome : std::uint8_t
    {
        Promoted,
        Duplicate,
        Stale,
        Future,
        Missing
    };

    struct SnapshotDeliveryState
    {
        SequenceId latest_submitted_sequence{};
        SequenceId latest_acknowledged_sequence{};
        std::optional<SubmittedSnapshotResult> acknowledged{};
        std::deque<SubmittedSnapshotResult> submitted{};
        std::uint64_t retained_capacity_bytes{};
        std::vector<EntityState> recovery_upserts{};
        std::uint32_t submissions_since_ack_progress{};
        Nanoseconds latest_ack_progress_time{};
        bool recovery_active{true};
        SnapshotRecoveryReason recovery_reason{SnapshotRecoveryReason::NoAcknowledgedBaseline};
        std::uint64_t forced_full_replace_count{};
        std::uint64_t baseline_eviction_count{};
        std::uint64_t recovery_request_count{};
    };

    struct ClientRecoveryRequestState
    {
        std::optional<SequenceId> requested_missing_baseline{};
        std::uint64_t sent_count{};
        std::uint64_t missing_baseline_rejection_count{};
    };

    [[nodiscard]] bool recovery_request_needed(
        ClientRecoveryRequestState const& state,
        SequenceId missing_baseline
    ) noexcept
    {
        return missing_baseline != 0U && state.requested_missing_baseline != missing_baseline;
    }

    void
    record_recovery_request(ClientRecoveryRequestState& state, SequenceId missing_baseline) noexcept
    {
        state.requested_missing_baseline = missing_baseline;
        ++state.sent_count;
    }

    void record_missing_baseline_rejection(ClientRecoveryRequestState& state) noexcept
    {
        ++state.missing_baseline_rejection_count;
    }

    void record_snapshot_progress(ClientRecoveryRequestState& state) noexcept
    {
        state.requested_missing_baseline.reset();
    }

    [[nodiscard]] bool valid_recovery_request(
        SnapshotDeliveryState const& state,
        SequenceId rejected_update_sequence,
        SequenceId missing_baseline_sequence
    ) noexcept
    {
        return rejected_update_sequence != 0U && missing_baseline_sequence != 0U &&
               missing_baseline_sequence < rejected_update_sequence &&
               missing_baseline_sequence >= state.latest_acknowledged_sequence &&
               rejected_update_sequence > state.latest_acknowledged_sequence &&
               rejected_update_sequence <= state.latest_submitted_sequence;
    }

    [[nodiscard]] std::uint64_t snapshot_capacity_bytes(WorldSnapshot const& snapshot) noexcept
    {
        auto total = std::uint64_t{};
        auto add_capacity = [&total](std::size_t capacity, std::size_t element_bytes)
        {
            auto const widened_capacity = static_cast<std::uint64_t>(capacity);
            auto const widened_element_bytes = static_cast<std::uint64_t>(element_bytes);
            if (widened_capacity >
                (std::numeric_limits<std::uint64_t>::max() - total) / widened_element_bytes)
            {
                total = std::numeric_limits<std::uint64_t>::max();
                return;
            }
            total += widened_capacity * widened_element_bytes;
        };
        add_capacity(snapshot.ids.capacity(), sizeof(EntityNetId));
        add_capacity(snapshot.classifications.capacity(), sizeof(EntityClassification));
        add_capacity(snapshot.positions.capacity(), sizeof(Vec3f));
        add_capacity(snapshot.headings.capacity(), sizeof(Vec3f));
        add_capacity(snapshot.hues.capacity(), sizeof(std::uint8_t));
        return total;
    }

    [[nodiscard]] std::uint64_t
    snapshot_diagnostic_fingerprint(WorldSnapshot const& snapshot) noexcept
    {
        auto hash = std::uint64_t{14695981039346656037ULL};
        auto append_u32 = [&hash](std::uint32_t value)
        {
            for (auto shift : {24U, 16U, 8U, 0U})
            {
                hash ^= (value >> shift) & 0xFFU;
                hash *= 1099511628211ULL;
            }
        };
        for (auto index = std::size_t{}; index < snapshot.size(); ++index)
        {
            append_u32(snapshot.ids[index]);
            append_u32(snapshot.classifications[index].value());
            append_u32(std::bit_cast<std::uint32_t>(snapshot.positions[index].x));
            append_u32(std::bit_cast<std::uint32_t>(snapshot.positions[index].y));
            append_u32(std::bit_cast<std::uint32_t>(snapshot.positions[index].z));
            append_u32(std::bit_cast<std::uint32_t>(snapshot.headings[index].x));
            append_u32(std::bit_cast<std::uint32_t>(snapshot.headings[index].y));
            append_u32(std::bit_cast<std::uint32_t>(snapshot.headings[index].z));
            append_u32(snapshot.hues[index]);
        }
        return hash;
    }

    void
    enter_snapshot_recovery(SnapshotDeliveryState& state, SnapshotRecoveryReason reason) noexcept
    {
        state.recovery_active = true;
        state.recovery_reason = reason;
    }

    [[nodiscard]] bool ack_progress_stalled(
        SnapshotDeliveryState const& state,
        std::uint32_t full_replace_after_unacknowledged_updates
    ) noexcept
    {
        return full_replace_after_unacknowledged_updates != 0U &&
               state.submissions_since_ack_progress >= full_replace_after_unacknowledged_updates;
    }

    void discard_acknowledged_replica(
        SnapshotDeliveryState& state,
        SnapshotRecoveryReason reason
    ) noexcept
    {
        if (state.acknowledged.has_value())
        {
            state.retained_capacity_bytes -= state.acknowledged->capacity_bytes;
            state.acknowledged.reset();
            ++state.baseline_eviction_count;
        }
        state.recovery_upserts.clear();
        enter_snapshot_recovery(state, reason);
    }

    [[nodiscard]] SnapshotRetentionPlan plan_snapshot_retention(
        SnapshotDeliveryState const& state,
        WorldSnapshot const& result
    ) noexcept
    {
        auto const result_bytes = snapshot_capacity_bytes(result);
        if (result_bytes > maximum_retained_capacity_bytes)
        {
            return {};
        }
        auto bytes = state.retained_capacity_bytes;
        auto count = state.submitted.size();
        auto evict_count = std::size_t{};
        while ((count >= maximum_retained_results ||
                bytes > maximum_retained_capacity_bytes - result_bytes) &&
               evict_count < state.submitted.size())
        {
            bytes -= state.submitted[evict_count].capacity_bytes;
            --count;
            ++evict_count;
        }
        if (count >= maximum_retained_results ||
            bytes > maximum_retained_capacity_bytes - result_bytes)
        {
            return {};
        }
        return {
            .valid = true,
            .evict_count = evict_count,
            .result_capacity_bytes = result_bytes,
        };
    }

    [[nodiscard]] bool same_entity_state(EntityState const& left, EntityState const& right) noexcept
    {
        return left.id == right.id && left.classification == right.classification &&
               left.position.x == right.position.x && left.position.y == right.position.y &&
               left.position.z == right.position.z && left.heading.x == right.heading.x &&
               left.heading.y == right.heading.y && left.heading.z == right.heading.z &&
               left.hue == right.hue;
    }

    [[nodiscard]] std::optional<EntityState>
    find_entity_state(WorldSnapshot const& snapshot, EntityNetId id)
    {
        auto const found = std::ranges::lower_bound(snapshot.ids, id);
        if (found == snapshot.ids.end() || *found != id)
        {
            return std::nullopt;
        }
        auto const index = static_cast<std::size_t>(std::distance(snapshot.ids.begin(), found));
        return EntityState{
            .id = id,
            .classification = snapshot.classifications[index],
            .position = snapshot.positions[index],
            .heading = snapshot.headings[index],
            .hue = snapshot.hues[index],
        };
    }

    [[nodiscard]] bool
    merge_recovery_upserts(SnapshotDeliveryState& state, SnapshotUpdate const& update)
    {
        for (auto const& upsert : update.upserts)
        {
            auto const found =
                std::ranges::lower_bound(state.recovery_upserts, upsert.id, {}, &EntityState::id);
            if (found != state.recovery_upserts.end() && found->id == upsert.id)
            {
                *found = upsert;
                continue;
            }

            if (state.recovery_upserts.size() >= maximum_recovery_upserts)
            {
                state.recovery_upserts.clear();
                enter_snapshot_recovery(state, SnapshotRecoveryReason::RetentionPressure);
                return false;
            }
            state.recovery_upserts.insert(found, upsert);
        }
        return true;
    }

    void commit_submitted_snapshot(
        SnapshotDeliveryState& state,
        SequenceId sequence,
        WorldSnapshot result,
        SnapshotKind kind,
        Nanoseconds submitted_at,
        SnapshotRetentionPlan const& plan
    )
    {
        for (auto index = std::size_t{}; index < plan.evict_count; ++index)
        {
            state.retained_capacity_bytes -= state.submitted.front().capacity_bytes;
            state.submitted.pop_front();
            ++state.baseline_eviction_count;
        }
        state.retained_capacity_bytes += plan.result_capacity_bytes;
        state.submitted.push_back({
            .sequence = sequence,
            .snapshot = std::move(result),
            .kind = kind,
            .submitted_at = submitted_at,
            .capacity_bytes = plan.result_capacity_bytes,
        });
        state.latest_submitted_sequence = sequence;
        ++state.submissions_since_ack_progress;
        if (state.recovery_active && kind == SnapshotKind::FullReplace)
        {
            ++state.forced_full_replace_count;
        }
    }

    [[nodiscard]] AckPromotionOutcome
    promote_snapshot_ack(SnapshotDeliveryState& state, SequenceId sequence, Nanoseconds now)
    {
        if (sequence == state.latest_acknowledged_sequence && sequence != 0U)
        {
            return AckPromotionOutcome::Duplicate;
        }
        if (sequence < state.latest_acknowledged_sequence)
        {
            return AckPromotionOutcome::Stale;
        }
        if (sequence == 0U || sequence > state.latest_submitted_sequence)
        {
            return AckPromotionOutcome::Future;
        }
        auto found =
            std::ranges::find(state.submitted, sequence, &SubmittedSnapshotResult::sequence);
        if (found == state.submitted.end())
        {
            enter_snapshot_recovery(state, SnapshotRecoveryReason::MissingRetainedResult);
            return AckPromotionOutcome::Missing;
        }

        auto promoted = std::move(*found);
        auto const promoted_full_replace = promoted.kind == SnapshotKind::FullReplace;
        auto const erase_end = std::next(found);
        for (auto iterator = state.submitted.begin(); iterator != erase_end; ++iterator)
        {
            state.retained_capacity_bytes -= iterator->capacity_bytes;
        }
        state.submitted.erase(state.submitted.begin(), erase_end);
        if (state.acknowledged.has_value())
        {
            state.retained_capacity_bytes -= state.acknowledged->capacity_bytes;
        }
        state.latest_acknowledged_sequence = sequence;
        state.latest_ack_progress_time = now;
        state.acknowledged = std::move(promoted);
        state.retained_capacity_bytes += state.acknowledged->capacity_bytes;
        state.submissions_since_ack_progress = 0U;

        auto retained_count = std::size_t{};
        for (auto const& recovery : state.recovery_upserts)
        {
            auto const acknowledged = find_entity_state(state.acknowledged->snapshot, recovery.id);
            if (acknowledged.has_value() && !same_entity_state(*acknowledged, recovery))
            {
                state.recovery_upserts[retained_count++] = recovery;
            }
        }
        state.recovery_upserts.resize(retained_count);

        if (!state.recovery_active || promoted_full_replace)
        {
            state.recovery_active = false;
            state.recovery_reason = SnapshotRecoveryReason::None;
        }
        return AckPromotionOutcome::Promoted;
    }

    void expire_retained_snapshots(SnapshotDeliveryState& state, Nanoseconds now)
    {
        while (!state.submitted.empty() &&
               now - state.submitted.front().submitted_at > maximum_retained_result_age)
        {
            state.retained_capacity_bytes -= state.submitted.front().capacity_bytes;
            state.submitted.pop_front();
            ++state.baseline_eviction_count;
        }
        if (state.acknowledged.has_value() &&
            now - state.acknowledged->submitted_at > maximum_retained_result_age)
        {
            discard_acknowledged_replica(state, SnapshotRecoveryReason::BaselineExpired);
        }
    }
}
