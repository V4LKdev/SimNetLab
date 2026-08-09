#!/usr/bin/env python3
"""Deterministic contract tests for the external perf stat evidence collector."""

from __future__ import annotations

import csv
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "scripts/simnet_perf_stat.py"
sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("simnet_perf_stat", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
collector = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = collector
SPEC.loader.exec_module(collector)


FAKE_PERF = r'''#!/usr/bin/env python3
import os
import sys

if "--version" in sys.argv:
    print('perf version fixture, "quoted"')
    raise SystemExit(0)

events = []
for index, argument in enumerate(sys.argv):
    if argument == "-e":
        events.extend(sys.argv[index + 1].strip("{}").split(","))
mode = os.environ.get("SIMNET_FAKE_PERF_MODE", "measured")
if mode == "permission":
    print('No permission, "fixture denial" to enable events.', file=sys.stderr)
    raise SystemExit(255)
if mode == "malformed":
    print("this is not machine-readable perf output", file=sys.stderr)
    raise SystemExit(0)

for index, event in enumerate(events):
    if mode == "unsupported":
        value = "<not supported>"
        percentage = ""
    elif mode == "partial" and index != 0:
        value = "<not counted>"
        percentage = ""
    else:
        value = str(1000 + index)
        percentage = "80.00" if mode == "low" and index == 0 else "100.00"
    unit = "msec" if event == "task-clock" else "count"
    print(
        f"{value};{unit};{event};1000000;{percentage};2.5;fixture metric, quoted",
        file=sys.stderr,
    )
'''


def make_fake_perf(root: Path) -> Path:
    path = root / 'fake perf, "quoted".py'
    path.write_text(FAKE_PERF, encoding="utf-8")
    path.chmod(0o755)
    return path


def read_rows(output: Path) -> list[dict[str, str]]:
    with (output / "perf_counters_v1.csv").open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


class PerfStatTests(unittest.TestCase):
    def test_profiles_preserve_simultaneous_ratio_groups(self) -> None:
        target = collector.Target("server", 42, 777)
        invocation = collector.build_perf_invocation(
            "/usr/bin/perf", target, collector.PROFILES["core"], 1.25
        )
        groups = [invocation[index + 1] for index, value in enumerate(invocation) if value == "-e"]
        self.assertEqual(groups, ["{cycles,instructions}", "{branches,branch-misses}"])
        self.assertEqual(invocation[-4:], ["-p", "42", "--timeout", "1250"])
        self.assertNotIn("-t", invocation)
        self.assertEqual(
            collector.PROFILES["memory"],
            (
                ("instructions", "L1-dcache-loads", "L1-dcache-load-misses"),
                ("instructions", "LLC-loads", "LLC-load-misses"),
                ("instructions", "dTLB-loads", "dTLB-load-misses"),
                ("instructions", "iTLB-loads", "iTLB-load-misses"),
            ),
        )
        self.assertEqual(
            collector.PROFILES["cache"],
            (("instructions", "cache-references", "cache-misses"),),
        )
        self.assertEqual(
            collector.PROFILES["stalls"],
            (("cycles", "stalled-cycles-frontend", "stalled-cycles-backend"),),
        )

    def test_machine_output_preserves_values_units_metrics_and_running_time(self) -> None:
        output = (
            "2400;count;cycles;900000;100.00;2.4;GHz\n"
            "1200;count;instructions;700000;80.00;1.5;insn per cycle\n"
            "<not supported>;;branches;;;;\n"
            "<not counted>;;branch-misses;;;;\n"
        )
        observations, diagnostics = collector.parse_perf_output(
            output, collector.PROFILES["core"], 95.0
        )
        self.assertEqual(diagnostics, [])
        by_event = {item.event: item for item in observations}
        self.assertEqual(by_event["cycles"].status, "measured")
        self.assertEqual(by_event["cycles"].counter_runtime_ns, "900000")
        self.assertEqual(by_event["cycles"].running_percentage, "100.00")
        self.assertEqual(by_event["cycles"].perf_scaled_value, "0")
        self.assertEqual(by_event["cycles"].metric_value, "2.4")
        self.assertEqual(by_event["cycles"].metric_unit, "GHz")
        self.assertTrue(by_event["cycles"].accepted)
        self.assertEqual(by_event["instructions"].status, "insufficient_running_time")
        self.assertEqual(by_event["instructions"].perf_scaled_value, "1")
        self.assertFalse(by_event["instructions"].accepted)
        self.assertEqual(by_event["branches"].status, "unsupported")
        self.assertEqual(by_event["branch-misses"].status, "not_counted")

    def test_repeated_instructions_remain_attributed_to_each_memory_group(self) -> None:
        lines = []
        for group in collector.PROFILES["memory"]:
            for event in group:
                lines.append(f"100;count;{event};1000000;100.00;;")
        observations, diagnostics = collector.parse_perf_output(
            "\n".join(lines), collector.PROFILES["memory"], 95.0
        )
        self.assertEqual(diagnostics, [])
        self.assertEqual(len(observations), 12)
        self.assertEqual(
            [item.group_index for item in observations if item.event == "instructions"],
            [0, 1, 2, 3],
        )
        self.assertTrue(all(item.accepted for item in observations))

    def test_hybrid_pmu_rows_are_aggregated_without_losing_running_time(self) -> None:
        output = (
            "WARNING: events were regrouped to match PMUs\n"
            "100;;cpu_atom/cycles/u;30000000;3.00;;\n"
            "40;;cpu_atom/instructions/u;30000000;3.00;;\n"
            "900;;cpu_core/cycles/u;960000000;96.00;;\n"
            "360;;cpu_core/instructions/u;960000000;96.00;;\n"
            "20;;cpu_atom/branches/u;30000000;3.00;;\n"
            "2;;cpu_atom/branch-misses/u;30000000;3.00;;\n"
            "180;;cpu_core/branches/u;960000000;96.00;;\n"
            "18;;cpu_core/branch-misses/u;960000000;96.00;;\n"
        )
        observations, diagnostics = collector.parse_perf_output(
            output, collector.PROFILES["core"], 95.0
        )
        self.assertEqual(diagnostics, [])
        by_event = {item.event: item for item in observations}
        self.assertEqual(by_event["cycles"].value, "1000")
        self.assertEqual(by_event["instructions"].value, "400")
        self.assertEqual(by_event["branches"].value, "200")
        self.assertEqual(by_event["branch-misses"].value, "20")
        self.assertEqual(by_event["cycles"].counter_runtime_ns, "990000000")
        self.assertEqual(by_event["cycles"].running_percentage, "99.00")
        self.assertEqual(by_event["cycles"].perf_scaled_value, "1")
        self.assertTrue(all(item.accepted for item in observations))

    def test_multiple_targets_write_stable_joinable_and_escaped_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = make_fake_perf(root)
            output = root / "evidence"
            with mock.patch.dict(os.environ, {"SIMNET_FAKE_PERF_MODE": "measured"}):
                result = collector.collect(
                    output,
                    "fixture-run",
                    [("server", 42), ("client", 43)],
                    "core",
                    0.01,
                    str(fake),
                    95.0,
                    True,
                    proc_root=root / "proc",
                    identity_reader=lambda _pid: 777,
                )
            self.assertEqual(result, 0)
            manifest = json.loads((output / "perf_manifest_v1.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["schema_version"], 1)
            self.assertEqual(manifest["run_id"], "fixture-run")
            self.assertEqual(manifest["perf"]["version"], 'perf version fixture, "quoted"')
            self.assertEqual(len(manifest["targets"]), 2)
            self.assertTrue(
                all(target["started_at_unix_ns"] for target in manifest["targets"])
            )
            self.assertTrue(all(target["ended_at_unix_ns"] for target in manifest["targets"]))
            self.assertEqual(
                {target["target_identity_status"] for target in manifest["targets"]},
                {"stable"},
            )
            self.assertEqual(
                manifest["profile"]["event_groups"],
                [["cycles", "instructions"], ["branches", "branch-misses"]],
            )
            rows = read_rows(output)
            self.assertEqual(len(rows), 8)
            self.assertEqual([int(row["record_order"]) for row in rows], list(range(8)))
            self.assertEqual({row["role"] for row in rows}, {"server", "client"})
            self.assertEqual({row["run_id"] for row in rows}, {"fixture-run"})
            self.assertEqual({row["accepted_for_analysis"] for row in rows}, {"1"})
            self.assertTrue(all(row["metric_unit"] == "fixture metric, quoted" for row in rows))
            with (output / "perf_counters_v1.csv").open(newline="", encoding="utf-8") as stream:
                self.assertEqual(next(csv.reader(stream)), collector.COUNTER_COLUMNS)
            with self.assertRaises(FileExistsError):
                collector.collect(
                    output,
                    "fixture-run",
                    [("server", 42)],
                    "software",
                    0.01,
                    str(fake),
                    95.0,
                    False,
                    identity_reader=lambda _pid: 777,
                )

    def test_permission_missing_perf_and_malformed_output_are_truthful(self) -> None:
        cases = (
            ("permission", "permission_denied", None),
            ("malformed", "malformed_output", None),
            ("missing", "perf_unavailable", "/missing/perf-fixture"),
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = make_fake_perf(root)
            for mode, expected, override in cases:
                output = root / mode
                with mock.patch.dict(os.environ, {"SIMNET_FAKE_PERF_MODE": mode}):
                    result = collector.collect(
                        output,
                        "failure-run",
                        [("server", 42)],
                        "cache",
                        0.01,
                        override or str(fake),
                        95.0,
                        False,
                        proc_root=root / "proc",
                        identity_reader=lambda _pid: 777,
                    )
                self.assertEqual(result, 1)
                rows = read_rows(output)
                self.assertEqual({row["counter_status"] for row in rows}, {expected})
                self.assertEqual({row["value"] for row in rows}, {""})
                self.assertEqual({row["accepted_for_analysis"] for row in rows}, {"0"})

    def test_target_exit_and_pid_reuse_override_partial_counts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = make_fake_perf(root)
            final = {42: None, 43: 999}
            calls: dict[int, int] = {}

            def identity(pid: int) -> int | None:
                calls[pid] = calls.get(pid, 0) + 1
                return 777 if calls[pid] == 1 else final[pid]

            with mock.patch.dict(os.environ, {"SIMNET_FAKE_PERF_MODE": "measured"}):
                result = collector.collect(
                    root / "evidence",
                    "identity-run",
                    [("server", 42), ("client", 43)],
                    "cache",
                    0.01,
                    str(fake),
                    95.0,
                    False,
                    proc_root=root / "proc",
                    identity_reader=identity,
                )
            self.assertEqual(result, 1)
            by_role: dict[str, set[str]] = {}
            for row in read_rows(root / "evidence"):
                by_role.setdefault(row["role"], set()).add(row["counter_status"])
                self.assertEqual(row["accepted_for_analysis"], "0")
            self.assertEqual(by_role, {"server": {"target_exited"}, "client": {"target_reused"}})

    def test_require_complete_profile_rejects_unavailable_events(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = make_fake_perf(root)
            with mock.patch.dict(os.environ, {"SIMNET_FAKE_PERF_MODE": "partial"}):
                optional = collector.collect(
                    root / "optional",
                    "partial-run",
                    [("server", 42)],
                    "core",
                    0.01,
                    str(fake),
                    95.0,
                    False,
                    identity_reader=lambda _pid: 777,
                )
                required = collector.collect(
                    root / "required",
                    "partial-run",
                    [("server", 42)],
                    "core",
                    0.01,
                    str(fake),
                    95.0,
                    True,
                    identity_reader=lambda _pid: 777,
                )
            self.assertEqual(optional, 0)
            self.assertEqual(required, 1)
            statuses = [row["counter_status"] for row in read_rows(root / "required")]
            self.assertEqual(statuses.count("measured"), 1)
            self.assertEqual(statuses.count("not_counted"), 3)

    def test_below_threshold_collection_is_retained_but_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = make_fake_perf(root)
            with mock.patch.dict(os.environ, {"SIMNET_FAKE_PERF_MODE": "low"}):
                result = collector.collect(
                    root / "evidence",
                    "low-run",
                    [("server", 42)],
                    "cache",
                    0.01,
                    str(fake),
                    95.0,
                    False,
                    identity_reader=lambda _pid: 777,
                )
            self.assertEqual(result, 0)
            rows = read_rows(root / "evidence")
            self.assertEqual(rows[0]["counter_status"], "insufficient_running_time")
            self.assertEqual(rows[0]["value"], "1000")
            self.assertEqual(rows[0]["running_percentage"], "80.00")
            self.assertEqual(rows[0]["perf_scaled_value"], "1")
            self.assertEqual(rows[0]["accepted_for_analysis"], "0")
            self.assertEqual(rows[1]["accepted_for_analysis"], "1")

    def test_signal_cleanup_terminates_and_kills_stubborn_children(self) -> None:
        class Child:
            def __init__(self, stubborn: bool) -> None:
                self.stubborn = stubborn
                self.terminated = False
                self.killed = False

            def poll(self) -> None:
                return None

            def terminate(self) -> None:
                self.terminated = True

            def wait(self, timeout: float) -> int:
                if self.stubborn and not self.killed:
                    raise subprocess.TimeoutExpired("fake perf", timeout)
                return 0

            def kill(self) -> None:
                self.killed = True

        ordinary = Child(False)
        stubborn = Child(True)
        collector.stop_children([ordinary, stubborn])
        self.assertTrue(ordinary.terminated)
        self.assertFalse(ordinary.killed)
        self.assertTrue(stubborn.terminated)
        self.assertTrue(stubborn.killed)
        with self.assertRaises(KeyboardInterrupt):
            collector.termination_signal_handler(15, None)


if __name__ == "__main__":
    unittest.main()
