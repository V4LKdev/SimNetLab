#!/usr/bin/env python3
"""Deterministic contract tests for the Linux evidence collector."""

from __future__ import annotations

import csv
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "scripts/simnet_linux_collector.py"
sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("simnet_linux_collector", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
collector = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = collector
SPEC.loader.exec_module(collector)


def process_stat(start_time: int = 777) -> str:
    fields = ["0"] * 37
    fields[0] = "S"
    fields[7] = "11"
    fields[9] = "2"
    fields[11] = "101"
    fields[12] = "23"
    fields[17] = "4"
    fields[19] = str(start_time)
    fields[36] = "6"
    return "42 (SimNet Client worker) " + " ".join(fields) + "\n"


class FixtureTree:
    def __init__(self, root: Path) -> None:
        self.proc = root / "proc"
        self.sys = root / "sys"
        process = self.proc / "42"
        process.mkdir(parents=True)
        (process / "stat").write_text(process_stat(), encoding="utf-8")
        (process / "cmdline").write_text("Client\0--run-id\0study-1\0", encoding="utf-8")
        (process / "status").write_text(
            "VmRSS:\t10 kB\nVmHWM:\t12 kB\nRssAnon:\t5 kB\nRssFile:\t4 kB\n"
            "RssShmem:\t1 kB\nVmSize:\t20 kB\nVmSwap:\t2 kB\nThreads:\t4\n"
            "voluntary_ctxt_switches:\t8\nnonvoluntary_ctxt_switches:\t3\n",
            encoding="utf-8",
        )
        (process / "io").write_text(
            "syscr: 7\nsyscw: 5\nread_bytes: 4096\nwrite_bytes: 8192\n",
            encoding="utf-8",
        )
        (process / "schedstat").write_text("900 100 12\n", encoding="utf-8")
        (process / "smaps_rollup").write_text(
            "Rss: 10 kB\nPss: 9 kB\nPss_Anon: 5 kB\nPss_File: 3 kB\n"
            "Pss_Shmem: 1 kB\nShared_Clean: 2 kB\nShared_Dirty: 1 kB\n"
            "Private_Clean: 3 kB\nPrivate_Dirty: 4 kB\nAnonymous: 5 kB\nSwap: 2 kB\n",
            encoding="utf-8",
        )
        (self.proc / "pressure").mkdir()
        (self.proc / "pressure/cpu").write_text(
            "some avg10=0.10 avg60=0.20 avg300=0.30 total=40\n", encoding="utf-8"
        )
        for name in ("memory", "io"):
            (self.proc / f"pressure/{name}").write_text(
                "some avg10=0.00 avg60=0.01 avg300=0.02 total=3\n"
                "full avg10=0.00 avg60=0.00 avg300=0.01 total=1\n",
                encoding="utf-8",
            )
        (self.proc / "meminfo").write_text(
            "MemTotal: 1000 kB\nMemAvailable: 750 kB\nSwapTotal: 200 kB\nSwapFree: 180 kB\n",
            encoding="utf-8",
        )
        (self.proc / "loadavg").write_text("0.10 0.20 0.30 1/100 7\n", encoding="utf-8")
        (self.proc / "cpuinfo").write_text("model name: Fixture CPU\n", encoding="utf-8")
        (self.proc / "net").mkdir()
        (self.proc / "net/dev").write_text(
            "Inter-| Receive | Transmit\n"
            " face |bytes packets errs drop fifo frame compressed multicast |"
            "bytes packets errs drop fifo colls carrier compressed\n"
            "  lo: 100 2 3 4 0 0 0 0 200 5 6 7 0 0 0 0\n",
            encoding="utf-8",
        )
        self.sys.mkdir()


class CollectorTests(unittest.TestCase):
    def test_parsers_preserve_documented_units(self) -> None:
        stat = collector.parse_proc_stat(process_stat())
        self.assertEqual(stat["process_start_time_ticks"], 777)
        self.assertEqual(stat["current_processor"], 6)
        self.assertEqual(stat["thread_count"], 4)
        status = collector.parse_proc_status("VmRSS: 2 kB\nThreads: 3\n")
        self.assertEqual(status, {"vm_rss_bytes": 2048, "thread_count": 3})
        network = collector.parse_net_dev(
            "a\nb\neth0: 1 2 3 4 0 0 0 0 5 6 7 8 0 0 0 0\n", "eth0"
        )
        self.assertEqual(network["network_rx_drops"], 4)
        self.assertEqual(network["network_tx_errors"], 7)

    def test_missing_permission_exit_and_pid_reuse_are_explicit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = FixtureTree(Path(directory))
            source = collector.LinuxSource(fixture.proc, fixture.sys)
            target = source.inspect_target("client", 42)
            values, errors, alive = source.sample_process(target, include_smaps=True)
            self.assertTrue(alive)
            self.assertEqual(errors, [])
            self.assertEqual(values["vm_rss_bytes"], 10 * 1024)
            self.assertEqual(values["scheduler_wait_ns"], 100)
            self.assertEqual(values["smaps_pss_bytes"], 9 * 1024)

            original = Path.read_text

            def denied(path: Path, *args: object, **kwargs: object) -> str:
                if path.name == "io":
                    raise PermissionError("fixture denial")
                return original(path, *args, **kwargs)

            with mock.patch.object(Path, "read_text", denied):
                values, errors, alive = source.sample_process(target, include_smaps=False)
            self.assertTrue(alive)
            self.assertNotIn("read_bytes", values)
            self.assertTrue(any("target process owner" in error for error in errors))

            (fixture.proc / "42/schedstat").write_text("invalid\n", encoding="utf-8")
            values, errors, alive = source.sample_process(target, include_smaps=False)
            self.assertTrue(alive)
            self.assertNotIn("scheduler_wait_ns", values)
            self.assertTrue(any("schedstat" in error and "truncated" in error for error in errors))

            (fixture.proc / "42/stat").write_text(process_stat(999), encoding="utf-8")
            _, errors, alive = source.sample_process(target, include_smaps=False)
            self.assertFalse(alive)
            self.assertTrue(any("PID reuse" in error for error in errors))

            (fixture.proc / "42/stat").unlink()
            _, errors, alive = source.sample_process(target, include_smaps=False)
            self.assertFalse(alive)
            self.assertTrue(any("missing metric source" in error for error in errors))

    def test_collection_has_stable_columns_join_keys_and_monotonic_order(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = FixtureTree(root)
            hwmon = fixture.sys / "class/hwmon/hwmon0"
            hwmon.mkdir(parents=True)
            (hwmon / "name").write_text("fixture_sensor\n", encoding="utf-8")
            (hwmon / "temp1_label").write_text("package,quoted\n", encoding="utf-8")
            (hwmon / "temp1_input").write_text("42000\n", encoding="utf-8")
            source = collector.LinuxSource(fixture.proc, fixture.sys)
            unix_values = iter((1_000, 1_100))
            monotonic_values = iter((500, 550))
            output = root / "evidence"
            result = collector.collect(
                output_directory=output,
                run_id="study-1",
                target_specs=[("client", 42)],
                interval_seconds=0.1,
                interface="lo",
                duration_seconds=0.0,
                smaps_every=1,
                source=source,
                unix_ns=lambda: next(unix_values),
                monotonic_ns=lambda: next(monotonic_values),
                sleep=lambda _seconds: None,
            )
            self.assertEqual(result, 0)
            manifest = json.loads((output / "run_manifest_v1.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["run_id"], "study-1")
            self.assertEqual(manifest["targets"][0]["role"], "client")
            self.assertIn("environmental_covariates", manifest["measurement_notes"])
            self.assertFalse(manifest["capabilities"]["host"]["powercap_energy"]["available"])
            self.assertIn(
                "no readable powercap",
                manifest["capabilities"]["host"]["powercap_energy"]["reason"],
            )

            with (output / "process_samples_v1.csv").open(newline="", encoding="utf-8") as stream:
                process_rows = list(csv.DictReader(stream))
                stream.seek(0)
                self.assertEqual(next(csv.reader(stream)), collector.PROCESS_COLUMNS)
            with (output / "host_samples_v1.csv").open(newline="", encoding="utf-8") as stream:
                host_rows = list(csv.DictReader(stream))
                stream.seek(0)
                self.assertEqual(next(csv.reader(stream)), collector.HOST_COLUMNS)
            self.assertEqual(len(process_rows), 1)
            self.assertEqual(len(host_rows), 1)
            self.assertEqual(process_rows[0]["run_id"], host_rows[0]["run_id"])
            self.assertEqual(
                process_rows[0]["recorded_at_unix_ns"], host_rows[0]["recorded_at_unix_ns"]
            )
            self.assertEqual(process_rows[0]["record_order"], "0")
            self.assertEqual(host_rows[0]["record_order"], "0")
            self.assertEqual(process_rows[0]["elapsed_since_collector_start_ns"], "50")
            self.assertEqual(process_rows[0]["errors_json"], "[]")
            self.assertEqual(process_rows[0]["smaps_rollup_attempted"], "1")
            self.assertEqual(host_rows[0]["network_rx_bytes"], "100")
            temperatures = json.loads(host_rows[0]["temperature_values_json"])
            self.assertEqual(temperatures["hwmon0/temp1"]["value"], 42000)

            with self.assertRaises(FileExistsError):
                collector.collect(
                    output,
                    "study-1",
                    [("client", 42)],
                    0.1,
                    "lo",
                    0.0,
                    1,
                    source=source,
                )


if __name__ == "__main__":
    unittest.main()
