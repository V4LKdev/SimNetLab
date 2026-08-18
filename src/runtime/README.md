# simnet_runtime

`simnet_runtime` plans fixed-step process frames, tracks runtime counters, evaluates run limits, and
provides owner-thread shutdown state.

`plan_runtime_frame` accepts the nonnegative part of each frame delta up to
`RuntimeSettings::max_frame_time`. Rejected excess is reported as `clamped_time`. Catch-up work is
limited separately. Whole-step backlog beyond that limit is reported as `dropped_time`, while the
fractional accumulator remainder is preserved.

Limits set to zero are disabled. `reached_runtime_limit` checks tick, frame, then runtime-duration
limits and returns the corresponding `ShutdownReason`.
