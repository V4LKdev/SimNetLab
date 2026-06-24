
# Test Plan
## Phase 1: Core comparison (single tech, one client, local loopback)
- Reset state: seed fixed, starting with 0 boids, ramp to 500 in 10 seconds, steady state for 30s, then ramp again.
- Metrics per tick: bandwidth, packet size, CPU time, entity count.
- Collect 30 runs per configuration, compute 95% CI

This provides the pure impact of each technique, isolating variables.

## Phase 2: Stress / ceiling test (one client, bandwidth cap)
- Set bandwidth cap
- Increase entity count from 100 to saturation by spawning batches every 5 seconds.
- Measure when bandwidth exceeds cap, and when CPU time exceeds budget.

This provides the MAX entities number per technique.

## Phase 3: Multi-client
- Add 2-4 clients with AoI, track per-client bandwidth and divergence.
- Map server load/out bandwidth vs single client to chart efficiency.

This provides information about techniques that really shine and become apparent under load or several clients.

## Phase 4: Robustness under network conditions
- tc-netem to add 2% loss, 50ms rtt, and 5ms jitter.
- Evaluate client divergence, and possible rollback performance from systems

This is essential for validating robustness of techniques.

## Statistical approach
- Start with simple repeated-measures ANOVA (within-subject factor = technique)
- If sphericity violated, use Greenhouse-Geisser correction.
(sphericity meaning: the variances of the differences between all combinations of factor levels should be the same.)
- Post-hoc pairwise comparisons with Bonferroni correction.
- Euclidean distance divergence and delta gamma report mean +- SD.
- Control variates can be adopted later.
- I dont think I wanna do this again :(

## Tool use
Google Benchmark gives:
- Automatic Repetition
- Time Measurement
- Preventing Optimization if relevant
- Custom metrics (report bandwidth, packet count, etc alongside CPU time)
- Multiple output formats.
- Benchmark Fixtures for core test scenarios

Batch scripts for tc-netem and config changes and CI pipeline.

Bencher:
- Reproducibility
- Automation (run google benchmark inside with different parameters)
- Tracking and Alerts.