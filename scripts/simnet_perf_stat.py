#!/usr/bin/env python3
"""Collect capability-aware run-level perf stat evidence for SimNet processes."""

from __future__ import annotations

import argparse
import csv
import json
import os
import platform
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
from dataclasses import dataclass, field
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Callable, Iterable, Sequence


TOOL_VERSION = "1.1.0"
MANIFEST_SCHEMA_VERSION = 2
COUNTER_SCHEMA_VERSION = 1

MINIMUM_RUNNING_PERCENTAGE = 95.0

# Evidence schema
COUNTER_COLUMNS = [
    "schema_version",
    "run_id",
    "recorded_at_unix_ns",
    "record_order",
    "role",
    "pid",
    "process_start_ticks",
    "profile",
    "event_group",
    "event",
    "counter_status",
    "value",
    "unit",
    "counter_runtime_ns",
    "running_percentage",
    "perf_scaled_value",
    "metric_value",
    "metric_unit",
    "accepted_for_analysis",
    "error_detail",
]


# Perf profiles
PROFILES: dict[str, tuple[tuple[str, ...], ...]] = {
    "software": (("task-clock", "context-switches", "cpu-migrations", "page-faults"),),
    "core": (("cycles", "instructions"), ("branches", "branch-misses")),
    "cache": (("instructions", "cache-references", "cache-misses"),),
    "memory": (
        ("instructions", "L1-dcache-loads", "L1-dcache-load-misses"),
        ("instructions", "LLC-loads", "LLC-load-misses"),
        ("instructions", "dTLB-loads", "dTLB-load-misses"),
        ("instructions", "iTLB-loads", "iTLB-load-misses"),
    ),
    "stalls": (("cycles", "stalled-cycles-frontend", "stalled-cycles-backend"),),
}

RUN_ID_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}\Z")
ROLE_PATTERN = re.compile(r"[A-Za-z][A-Za-z0-9_-]*\Z")
PERMISSION_MARKERS = (
    "permission denied",
    "not permitted",
    "no permission",
    "access to performance monitoring",
    "perf_event_paranoid",
)
UNSUPPORTED_MARKERS = (
    "not supported",
    "unsupported",
    "event syntax error",
    "unknown event",
)


@dataclass(frozen=True)
class Target:
    role: str
    pid: int
    process_start_ticks: int | None


@dataclass
class CounterObservation:
    event: str
    group_index: int
    status: str
    value: str = ""
    unit: str = ""
    counter_runtime_ns: str = ""
    running_percentage: str = ""
    perf_scaled_value: str = ""
    metric_value: str = ""
    metric_unit: str = ""
    accepted: bool = False
    error_detail: str = ""


@dataclass
class Session:
    target: Target
    invocation: list[str]
    started_at_unix_ns: int
    process: subprocess.Popen[str] | None = None
    ended_at_unix_ns: int = 0
    return_code: int | None = None
    stderr: str = ""
    errors: list[str] = field(default_factory=list)
    observations: list[CounterObservation] = field(default_factory=list)
    final_process_start_ticks: int | None = None


def parse_target(value: str) -> tuple[str, int]:
    role, separator, pid_text = value.partition(":")
    if not separator or ROLE_PATTERN.fullmatch(role) is None:
        raise argparse.ArgumentTypeError("target must be ROLE:PID")
    try:
        pid = int(pid_text)
    except ValueError as error:
        raise argparse.ArgumentTypeError("target PID must be an integer") from error
    if pid <= 0:
        raise argparse.ArgumentTypeError("target PID must be positive")
    return role.lower(), pid


def validate_run_id(run_id: str) -> None:
    if RUN_ID_PATTERN.fullmatch(run_id) is None:
        raise ValueError("run ID must match [A-Za-z0-9][A-Za-z0-9._-]{0,63}")


def parse_process_start_ticks(text: str) -> int:
    # comm may contain spaces, so split only after its closing parenthesis.
    closing = text.rfind(")")
    if closing < 0:
        raise ValueError("malformed /proc PID stat")
    fields = text[closing + 2 :].split()
    if len(fields) <= 19:
        raise ValueError("truncated /proc PID stat")
    return int(fields[19])


def read_process_start_ticks(pid: int, proc_root: Path = Path("/proc")) -> int | None:
    try:
        stat = (proc_root / str(pid) / "stat").read_text(encoding="utf-8")
        return parse_process_start_ticks(stat)
    except FileNotFoundError:
        return None


