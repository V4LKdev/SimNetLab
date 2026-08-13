#!/usr/bin/env python3
"""Collect capability-aware Linux process and host evidence for SimNet runs."""

from __future__ import annotations

import argparse
import csv
import json
import os
import platform
import signal
import socket
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable


COLLECTOR_VERSION = "1.0.0"
PROCESS_SCHEMA_VERSION = 1
HOST_SCHEMA_VERSION = 1

PROCESS_COLUMNS = [
    "schema_version",
    "run_id",
    "recorded_at_unix_ns",
    "elapsed_since_collector_start_ns",
    "record_order",
    "role",
    "pid",
    "process_start_time_ticks",
    "sample_status",
    "errors_json",
    "current_processor",
    "thread_count",
    "user_cpu_ticks",
    "system_cpu_ticks",
    "minor_page_faults",
    "major_page_faults",
    "vm_rss_bytes",
    "vm_hwm_bytes",
    "rss_anon_bytes",
    "rss_file_bytes",
    "rss_shared_bytes",
    "vm_size_bytes",
    "swap_bytes",
    "voluntary_context_switches",
    "involuntary_context_switches",
    "scheduler_runtime_ns",
    "scheduler_wait_ns",
    "scheduler_timeslices",
    "read_syscalls",
    "write_syscalls",
    "read_bytes",
    "write_bytes",
    "smaps_rollup_attempted",
    "smaps_rss_bytes",
    "smaps_pss_bytes",
    "smaps_pss_anon_bytes",
    "smaps_pss_file_bytes",
    "smaps_pss_shared_bytes",
    "smaps_shared_clean_bytes",
    "smaps_shared_dirty_bytes",
    "smaps_private_clean_bytes",
    "smaps_private_dirty_bytes",
    "smaps_anonymous_bytes",
    "smaps_swap_bytes",
]

HOST_COLUMNS = [
    "schema_version",
    "run_id",
    "recorded_at_unix_ns",
    "elapsed_since_collector_start_ns",
    "record_order",
    "sample_status",
    "errors_json",
    "cpu_psi_some_avg10",
    "cpu_psi_some_avg60",
    "cpu_psi_some_avg300",
    "cpu_psi_some_total_us",
    "memory_psi_some_avg10",
    "memory_psi_some_avg60",
    "memory_psi_some_avg300",
    "memory_psi_some_total_us",
    "memory_psi_full_avg10",
    "memory_psi_full_avg60",
    "memory_psi_full_avg300",
    "memory_psi_full_total_us",
    "io_psi_some_avg10",
    "io_psi_some_avg60",
    "io_psi_some_avg300",
    "io_psi_some_total_us",
    "io_psi_full_avg10",
    "io_psi_full_avg60",
    "io_psi_full_avg300",
    "io_psi_full_total_us",
    "memory_total_bytes",
    "memory_available_bytes",
    "swap_total_bytes",
    "swap_free_bytes",
    "load_average_1m",
    "load_average_5m",
    "load_average_15m",
    "network_interface",
    "network_rx_bytes",
    "network_rx_packets",
    "network_rx_drops",
    "network_rx_errors",
    "network_tx_bytes",
    "network_tx_packets",
    "network_tx_drops",
    "network_tx_errors",
    "cpu_frequency_values_json",
    "temperature_values_json",
    "powercap_energy_values_json",
]


class UnavailableError(RuntimeError):
    """A metric source is absent or unreadable and must not be represented as zero."""


def _read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError as error:
        raise UnavailableError(f"missing metric source: {path}") from error
    except PermissionError as error:
        raise UnavailableError(
            f"permission denied reading {path}. Run as the target process owner or adjust access"
        ) from error
    except OSError as error:
        raise UnavailableError(f"cannot read {path}: {error.strerror or error}") from error


def _kilobytes_to_bytes(value: str) -> int:
    parts = value.split()
    if len(parts) != 2 or parts[1] != "kB":
        raise ValueError(f"expected a kB value, got {value!r}")
    return int(parts[0]) * 1024


