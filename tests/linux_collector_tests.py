#!/usr/bin/env python3
"""Contract tests for the Linux research evidence collector."""

from __future__ import annotations

import csv
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


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


def build_fixture(root: Path) -> collector.LinuxSource:
    proc = root / "proc"
    process = proc / "42"
    process.mkdir(parents=True)

    (process / "stat").write_text(process_stat(), encoding="utf-8")
    (process / "cmdline").write_text("Client\0--run-id\0study-1\0", encoding="utf-8")
    (process / "status").write_text(
        "VmRSS:\t10 kB\n"
        "VmHWM:\t12 kB\n"
        "RssAnon:\t5 kB\n"
        "RssFile:\t4 kB\n"
        "RssShmem:\t1 kB\n"
        "VmSize:\t20 kB\n"
        "VmSwap:\t2 kB\n"
        "Threads:\t4\n"
        "voluntary_ctxt_switches:\t8\n"
        "nonvoluntary_ctxt_switches:\t3\n",
        encoding="utf-8",
    )
    (process / "io").write_text(
        "syscr: 7\n"
        "syscw: 5\n"
        "read_bytes: 4096\n"
        "write_bytes: 8192\n",
        encoding="utf-8",
    )
    (process / "schedstat").write_text("900 100 12\n", encoding="utf-8")
    (process / "smaps_rollup").write_text(
        "Rss: 10 kB\n"
        "Pss: 9 kB\n"
        "Pss_Anon: 5 kB\n"
        "Pss_File: 3 kB\n"
        "Pss_Shmem: 1 kB\n"
        "Shared_Clean: 2 kB\n"
        "Shared_Dirty: 1 kB\n"
        "Private_Clean: 3 kB\n"
        "Private_Dirty: 4 kB\n"
        "Anonymous: 5 kB\n"
        "Swap: 2 kB\n",
        encoding="utf-8",
    )

    (proc / "pressure").mkdir()
    (proc / "pressure/cpu").write_text(
        "some avg10=0.10 avg60=0.20 avg300=0.30 total=40\n",
        encoding="utf-8",
    )
    for name in ("memory", "io"):
        (proc / f"pressure/{name}").write_text(
            "some avg10=0.00 avg60=0.01 avg300=0.02 total=3\n"
            "full avg10=0.00 avg60=0.00 avg300=0.01 total=1\n",
            encoding="utf-8",
        )

    (proc / "meminfo").write_text(
        "MemTotal: 1000 kB\n"
        "MemAvailable: 750 kB\n"
        "SwapTotal: 200 kB\n"
        "SwapFree: 180 kB\n",
        encoding="utf-8",
    )
    (proc / "loadavg").write_text("0.10 0.20 0.30 1/100 7\n", encoding="utf-8")
    (proc / "cpuinfo").write_text("model name: Fixture CPU\n", encoding="utf-8")

    (proc / "net").mkdir()
    (proc / "net/dev").write_text(
        "Inter-| Receive | Transmit\n"
        " face |bytes packets errs drop fifo frame compressed multicast |"
        "bytes packets errs drop fifo colls carrier compressed\n"
        "  lo: 100 2 3 4 0 0 0 0 200 5 6 7 0 0 0 0\n",
        encoding="utf-8",
    )

    return collector.LinuxSource(proc)