def _read_optional(path: Path) -> dict[str, object]:
    try:
        return {"available": True, "path": str(path), "value": path.read_text().strip()}
    except FileNotFoundError:
        return {"available": False, "path": str(path), "reason": "not exposed by this kernel"}
    except PermissionError as error:
        return {
            "available": False,
            "path": str(path),
            "reason": f"permission denied: {error}",
        }
    except OSError as error:
        return {"available": False, "path": str(path), "reason": str(error)}


def cpu_model(proc_root: Path = Path("/proc")) -> str | None:
    try:
        for line in (proc_root / "cpuinfo").read_text(encoding="utf-8").splitlines():
            key, separator, value = line.partition(":")
            if separator and key.strip() in ("model name", "Hardware", "Processor"):
                return value.strip()
    except OSError:
        pass
    return None


def build_perf_invocation(
        perf_binary: str,
        target: Target,
        event_groups: Sequence[Sequence[str]],
        duration_seconds: float,
) -> list[str]:
    timeout_ms = max(1, round(duration_seconds * 1000.0))
    arguments = [perf_binary, "stat", "--no-big-num", "-x", ";"]
    for group in event_groups:
        arguments.extend(("-e", "{" + ",".join(group) + "}"))
    arguments.extend(("-p", str(target.pid), "--timeout", str(timeout_ms)))
    return arguments


def _decimal(value: str) -> Decimal | None:
    try:
        parsed = Decimal(value.strip().replace("%", ""))
    except InvalidOperation:
        return None
    return parsed if parsed.is_finite() else None


def _event_identity(observed: str, expected: Iterable[str]) -> tuple[str, str] | None:
    observed = observed.strip()
    scope = "generic"
    # Hybrid CPUs may emit one row per PMU for a generic event.
    if observed.startswith("cpu_") and observed.count("/") >= 2:
        scope, observed, _modifiers = observed.split("/", 2)
    for event in expected:
        if observed == event or observed == event + "/" or observed.startswith(event + ":"):
            return event, scope
    return None


def _aggregate_components(
        event: str,
        group_index: int,
        components: Sequence[CounterObservation],
) -> CounterObservation:
    if len(components) == 1:
        return components[0]

    unavailable = [
        component
        for component in components
        if component.status not in ("measured", "insufficient_running_time")
    ]
    if unavailable:
        priority = ("malformed_output", "permission_denied", "unsupported", "not_counted")
        status = next(
            (
                candidate
                for candidate in priority
                if any(component.status == candidate for component in unavailable)
            ),
            unavailable[0].status,
        )
        return CounterObservation(
            event,
            group_index,
            status,
            error_detail="one or more hybrid PMU components are unavailable",
        )

    values = [_decimal(component.value) for component in components]
    if any(value is None for value in values):
        return CounterObservation(
            event,
            group_index,
            "malformed_output",
            error_detail="hybrid PMU counter value is not numeric",
        )
    numeric_values = [value for value in values if value is not None]

    units = {component.unit for component in components}
    if len(units) != 1:
        return CounterObservation(
            event,
            group_index,
            "malformed_output",
            error_detail="hybrid PMU counter units differ",
        )

    runtimes = [_decimal(component.counter_runtime_ns) for component in components]
    numeric_runtimes = [runtime for runtime in runtimes if runtime is not None]
    runtime = (
        sum(numeric_runtimes, Decimal())
        if len(numeric_runtimes) == len(runtimes)
        else None
    )

    percentages = [_decimal(component.running_percentage) for component in components]
    numeric_percentages = [percentage for percentage in percentages if percentage is not None]
    running = (
        min(Decimal("100"), sum(numeric_percentages, Decimal()))
        if len(numeric_percentages) == len(percentages)
        else None
    )

    below_threshold = (
            running is not None
            and running < Decimal(str(MINIMUM_RUNNING_PERCENTAGE))
    )

    return CounterObservation(
        event=event,
        group_index=group_index,
        status="insufficient_running_time" if below_threshold else "measured",
        value=format(sum(numeric_values, Decimal()), "f"),
        unit=components[0].unit,
        counter_runtime_ns=format(runtime, "f") if runtime is not None else "",
        running_percentage=format(running, "f") if running is not None else "",
        perf_scaled_value=(
            "1" if running is not None and running < Decimal("100") else
            "0" if running is not None else
            ""
        ),
        accepted=not below_threshold,
        error_detail=(
            "combined hybrid PMU running percentage is below the accepted threshold"
            if below_threshold
            else ""
        ),
    )