def parse_key_values(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in text.splitlines():
        key, separator, value = line.partition(":")
        if separator:
            values[key] = value.strip()
    return values


def parse_proc_stat(text: str) -> dict[str, int]:
    closing = text.rfind(")")
    if closing < 0:
        raise ValueError("process stat has no closing command delimiter")
    fields = text[closing + 2 :].split()
    if len(fields) < 37:
        raise ValueError("process stat is truncated")
    return {
        "minor_page_faults": int(fields[7]),
        "major_page_faults": int(fields[9]),
        "user_cpu_ticks": int(fields[11]),
        "system_cpu_ticks": int(fields[12]),
        "thread_count": int(fields[17]),
        "process_start_time_ticks": int(fields[19]),
        "current_processor": int(fields[36]),
    }


def parse_proc_status(text: str) -> dict[str, int]:
    values = parse_key_values(text)
    output: dict[str, int] = {}
    byte_fields = {
        "VmRSS": "vm_rss_bytes",
        "VmHWM": "vm_hwm_bytes",
        "RssAnon": "rss_anon_bytes",
        "RssFile": "rss_file_bytes",
        "RssShmem": "rss_shared_bytes",
        "VmSize": "vm_size_bytes",
        "VmSwap": "swap_bytes",
    }
    integer_fields = {
        "Threads": "thread_count",
        "voluntary_ctxt_switches": "voluntary_context_switches",
        "nonvoluntary_ctxt_switches": "involuntary_context_switches",
    }
    for source, destination in byte_fields.items():
        if source in values:
            output[destination] = _kilobytes_to_bytes(values[source])
    for source, destination in integer_fields.items():
        if source in values:
            output[destination] = int(values[source])
    return output


def parse_proc_io(text: str) -> dict[str, int]:
    values = parse_key_values(text)
    mapping = {
        "syscr": "read_syscalls",
        "syscw": "write_syscalls",
        "read_bytes": "read_bytes",
        "write_bytes": "write_bytes",
    }
    return {
        destination: int(values[source])
        for source, destination in mapping.items()
        if source in values
    }


def parse_schedstat(text: str) -> dict[str, int]:
    lines = text.splitlines()
    fields = lines[0].split() if lines else []
    if len(fields) < 3:
        raise ValueError("schedstat is truncated")
    return {
        "scheduler_runtime_ns": int(fields[0]),
        "scheduler_wait_ns": int(fields[1]),
        "scheduler_timeslices": int(fields[2]),
    }


def parse_smaps_rollup(text: str) -> dict[str, int]:
    values = parse_key_values(text)
    mapping = {
        "Rss": "smaps_rss_bytes",
        "Pss": "smaps_pss_bytes",
        "Pss_Anon": "smaps_pss_anon_bytes",
        "Pss_File": "smaps_pss_file_bytes",
        "Pss_Shmem": "smaps_pss_shared_bytes",
        "Shared_Clean": "smaps_shared_clean_bytes",
        "Shared_Dirty": "smaps_shared_dirty_bytes",
        "Private_Clean": "smaps_private_clean_bytes",
        "Private_Dirty": "smaps_private_dirty_bytes",
        "Anonymous": "smaps_anonymous_bytes",
        "Swap": "smaps_swap_bytes",
    }
    return {
        destination: _kilobytes_to_bytes(values[source])
        for source, destination in mapping.items()
        if source in values
    }


def parse_psi(text: str, resource: str) -> dict[str, float | int]:
    output: dict[str, float | int] = {}
    for line in text.splitlines():
        fields = line.split()
        if not fields:
            continue
        category = fields[0]
        for item in fields[1:]:
            key, separator, value = item.partition("=")
            if not separator:
                continue
            destination = f"{resource}_psi_{category}_{key}{'_us' if key == 'total' else ''}"
            output[destination] = int(value) if key == "total" else float(value)
    return output


def parse_meminfo(text: str) -> dict[str, int]:
    values = parse_key_values(text)
    mapping = {
        "MemTotal": "memory_total_bytes",
        "MemAvailable": "memory_available_bytes",
        "SwapTotal": "swap_total_bytes",
        "SwapFree": "swap_free_bytes",
    }
    return {
        destination: _kilobytes_to_bytes(values[source])
        for source, destination in mapping.items()
        if source in values
    }


def parse_loadavg(text: str) -> dict[str, float]:
    fields = text.split()
    if len(fields) < 3:
        raise ValueError("loadavg is truncated")
    return {
        "load_average_1m": float(fields[0]),
        "load_average_5m": float(fields[1]),
        "load_average_15m": float(fields[2]),
    }


def parse_net_dev(text: str, interface: str) -> dict[str, int]:
    for line in text.splitlines()[2:]:
        name, separator, counters = line.partition(":")
        if separator and name.strip() == interface:
            fields = counters.split()
            if len(fields) < 16:
                raise ValueError("network interface counters are truncated")
            return {
                "network_rx_bytes": int(fields[0]),
                "network_rx_packets": int(fields[1]),
                "network_rx_errors": int(fields[2]),
                "network_rx_drops": int(fields[3]),
                "network_tx_bytes": int(fields[8]),
                "network_tx_packets": int(fields[9]),
                "network_tx_errors": int(fields[10]),
                "network_tx_drops": int(fields[11]),
            }
    raise UnavailableError(f"network interface {interface!r} is absent from /proc/net/dev")


@dataclass(frozen=True)
class Target:
    role: str
    pid: int
    start_time_ticks: int
    command_line: list[str]
    command_line_error: str | None = None


@dataclass(frozen=True)
class Sensor:
    key: str
    path: Path
    unit: str
    metadata: dict[str, object]


class LinuxSource:
    def __init__(self, proc_root: Path = Path("/proc"), sys_root: Path = Path("/sys")) -> None:
        self.proc_root = proc_root
        self.sys_root = sys_root
        self.frequency_sensors = self._discover_frequency_sensors()
        self.temperature_sensors = self._discover_temperature_sensors()
        self.powercap_sensors = self._discover_powercap_sensors()

    def _discover_frequency_sensors(self) -> list[Sensor]:
        base = self.sys_root / "devices/system/cpu/cpufreq"
        sensors: list[Sensor] = []
        for path in sorted(base.glob("policy*/scaling_cur_freq")):
            metadata: dict[str, object] = {
                "policy": path.parent.name,
                "path": str(path),
                "unit": "kilohertz",
            }
            for name in (
                "scaling_governor",
                "scaling_min_freq",
                "scaling_max_freq",
                "cpuinfo_min_freq",
                "cpuinfo_max_freq",
            ):
                try:
                    metadata[name] = _read_text(path.parent / name).strip()
                except UnavailableError:
                    metadata[name] = None
            sensors.append(
                Sensor(
                    key=path.parent.name,
                    path=path,
                    unit="kilohertz",
                    metadata=metadata,
                )
            )
        return sensors

    def _discover_temperature_sensors(self) -> list[Sensor]:
        sensors: list[Sensor] = []
        for path in sorted((self.sys_root / "class/hwmon").glob("hwmon*/temp*_input")):
            prefix = path.name.removesuffix("_input")
            device = path.parent.name
            try:
                device = _read_text(path.parent / "name").strip() or device
            except UnavailableError:
                pass
            try:
                label = _read_text(path.parent / f"{prefix}_label").strip()
            except UnavailableError:
                label = prefix
            key = f"{path.parent.name}/{prefix}"
            sensors.append(
                Sensor(
                    key=key,
                    path=path,
                    unit="millidegree_celsius",
                    metadata={
                        "key": key,
                        "device": device,
                        "label": label,
                        "path": str(path),
                        "unit": "millidegree_celsius",
                    },
                )
            )
        return sensors

    def _discover_powercap_sensors(self) -> list[Sensor]:
        sensors: list[Sensor] = []
        for path in sorted((self.sys_root / "class/powercap").glob("**/energy_uj")):
            try:
                name = _read_text(path.parent / "name").strip()
            except UnavailableError:
                name = path.parent.name
            try:
                maximum = int(_read_text(path.parent / "max_energy_range_uj").strip())
            except (UnavailableError, ValueError):
                maximum = None
            key = str(path.parent.relative_to(self.sys_root / "class/powercap"))
            sensors.append(
                Sensor(
                    key=key,
                    path=path,
                    unit="microjoule",
                    metadata={
                        "key": key,
                        "name": name,
                        "path": str(path),
                        "unit": "microjoule",
                        "max_energy_range_microjoule": maximum,
                        "wraps_at_max_energy_range": maximum is not None,
                    },
                )
            )
        return sensors

    def inspect_target(self, role: str, pid: int) -> Target:
        stat = parse_proc_stat(_read_text(self.proc_root / str(pid) / "stat"))
        try:
            command_line = (
                _read_text(self.proc_root / str(pid) / "cmdline").rstrip("\0").split("\0")
            )
            command_line_error = None
        except UnavailableError as error:
            command_line = []
            command_line_error = str(error)
        return Target(
            role,
            pid,
            stat["process_start_time_ticks"],
            command_line,
            command_line_error,
        )

    def target_capabilities(self, target: Target) -> dict[str, object]:
        capabilities: dict[str, object] = {}
        for name in ("stat", "status", "io", "schedstat", "smaps_rollup"):
            path = self.proc_root / str(target.pid) / name
            try:
                _read_text(path)
                capabilities[name] = {"available": True, "path": str(path)}
            except UnavailableError as error:
                capabilities[name] = {
                    "available": False,
                    "path": str(path),
                    "reason": str(error),
                }
        return capabilities

    def sample_process(
        self, target: Target, include_smaps: bool
    ) -> tuple[dict[str, object], list[str], bool]:
        base = self.proc_root / str(target.pid)
        errors: list[str] = []
        values: dict[str, object] = {
            "smaps_rollup_attempted": 1 if include_smaps else 0,
        }
        try:
            stat = parse_proc_stat(_read_text(base / "stat"))
        except UnavailableError as error:
            return values, [str(error)], False
        except ValueError as error:
            return values, [f"invalid {base / 'stat'}: {error}"], False
        if stat["process_start_time_ticks"] != target.start_time_ticks:
            return values, ["PID reuse detected from a changed process start time"], False
        values.update(stat)
        readers: list[tuple[str, Callable[[str], dict[str, int]]]] = [
            ("status", parse_proc_status),
            ("io", parse_proc_io),
            ("schedstat", parse_schedstat),
        ]
        if include_smaps:
            readers.append(("smaps_rollup", parse_smaps_rollup))
        for name, parser in readers:
            path = base / name
            try:
                values.update(parser(_read_text(path)))
            except UnavailableError as error:
                errors.append(str(error))
            except ValueError as error:
                errors.append(f"invalid {path}: {error}")
        return values, errors, True

    def _read_sensors(self, sensors: Iterable[Sensor]) -> tuple[dict[str, object], list[str]]:
        values: dict[str, object] = {}
        errors: list[str] = []
        for sensor in sensors:
            try:
                values[sensor.key] = {
                    "value": int(_read_text(sensor.path).strip()),
                    "unit": sensor.unit,
                }
            except UnavailableError as error:
                errors.append(str(error))
            except ValueError as error:
                errors.append(f"invalid {sensor.path}: {error}")
        return values, errors

    def sample_host(self, interface: str | None) -> tuple[dict[str, object], list[str]]:
        values: dict[str, object] = {}
        errors: list[str] = []
        sources: list[tuple[Path, Callable[[str], dict[str, object]]]] = [
            (self.proc_root / "pressure/cpu", lambda text: parse_psi(text, "cpu")),
            (self.proc_root / "pressure/memory", lambda text: parse_psi(text, "memory")),
            (self.proc_root / "pressure/io", lambda text: parse_psi(text, "io")),
            (self.proc_root / "meminfo", parse_meminfo),
            (self.proc_root / "loadavg", parse_loadavg),
        ]
        for path, parser in sources:
            try:
                values.update(parser(_read_text(path)))
            except UnavailableError as error:
                errors.append(str(error))
            except ValueError as error:
                errors.append(f"invalid {path}: {error}")
        if interface is not None:
            values["network_interface"] = interface
            try:
                values.update(parse_net_dev(_read_text(self.proc_root / "net/dev"), interface))
            except (UnavailableError, ValueError) as error:
                errors.append(str(error))
        frequencies, frequency_errors = self._read_sensors(self.frequency_sensors)
        temperatures, temperature_errors = self._read_sensors(self.temperature_sensors)
        energy, energy_errors = self._read_sensors(self.powercap_sensors)
        values["cpu_frequency_values_json"] = json.dumps(
            frequencies, sort_keys=True, separators=(",", ":")
        )
        values["temperature_values_json"] = json.dumps(
            temperatures, sort_keys=True, separators=(",", ":")
        )
        values["powercap_energy_values_json"] = json.dumps(
            energy, sort_keys=True, separators=(",", ":")
        )
        errors.extend(frequency_errors + temperature_errors + energy_errors)
        return values, errors

    def capability_manifest(self, interface: str | None) -> dict[str, object]:
        def path_capability(path: Path) -> dict[str, object]:
            try:
                _read_text(path)
                return {"available": True, "path": str(path)}
            except UnavailableError as error:
                return {"available": False, "path": str(path), "reason": str(error)}

        return {
            "process": {
                "stat": {"available": True, "source": "/proc/ROLE_PID/stat"},
                "status": {"available": True, "source": "/proc/ROLE_PID/status"},
                "io": {
                    "available": True,
                    "source": "/proc/ROLE_PID/io",
                    "permission_may_vary": True,
                },
                "schedstat": {"available": True, "source": "/proc/ROLE_PID/schedstat"},
                "smaps_rollup": {
                    "available": True,
                    "source": "/proc/ROLE_PID/smaps_rollup",
                    "permission_may_vary": True,
                },
            },
            "host": {
                "cpu_pressure": path_capability(self.proc_root / "pressure/cpu"),
                "memory_pressure": path_capability(self.proc_root / "pressure/memory"),
                "io_pressure": path_capability(self.proc_root / "pressure/io"),
                "memory": path_capability(self.proc_root / "meminfo"),
                "load": path_capability(self.proc_root / "loadavg"),
                "network_interface": {
                    "available": interface is not None,
                    "interface": interface,
                    "reason": (
                        None if interface is not None else "no network interface was requested"
                    ),
                },
                "cpu_frequency": {
                    "available": bool(self.frequency_sensors),
                    "sources": [sensor.metadata for sensor in self.frequency_sensors],
                    "reason": (
                        None
                        if self.frequency_sensors
                        else "no cpufreq policy sources discovered"
                    ),
                },
                "temperature": {
                    "available": bool(self.temperature_sensors),
                    "sources": [sensor.metadata for sensor in self.temperature_sensors],
                    "reason": (
                        None
                        if self.temperature_sensors
                        else "no readable hwmon temperature sources discovered"
                    ),
                },
                "powercap_energy": {
                    "available": bool(self.powercap_sensors),
                    "sources": [sensor.metadata for sensor in self.powercap_sensors],
                    "reason": (
                        None
                        if self.powercap_sensors
                        else "no readable powercap energy sources discovered"
                    ),
                },
            },
        }


def _cpu_model(proc_root: Path) -> str | None:
    try:
        values = parse_key_values(_read_text(proc_root / "cpuinfo"))
    except UnavailableError:
        return None
    return values.get("model name") or values.get("Hardware") or values.get("Processor")


def _optional_text(path: Path) -> str | None:
    try:
        return _read_text(path).strip() or None
    except UnavailableError:
        return None


def _write_manifest(
    path: Path,
    run_id: str,
    targets: list[Target],
    source: LinuxSource,
    interface: str | None,
    interval_seconds: float,
    smaps_every: int,
    started_unix_ns: int,
) -> None:
    manifest = {
        "schema_version": 1,
        "collector": {"name": "simnet_linux_collector", "version": COLLECTOR_VERSION},
        "collector_command_line": sys.argv,
        "run_id": run_id,
        "collection_started_unix_ns": started_unix_ns,
        "sampling_interval_seconds": interval_seconds,
        "smaps_rollup_every_samples": smaps_every,
        "kernel": platform.release(),
        "boot_id": _optional_text(source.proc_root / "sys/kernel/random/boot_id"),
        "hostname": socket.gethostname(),
        "architecture": platform.machine(),
        "cpu_model": _cpu_model(source.proc_root),
        "logical_cpu_count": os.cpu_count(),
        "clock_ticks_per_second": os.sysconf("SC_CLK_TCK"),
        "targets": [
            {
                "role": target.role,
                "pid": target.pid,
                "process_start_time_ticks": target.start_time_ticks,
                "command_line": target.command_line,
                "command_line_error": target.command_line_error,
                "capabilities": source.target_capabilities(target),
            }
            for target in targets
        ],
        "capabilities": source.capability_manifest(interface),
        "measurement_notes": {
            "timestamps": (
                "Unix nanoseconds join files. Monotonic elapsed nanoseconds order samples"
            ),
            "environmental_covariates": ["host_temperature", "powercap_energy", "pressure_stall"],
            "attribution": (
                "Environmental covariates are host-level and are not process-attributable"
            ),
            "unavailable_values": (
                "Empty CSV cells are unavailable. Consult errors_json and capabilities"
            ),
        },
    }
    with path.open("x", encoding="utf-8", newline="") as output:
        json.dump(manifest, output, indent=2, sort_keys=True)
        output.write("\n")


def collect(
    output_directory: Path,
    run_id: str,
    target_specs: list[tuple[str, int]],
    interval_seconds: float,
    interface: str | None,
    duration_seconds: float | None,
    smaps_every: int,
    source: LinuxSource | None = None,
    unix_ns: Callable[[], int] = time.time_ns,
    monotonic_ns: Callable[[], int] = time.monotonic_ns,
    sleep: Callable[[float], None] = time.sleep,
) -> int:
    if not run_id or len(run_id) > 64 or not run_id[0].isalnum() or any(
        not (character.isascii() and (character.isalnum() or character in "._-"))
        for character in run_id
    ):
        raise ValueError("run ID must match the SimNet evidence token contract")
    if interval_seconds <= 0:
        raise ValueError("sampling interval must be positive")
    if smaps_every <= 0:
        raise ValueError("smaps interval must be positive")
    if duration_seconds is not None and duration_seconds < 0:
        raise ValueError("duration must be nonnegative")
    roles = [role for role, _ in target_specs]
    if any(
        not role or not role.isascii() or not role.replace("_", "").isalnum()
        for role in roles
    ):
        raise ValueError("target roles must contain only letters, digits, and underscores")
    if len(set(roles)) != len(roles):
        raise ValueError("target roles must be unique")
    source = source or LinuxSource()
    targets = [source.inspect_target(role, pid) for role, pid in target_specs]
    output_directory.mkdir(parents=True, exist_ok=True)
    paths = {
        "manifest": output_directory / "run_manifest_v1.json",
        "process": output_directory / "process_samples_v1.csv",
        "host": output_directory / "host_samples_v1.csv",
    }
    collisions = [str(path) for path in paths.values() if path.exists()]
    if collisions:
        raise FileExistsError("collector output already exists: " + ", ".join(collisions))

    started_unix_ns = unix_ns()
    started_monotonic_ns = monotonic_ns()
    _write_manifest(
        paths["manifest"],
        run_id,
        targets,
        source,
        interface,
        interval_seconds,
        smaps_every,
        started_unix_ns,
    )
    stopping = False

    def request_stop(_signum: int, _frame: object) -> None:
        nonlocal stopping
        stopping = True

    previous_handlers: dict[int, object] = {}
    if source.proc_root == Path("/proc"):
        for signum in (signal.SIGINT, signal.SIGTERM):
            previous_handlers[signum] = signal.signal(signum, request_stop)
    active = {target.role: target for target in targets}
    process_order = 0
    host_order = 0
    sample_index = 0
    try:
        with paths["process"].open("x", encoding="utf-8", newline="") as process_file, paths[
            "host"
        ].open("x", encoding="utf-8", newline="") as host_file:
            process_writer = csv.DictWriter(
                process_file, fieldnames=PROCESS_COLUMNS, extrasaction="ignore"
            )
            host_writer = csv.DictWriter(host_file, fieldnames=HOST_COLUMNS, extrasaction="ignore")
            process_writer.writeheader()
            host_writer.writeheader()
            while not stopping:
                recorded_unix_ns = unix_ns()
                elapsed_ns = monotonic_ns() - started_monotonic_ns
                for role, target in list(active.items()):
                    values, errors, alive = source.sample_process(
                        target,
                        include_smaps=sample_index % smaps_every == 0,
                    )
                    status = "sampled" if alive else (
                        "pid_reused" if any("PID reuse" in error for error in errors) else "exited"
                    )
                    row: dict[str, object] = {
                        "schema_version": PROCESS_SCHEMA_VERSION,
                        "run_id": run_id,
                        "recorded_at_unix_ns": recorded_unix_ns,
                        "elapsed_since_collector_start_ns": elapsed_ns,
                        "record_order": process_order,
                        "role": role,
                        "pid": target.pid,
                        "process_start_time_ticks": target.start_time_ticks,
                        "sample_status": status,
                        "errors_json": json.dumps(errors, separators=(",", ":")),
                    }
                    row.update(values)
                    process_writer.writerow(row)
                    process_order += 1
                    if not alive:
                        del active[role]
                host_values, host_errors = source.sample_host(interface)
                host_row: dict[str, object] = {
                    "schema_version": HOST_SCHEMA_VERSION,
                    "run_id": run_id,
                    "recorded_at_unix_ns": recorded_unix_ns,
                    "elapsed_since_collector_start_ns": elapsed_ns,
                    "record_order": host_order,
                    "sample_status": "sampled" if not host_errors else "partial",
                    "errors_json": json.dumps(host_errors, separators=(",", ":")),
                }
                host_row.update(host_values)
                host_writer.writerow(host_row)
                host_order += 1
                process_file.flush()
                host_file.flush()
                sample_index += 1
                if not active:
                    break
                if duration_seconds is not None and elapsed_ns >= int(
                    duration_seconds * 1_000_000_000
                ):
                    break
                sleep(interval_seconds)
    finally:
        for signum, handler in previous_handlers.items():
            signal.signal(signum, handler)
    return 0


def parse_target(value: str) -> tuple[str, int]:
    role, separator, pid_text = value.partition(":")
    if (
        not separator
        or not role
        or not role.isascii()
        or not role.replace("_", "").isalnum()
    ):
        raise argparse.ArgumentTypeError("target must be ROLE:PID")
    try:
        pid = int(pid_text)
    except ValueError as error:
        raise argparse.ArgumentTypeError("target PID must be an integer") from error
    if pid <= 0:
        raise argparse.ArgumentTypeError("target PID must be positive")
    return role.lower(), pid


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--target", required=True, action="append", type=parse_target)
    parser.add_argument("--interval-ms", type=float, default=100.0)
    parser.add_argument("--network-interface")
    parser.add_argument("--duration-seconds", type=float)
    parser.add_argument("--smaps-every", type=int, default=10)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        return collect(
            output_directory=arguments.output_directory,
            run_id=arguments.run_id,
            target_specs=arguments.target,
            interval_seconds=arguments.interval_ms / 1000.0,
            interface=arguments.network_interface,
            duration_seconds=arguments.duration_seconds,
            smaps_every=arguments.smaps_every,
        )
    except (FileExistsError, OSError, UnavailableError, ValueError) as error:
        print(f"collector error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