class CollectorTests(unittest.TestCase):
    def test_linux_parsers_preserve_documented_units(self) -> None:
        status = collector.parse_proc_status("VmRSS: 2 kB\nThreads: 3\n")
        self.assertEqual(status, {"vm_rss_bytes": 2048, "thread_count": 3})

        schedstat = collector.parse_schedstat("900 100 12\n")
        self.assertEqual(schedstat["scheduler_runtime_ns"], 900)
        self.assertEqual(schedstat["scheduler_wait_ns"], 100)

        smaps = collector.parse_smaps_rollup("Rss: 10 kB\nPss: 9 kB\n")
        self.assertEqual(smaps["smaps_rss_bytes"], 10 * 1024)
        self.assertEqual(smaps["smaps_pss_bytes"], 9 * 1024)

        network = collector.parse_net_dev(
            "a\nb\neth0: 1 2 3 4 0 0 0 0 5 6 7 8 0 0 0 0\n",
            "eth0",
        )
        self.assertEqual(network["network_rx_bytes"], 1)
        self.assertEqual(network["network_rx_drops"], 4)
        self.assertEqual(network["network_tx_bytes"], 5)
        self.assertEqual(network["network_tx_errors"], 7)

    def test_process_samples_reject_pid_reuse(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = build_fixture(root)
            target = source.inspect_target("client", 42)

            values, errors, alive = source.sample_process(target, include_smaps=True)
            self.assertTrue(alive)
            self.assertEqual(errors, [])
            self.assertEqual(values["vm_rss_bytes"], 10 * 1024)
            self.assertEqual(values["scheduler_wait_ns"], 100)
            self.assertEqual(values["smaps_pss_bytes"], 9 * 1024)

            (source.proc_root / "42/stat").write_text(process_stat(999), encoding="utf-8")
            values, errors, alive = source.sample_process(target, include_smaps=False)

            self.assertFalse(alive)
            self.assertEqual(values, {"smaps_rollup_attempted": 0})
            self.assertEqual(errors, ["PID reuse detected from a changed process start time"])

    def test_collection_writes_joinable_evidence_with_fixed_methodology(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = build_fixture(root)
            output = root / "evidence"

            result = collector.collect(
                output_directory=output,
                run_id="study-1",
                target_specs=[("client", 42)],
                interface="lo",
                duration_seconds=1.0e-9,
                source=source,
                unix_ns=iter((1_000, 1_100)).__next__,
                monotonic_ns=iter((500, 550)).__next__,
                sleep=lambda _seconds: None,
            )
            self.assertEqual(result, 0)

            manifest = json.loads((output / "run_manifest_v2.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["schema_version"], collector.MANIFEST_SCHEMA_VERSION)
            self.assertEqual(manifest["run_id"], "study-1")
            self.assertEqual(manifest["sampling_interval_seconds"], 0.1)
            self.assertEqual(manifest["smaps_rollup_every_samples"], 10)
            self.assertEqual(manifest["targets"][0]["role"], "client")
            self.assertEqual(manifest["targets"][0]["pid"], 42)
            self.assertEqual(manifest["targets"][0]["process_start_time_ticks"], 777)
            self.assertTrue(manifest["capabilities"]["host"]["network_interface"]["requested"])
            self.assertEqual(
                manifest["capabilities"]["host"]["network_interface"]["interface"],
                "lo",
            )

            with (output / "process_samples_v1.csv").open(
                    newline="",
                    encoding="utf-8",
            ) as stream:
                process_rows = list(csv.DictReader(stream))
                stream.seek(0)
                self.assertEqual(next(csv.reader(stream)), collector.PROCESS_COLUMNS)

            with (output / "host_samples_v2.csv").open(
                    newline="",
                    encoding="utf-8",
            ) as stream:
                host_rows = list(csv.DictReader(stream))
                stream.seek(0)
                self.assertEqual(next(csv.reader(stream)), collector.HOST_COLUMNS)

            self.assertEqual(len(process_rows), 1)
            self.assertEqual(len(host_rows), 1)

            process = process_rows[0]
            host = host_rows[0]
            self.assertEqual(process["run_id"], host["run_id"])
            self.assertEqual(process["recorded_at_unix_ns"], host["recorded_at_unix_ns"])
            self.assertEqual(process["elapsed_since_collector_start_ns"], "50")
            self.assertEqual(process["record_order"], "0")
            self.assertEqual(host["record_order"], "0")
            self.assertEqual(process["role"], "client")
            self.assertEqual(process["pid"], "42")
            self.assertEqual(process["process_start_time_ticks"], "777")
            self.assertEqual(process["sample_status"], "sampled")
            self.assertEqual(process["errors_json"], "[]")
            self.assertEqual(process["smaps_rollup_attempted"], "1")
            self.assertEqual(host["network_interface"], "lo")
            self.assertEqual(host["network_rx_bytes"], "100")
            self.assertEqual(host["network_tx_bytes"], "200")


if __name__ == "__main__":
    unittest.main()