# Perf output parsing

def parse_perf_output(
        output: str,
        event_groups: Sequence[Sequence[str]],
) -> tuple[list[CounterObservation], list[str]]:
    expected_slots = [
        (group_index, event)
        for group_index, group in enumerate(event_groups)
        for event in group
    ]
    expected_events = [event for _group_index, event in expected_slots]
    components: dict[tuple[int, str], list[CounterObservation]] = {}
    seen_by_scope: dict[tuple[int, str], set[str]] = {}
    group_cursor = 0
    diagnostics: list[str] = []
    reader = csv.reader(output.splitlines(), delimiter=";")
    for line_number, fields in enumerate(reader, start=1):
        if not fields or not any(field.strip() for field in fields):
            continue
        if fields[0].lstrip().startswith("#"):
            continue
        if fields[0].startswith("WARNING: events were regrouped to match PMUs"):
            continue
        if len(fields) < 3:
            diagnostics.append(f"line {line_number}: malformed perf output")
            continue
        identity = _event_identity(fields[2], expected_events)
        if identity is None:
            diagnostics.append(f"line {line_number}: unrecognized perf output")
            continue
        event, scope = identity
        while group_cursor < len(event_groups):
            group = event_groups[group_cursor]
            seen = seen_by_scope.get((group_cursor, scope), set())
            if event not in group or (
                    event == group[0] and seen and all(expected in seen for expected in group)
            ):
                group_cursor += 1
                continue
            break
        if group_cursor == len(event_groups):
            diagnostics.append(f"line {line_number}: event does not match a requested group")
            continue
        group_index = group_cursor
        slot = (group_index, event)

        value_token = fields[0].strip()
        unit = fields[1].strip()
        runtime = fields[3].strip() if len(fields) > 3 else ""
        percentage = fields[4].strip().removesuffix("%") if len(fields) > 4 else ""
        metric_value = fields[5].strip() if len(fields) > 5 else ""
        metric_unit = fields[6].strip() if len(fields) > 6 else ""
        lowered = value_token.lower()
        if "not supported" in lowered:
            observation = CounterObservation(event, group_index, "unsupported")
        elif "not counted" in lowered:
            observation = CounterObservation(event, group_index, "not_counted")
        elif _decimal(value_token) is None:
            observation = CounterObservation(
                event,
                group_index,
                "malformed_output",
                error_detail="counter value is not numeric",
            )
        else:
            running = _decimal(percentage) if percentage else None
            if percentage and running is None:
                observation = CounterObservation(
                    event,
                    group_index,
                    "malformed_output",
                    error_detail="running percentage is not numeric",
                )
            else:
                below_threshold = running is not None and running < Decimal(
                    str(MINIMUM_RUNNING_PERCENTAGE)
                )
                observation = CounterObservation(
                    event=event,
                    group_index=group_index,
                    status="insufficient_running_time" if below_threshold else "measured",
                    value=value_token,
                    unit=unit,
                    counter_runtime_ns=runtime if _decimal(runtime) is not None else "",
                    running_percentage=percentage,
                    perf_scaled_value=("1" if running < Decimal("100") else "0")
                    if running is not None
                    else "",
                    metric_value=metric_value if _decimal(metric_value) is not None else "",
                    metric_unit=metric_unit,
                    accepted=not below_threshold,
                    error_detail="running percentage is below the accepted threshold"
                    if below_threshold
                    else "",
                )
        components.setdefault(slot, []).append(observation)
        seen_by_scope.setdefault((group_index, scope), set()).add(event)

    observations = []
    for group_index, event in expected_slots:
        slot = (group_index, event)
        if slot in components:
            observations.append(
                _aggregate_components(event, group_index, components[slot])
            )
    return observations, diagnostics


def _default_failure_status(stderr: str, return_code: int | None) -> tuple[str, str]:
    lowered = stderr.lower()
    if any(marker in lowered for marker in PERMISSION_MARKERS):
        return "permission_denied", stderr.strip() or "perf permission denied"
    if any(marker in lowered for marker in UNSUPPORTED_MARKERS):
        return "unsupported", stderr.strip() or "event is unsupported"
    if return_code not in (None, 0):
        return "perf_failed", stderr.strip() or f"perf exited with status {return_code}"
    return "malformed_output", "perf did not emit a row for the requested event"


