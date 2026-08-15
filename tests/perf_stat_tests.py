#!/usr/bin/env python3
"""Contract tests for the perf stat research evidence collector."""

from __future__ import annotations

import csv
import importlib.util
import json
import os
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
import sys

if "--version" in sys.argv:
    print("perf version fixture")
    raise SystemExit(0)

events = []
for index, argument in enumerate(sys.argv):
    if argument == "-e":
        events.extend(sys.argv[index + 1].strip("{}").split(","))

for index, event in enumerate(events):
    print(
        f"{1000 + index};count;{event};1000000;100.00;2.5;fixture_metric",
        file=sys.stderr,
    )
'''


def install_fake_perf(root: Path) -> Path:
    binary_directory = root / "bin"
    binary_directory.mkdir()
    path = binary_directory / "perf"
    path.write_text(FAKE_PERF, encoding="utf-8")
    path.chmod(0o755)
    return binary_directory


def read_rows(output: Path) -> list[dict[str, str]]:
    with (output / "perf_counters_v1.csv").open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


class PerfStatTests(unittest.TestCase):
    def test_profiles_preserve_simultaneous_ratio_groups(self) -> None:
        target = collector.Target("server", 42, 777)
        invocation = collector.build_perf_invocation(
            "/usr/bin/perf",
            target,
            collector.PROFILES["core"],
            1.25,
        )

        groups = [invocation[index + 1] for index, value in enumerate(invocation) if value == "-e"]
        self.assertEqual(groups, ["{cycles,instructions}", "{branches,branch-misses}"])
        self.assertEqual(invocation[-4:], ["-p", "42", "--timeout", "1250"])

        self.assertEqual(
            collector.PROFILES["memory"],
            (
                ("instructions", "L1-dcache-loads", "L1-dcache-load-misses"),
                ("instructions", "LLC-loads", "LLC-load-misses"),
                ("instructions", "dTLB-loads", "dTLB-load-misses"),
                ("instructions", "iTLB-loads", "iTLB-load-misses"),
            ),
        )

        lines = [
            f"100;count;{event};1000000;100.00;;"
            for group in collector.PROFILES["memory"]
            for event in group
        ]
        observations, diagnostics = collector.parse_perf_output(
            "\n".join(lines),
            collector.PROFILES["memory"],
        )
        self.assertEqual(diagnostics, [])
        self.assertEqual(
            [item.group_index for item in observations if item.event == "instructions"],
            [0, 1, 2, 3],
        )

    def test_perf_output_preserves_values_and_fixed_acceptance_rule(self) -> None:
        output = (
            "2400;count;cycles;900000;100.00;2.4;GHz\n"
            "1200;count;instructions;700000;94.99;1.5;insn per cycle\n"
            "800;count;branches;800000;95.00;;\n"
            "<not counted>;;branch-misses;;;;\n"
        )
        observations, diagnostics = collector.parse_perf_output(
            output,
            collector.PROFILES["core"],
        )

        self.assertEqual(diagnostics, [])
        by_event = {item.event: item for item in observations}

        self.assertEqual(by_event["cycles"].status, "measured")
        self.assertEqual(by_event["cycles"].value, "2400")
        self.assertEqual(by_event["cycles"].counter_runtime_ns, "900000")
        self.assertEqual(by_event["cycles"].metric_value, "2.4")
        self.assertEqual(by_event["cycles"].metric_unit, "GHz")
        self.assertTrue(by_event["cycles"].accepted)

        self.assertEqual(by_event["instructions"].status, "insufficient_running_time")
        self.assertEqual(by_event["instructions"].running_percentage, "94.99")
        self.assertEqual(by_event["instructions"].perf_scaled_value, "1")
        self.assertFalse(by_event["instructions"].accepted)

        self.assertEqual(by_event["branches"].status, "measured")
        self.assertEqual(by_event["branches"].running_percentage, "95.00")
        self.assertTrue(by_event["branches"].accepted)
        self.assertEqual(by_event["branch-misses"].status, "not_counted")

        hybrid = (
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
            hybrid,
            collector.PROFILES["core"],
        )
        self.assertEqual(diagnostics, [])
        by_event = {item.event: item for item in observations}
        self.assertEqual(by_event["cycles"].value, "1000")
        self.assertEqual(by_event["cycles"].counter_runtime_ns, "990000000")
        self.assertEqual(by_event["cycles"].running_percentage, "99.00")
        self.assertTrue(all(item.accepted for item in observations))

    def test_pid_reuse_rejects_otherwise_valid_counters(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake_bin = install_fake_perf(root)
            calls = 0

            def identity(_pid: int) -> int:
                nonlocal calls
                calls += 1
                return 777 if calls == 1 else 999

            with mock.patch.dict(
                    os.environ,
                    {"PATH": str(fake_bin) + os.pathsep + os.environ.get("PATH", "")},
            ):
                result = collector.collect(
                    root / "evidence",
                    "identity-run",
                    [("server", 42)],
                    "core",
                    0.01,
                    proc_root=root / "proc",
                    identity_reader=identity,
                    )

            self.assertEqual(result, 1)
            rows = read_rows(root / "evidence")
            self.assertEqual({row["counter_status"] for row in rows}, {"target_reused"})
            self.assertEqual({row["accepted_for_analysis"] for row in rows}, {"0"})

            manifest = json.loads(
                (root / "evidence/perf_manifest_v2.json").read_text(encoding="utf-8")
            )
            self.assertEqual(manifest["targets"][0]["target_identity_status"], "target_reused")
            self.assertEqual(manifest["targets"][0]["process_start_ticks"], 777)
            self.assertEqual(manifest["targets"][0]["final_process_start_ticks"], 999)

    def test_collection_writes_joinable_evidence_for_each_target(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake_bin = install_fake_perf(root)
            output = root / "evidence"

            with mock.patch.dict(
                    os.environ,
                    {"PATH": str(fake_bin) + os.pathsep + os.environ.get("PATH", "")},
            ):
                result = collector.collect(
                    output,
                    "fixture-run",
                    [("server", 42), ("client", 43)],
                    "core",
                    0.01,
                    proc_root=root / "proc",
                    identity_reader=lambda _pid: 777,
                )

            self.assertEqual(result, 0)

            manifest = json.loads((output / "perf_manifest_v2.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["schema_version"], collector.MANIFEST_SCHEMA_VERSION)
            self.assertEqual(manifest["run_id"], "fixture-run")
            self.assertEqual(manifest["minimum_running_percentage"], 95.0)
            self.assertEqual(manifest["perf"]["version"], "perf version fixture")
            self.assertEqual(
                manifest["profile"]["event_groups"],
                [["cycles", "instructions"], ["branches", "branch-misses"]],
            )
            self.assertEqual(
                {target["target_identity_status"] for target in manifest["targets"]},
                {"stable"},
            )

            rows = read_rows(output)
            self.assertEqual(len(rows), 8)
            self.assertEqual([int(row["record_order"]) for row in rows], list(range(8)))
            self.assertEqual({row["run_id"] for row in rows}, {"fixture-run"})
            self.assertEqual({row["role"] for row in rows}, {"server", "client"})
            self.assertEqual({row["process_start_ticks"] for row in rows}, {"777"})
            self.assertEqual({row["accepted_for_analysis"] for row in rows}, {"1"})

            with (output / "perf_counters_v1.csv").open(newline="", encoding="utf-8") as stream:
                self.assertEqual(next(csv.reader(stream)), collector.COUNTER_COLUMNS)


if __name__ == "__main__":
    unittest.main()