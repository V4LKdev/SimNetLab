# simnet_runtime

`simnet_runtime` provides reusable process-loop planning vocabulary on top of
the fixed-step arithmetic in `simnet_core`.

It owns steady-clock frame sampling, fixed-step frame plans, runtime counters,
run-limit evaluation, shutdown reasons, and an owner-thread stop request. It does
not own an application loop or depend on config, telemetry, transport,
pipeline, Flecs, rendering, or game modules.

`plan_runtime_frame` accepts the nonnegative portion of each raw frame delta up
to `RuntimeSettings::max_frame_time`. Rejected excess is reported as
`clamped_time`. It separately limits catch-up work and discards whole-step
backlog remaining after the step cap while preserving the fractional
accumulator remainder. That overload loss is reported as `dropped_time`. Both
measurements appear in the returned `RuntimeFramePlan` and cumulative
`RuntimeStats`.

Limits with a value of zero are disabled. `reached_runtime_limit` evaluates
tick, frame, then runtime-duration limits and returns the corresponding
`ShutdownReason`.