def _fill_missing_observations(
        parsed: Sequence[CounterObservation],
        event_groups: Sequence[Sequence[str]],
        status: str,
        detail: str,
) -> list[CounterObservation]:
    by_slot = {
        (observation.group_index, observation.event): observation for observation in parsed
    }
    result: list[CounterObservation] = []
    for group_index, group in enumerate(event_groups):
        for event in group:
            result.append(
                by_slot.get((group_index, event))
                or CounterObservation(event, group_index, status, error_detail=detail)
            )
    return result


def _override_observations(
        observations: Sequence[CounterObservation], status: str, detail: str
) -> None:
    for observation in observations:
        observation.status = status
        observation.accepted = False
        observation.error_detail = detail


def target_identity_status(session: Session) -> str:
    if session.target.process_start_ticks is None or session.final_process_start_ticks is None:
        return "target_exited"
    if session.target.process_start_ticks != session.final_process_start_ticks:
        return "target_reused"
    return "stable"


# Process management

def stop_children(processes: Iterable[subprocess.Popen[str]]) -> None:
    active = [process for process in processes if process.poll() is None]
    for process in active:
        try:
            process.terminate()
        except ProcessLookupError:
            pass
    for process in active:
        try:
            process.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=2.0)


def termination_signal_handler(_signum: int, _frame: object) -> None:
    raise KeyboardInterrupt


def resolve_perf_binary() -> tuple[str | None, str | None]:
    resolved = shutil.which("perf")
    if resolved is None:
        return None, "perf not found in PATH"
    return str(Path(resolved).resolve()), None


def perf_version(perf_binary: str) -> tuple[str | None, str | None]:
    try:
        result = subprocess.run(
            [perf_binary, "--version"],
            capture_output=True,
            text=True,
            timeout=5.0,
            check=False,
            env={**os.environ, "LC_ALL": "C"},
        )
    except OSError as error:
        return None, str(error)
    except subprocess.TimeoutExpired:
        return None, "perf --version timed out"
    output = (result.stdout or result.stderr).strip()
    if result.returncode != 0:
        return None, output or f"perf --version exited with status {result.returncode}"
    return output, None


def _prepare_output_directory(output_directory: Path) -> None:
    if output_directory.exists():
        if not output_directory.is_dir():
            raise FileExistsError(f"output path is not a directory: {output_directory}")
        if any(output_directory.iterdir()):
            raise FileExistsError(f"output directory is not empty: {output_directory}")
    else:
        output_directory.mkdir(parents=True)


def _host_manifest(proc_root: Path) -> dict[str, object]:
    return {
        "kernel": platform.release(),
        "hostname": socket.gethostname(),
        "architecture": platform.machine(),
        "cpu_model": cpu_model(proc_root),
        "logical_cpu_count": os.cpu_count(),
        "security": {
            "perf_event_paranoid": _read_optional(proc_root / "sys/kernel/perf_event_paranoid"),
            "kptr_restrict": _read_optional(proc_root / "sys/kernel/kptr_restrict"),
        },
    }


def _write_evidence(
        output_directory: Path,
        manifest: dict[str, object],
        rows: Sequence[dict[str, object]],
) -> None:
    manifest_path = output_directory / "perf_manifest_v2.json"
    counters_path = output_directory / "perf_counters_v1.csv"
    with manifest_path.open("x", encoding="utf-8") as stream:
        json.dump(manifest, stream, indent=2, sort_keys=True)
        stream.write("\n")
    with counters_path.open("x", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=COUNTER_COLUMNS, extrasaction="raise")
        writer.writeheader()
        writer.writerows(rows)


# Collection

def _run_sessions(
        sessions: Sequence[Session],
        duration_seconds: float,
        unix_ns: Callable[[], int],
        monotonic: Callable[[], float],
) -> bool:
    launched: list[subprocess.Popen[str]] = []
    interrupted = False
    try:
        for session in sessions:
            if not session.invocation or session.target.process_start_ticks is None:
                if session.target.process_start_ticks is None:
                    session.errors.append("target exited before perf attachment")
                continue

            session.started_at_unix_ns = unix_ns()
            try:
                session.process = subprocess.Popen(
                    session.invocation,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE,
                    text=True,
                    env={**os.environ, "LC_ALL": "C"},
                )
                launched.append(session.process)
            except OSError as error:
                session.errors.append(f"perf launch failed: {error}")
                session.ended_at_unix_ns = unix_ns()

        launch_failed = any(
            session.invocation
            and session.target.process_start_ticks is not None
            and session.process is None
            for session in sessions
        )
        if launch_failed:
            stop_children(launched)
            for session in sessions:
                if session.process is not None:
                    session.return_code = session.process.poll()
                    session.ended_at_unix_ns = unix_ns()
                    session.errors.append("perf collection canceled after a launch failure")
            return False

        deadline = monotonic() + duration_seconds + 5.0
        for session in sessions:
            if session.process is None:
                continue
            try:
                _stdout, session.stderr = session.process.communicate(
                    timeout=max(0.001, deadline - monotonic())
                )
                session.return_code = session.process.returncode
            except subprocess.TimeoutExpired:
                session.errors.append("perf exceeded its bounded timeout")
                stop_children([session.process])
                _stdout, session.stderr = session.process.communicate()
                session.return_code = session.process.returncode
            session.ended_at_unix_ns = unix_ns()
    except KeyboardInterrupt:
        interrupted = True
        stop_children(launched)
        for session in sessions:
            if session.process is not None and session.ended_at_unix_ns == 0:
                session.ended_at_unix_ns = unix_ns()
                session.return_code = session.process.poll()
    finally:
        stop_children(launched)

    return interrupted


def collect(
        output_directory: Path,
        run_id: str,
        target_specs: Sequence[tuple[str, int]],
        profile: str,
        duration_seconds: float,
        *,
        proc_root: Path = Path("/proc"),
        unix_ns: Callable[[], int] = time.time_ns,
        monotonic: Callable[[], float] = time.monotonic,
        identity_reader: Callable[[int], int | None] | None = None,
) -> int:
    validate_run_id(run_id)
    if profile not in PROFILES:
        raise ValueError(f"unknown profile: {profile}")
    if duration_seconds <= 0:
        raise ValueError("duration must be positive")
    if not target_specs:
        raise ValueError("at least one target is required")

    roles = [role for role, _pid in target_specs]
    if any(ROLE_PATTERN.fullmatch(role) is None for role in roles):
        raise ValueError("invalid target role")
    if len(set(roles)) != len(roles):
        raise ValueError("target roles must be unique")

    _prepare_output_directory(output_directory)

    read_identity = identity_reader or (lambda pid: read_process_start_ticks(pid, proc_root))
    event_groups = PROFILES[profile]
    requested_events = [event for group in event_groups for event in group]

    perf_binary, perf_error = resolve_perf_binary()
    version = None
    version_error = None
    if perf_binary is not None:
        version, version_error = perf_version(perf_binary)

    targets = [Target(role, pid, read_identity(pid)) for role, pid in target_specs]
    sessions = [
        Session(
            target=target,
            invocation=(
                build_perf_invocation(perf_binary, target, event_groups, duration_seconds)
                if perf_binary is not None
                else []
            ),
            started_at_unix_ns=0,
        )
        for target in targets
    ]

    interrupted = _run_sessions(sessions, duration_seconds, unix_ns, monotonic)

    for session in sessions:
        session.final_process_start_ticks = read_identity(session.target.pid)

        if perf_binary is None:
            session.observations = _fill_missing_observations(
                [], event_groups, "perf_unavailable", perf_error or "perf is unavailable"
            )
            continue
        if session.target.process_start_ticks is None:
            session.observations = _fill_missing_observations(
                [], event_groups, "target_exited", "target exited before perf attachment"
            )
            continue
        if session.process is None:
            detail = "; ".join(session.errors) or "perf failed before collection"
            session.observations = _fill_missing_observations(
                [], event_groups, "perf_failed", detail
            )
            continue

        parsed, diagnostics = parse_perf_output(session.stderr, event_groups)
        session.errors.extend(diagnostics)
        default_status, default_detail = _default_failure_status(
            session.stderr, session.return_code
        )
        if diagnostics and not parsed and default_status == "malformed_output":
            default_detail = "; ".join(diagnostics)

        session.observations = _fill_missing_observations(
            parsed, event_groups, default_status, default_detail
        )
        if session.return_code not in (None, 0):
            session.errors.append(default_detail)

        # Reject counters if the PID no longer identifies the original process.
        if session.final_process_start_ticks is None:
            _override_observations(
                session.observations, "target_exited", "target exited during perf collection"
            )
        elif session.final_process_start_ticks != session.target.process_start_ticks:
            _override_observations(
                session.observations, "target_reused", "PID start ticks changed during collection"
            )
        elif session.return_code not in (None, 0) and default_status in (
                "permission_denied",
                "perf_failed",
        ):
            _override_observations(session.observations, default_status, default_detail)

    rows: list[dict[str, object]] = []
    record_order = 0
    for session in sessions:
        recorded_at = session.ended_at_unix_ns or unix_ns()
        for observation in session.observations:
            rows.append(
                {
                    "schema_version": COUNTER_SCHEMA_VERSION,
                    "run_id": run_id,
                    "recorded_at_unix_ns": recorded_at,
                    "record_order": record_order,
                    "role": session.target.role,
                    "pid": session.target.pid,
                    "process_start_ticks": (
                        session.target.process_start_ticks
                        if session.target.process_start_ticks is not None
                        else ""
                    ),
                    "profile": profile,
                    "event_group": observation.group_index,
                    "event": observation.event,
                    "counter_status": observation.status,
                    "value": observation.value,
                    "unit": observation.unit,
                    "counter_runtime_ns": observation.counter_runtime_ns,
                    "running_percentage": observation.running_percentage,
                    "perf_scaled_value": observation.perf_scaled_value,
                    "metric_value": observation.metric_value,
                    "metric_unit": observation.metric_unit,
                    "accepted_for_analysis": "1" if observation.accepted else "0",
                    "error_detail": observation.error_detail,
                }
            )
            record_order += 1

    accepted_count = sum(row["accepted_for_analysis"] == "1" for row in rows)
    complete = bool(rows) and all(row["accepted_for_analysis"] == "1" for row in rows)
    manifest = {
        "schema_version": MANIFEST_SCHEMA_VERSION,
        "tool": {"name": "simnet_perf_stat", "version": TOOL_VERSION},
        "run_id": run_id,
        "collector_command_line": list(sys.argv),
        "perf": {
            "resolved_binary": perf_binary,
            "version": version,
            "available": perf_binary is not None and version is not None,
            "error": perf_error or version_error,
        },
        **_host_manifest(proc_root),
        "profile": {
            "name": profile,
            "event_groups": [list(group) for group in event_groups],
            "requested_events": requested_events,
            "complete": complete,
        },
        "duration_seconds": duration_seconds,
        "minimum_running_percentage": MINIMUM_RUNNING_PERCENTAGE,
        "targets": [
            {
                "role": session.target.role,
                "pid": session.target.pid,
                "process_start_ticks": session.target.process_start_ticks,
                "final_process_start_ticks": session.final_process_start_ticks,
                "target_identity_status": target_identity_status(session),
                "started_at_unix_ns": session.started_at_unix_ns or None,
                "ended_at_unix_ns": session.ended_at_unix_ns or None,
                "perf_exit_status": session.return_code,
                "invocation": session.invocation,
                "errors": session.errors,
                "counter_statuses": [
                    {"event": item.event, "status": item.status}
                    for item in session.observations
                ],
            }
            for session in sessions
        ],
        "errors": [error for error in (perf_error, version_error) if error],
        "measurement_notes": {
            "attribution": "Each perf process measures one complete attached process",
            "sampling": "Counters are run-level totals, not interval samples or SimNet stages",
            "multiplexing": (
                "Separate matched profile runs limit multiplexing; counters below the "
                "running threshold are rejected for analysis"
            ),
            "ratio_pairs": (
                "Ratios are suitable only when both accepted source counters share an event group"
            ),
            "joining": "Join by run_id, role, PID identity, and the recorded target interval",
        },
    }
    _write_evidence(output_directory, manifest, rows)

    if interrupted:
        return 130
    return 0 if accepted_count else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--target", action="append", type=parse_target, required=True)
    parser.add_argument("--profile", choices=sorted(PROFILES), required=True)
    parser.add_argument("--duration-seconds", type=float, required=True)
    return parser


def main() -> int:
    arguments = build_parser().parse_args()
    previous_sigterm = signal.signal(signal.SIGTERM, termination_signal_handler)
    try:
        return collect(
            arguments.output_directory,
            arguments.run_id,
            arguments.target,
            arguments.profile,
            arguments.duration_seconds,
        )
    except (FileExistsError, OSError, ValueError) as error:
        print(f"simnet_perf_stat: {error}", file=sys.stderr)
        return 2
    finally:
        signal.signal(signal.SIGTERM, previous_sigterm)


if __name__ == "__main__":
    raise SystemExit(main())