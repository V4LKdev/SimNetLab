#!/usr/bin/env python3
"""Run advisory clang-tidy checks from a Clang compilation database."""

from __future__ import annotations

import argparse
import json
import shlex
import shutil
import subprocess
import sys
from pathlib import Path


CPP_SUFFIXES = {".cpp", ".cppm", ".h", ".hpp"}


def compiler_name(entry: dict[str, object]) -> str:
    arguments = entry.get("arguments")
    if isinstance(arguments, list) and arguments:
        return Path(str(arguments[0])).name
    command = entry.get("command")
    if isinstance(command, str):
        tokens = shlex.split(command)
        if tokens:
            return Path(tokens[0]).name
    return ""


def selected_files(
    root: Path,
    entries: list[dict[str, object]],
    requested_paths: list[str],
) -> list[Path]:
    filters = [(root / value).resolve() for value in requested_paths]
    files: set[Path] = set()
    for entry in entries:
        value = entry.get("file")
        if not isinstance(value, str):
            continue
        path = Path(value).resolve()
        if path.suffix not in CPP_SUFFIXES or not path.is_relative_to(root):
            continue
        if filters and not any(path == item or path.is_relative_to(item) for item in filters):
            continue
        files.add(path)
    return sorted(files)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--path", action="append", default=[], dest="paths")
    parser.add_argument("--clang-tidy", default="clang-tidy", dest="clang_tidy")
    arguments = parser.parse_args()

    if any("fix" in argument for argument in sys.argv[1:]):
        print("simnet_clang_tidy: fix options are forbidden", file=sys.stderr)
        return 2

    executable = shutil.which(arguments.clang_tidy)
    if executable is None:
        print(f"simnet_clang_tidy: executable not found: {arguments.clang_tidy}", file=sys.stderr)
        return 2

    root = Path(__file__).resolve().parents[1]
    build_dir = arguments.build_dir.resolve()
    database = build_dir / "compile_commands.json"
    try:
        entries = json.loads(database.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"simnet_clang_tidy: cannot read {database}: {error}", file=sys.stderr)
        return 2
    if not isinstance(entries, list) or not entries:
        print(f"simnet_clang_tidy: empty compilation database: {database}", file=sys.stderr)
        return 2

    compilers = {compiler_name(entry) for entry in entries}
    if any("clang" not in name for name in compilers):
        print(
            "simnet_clang_tidy: compilation database must contain only Clang commands, found "
            + ", ".join(sorted(compilers)),
            file=sys.stderr,
        )
        return 2

    files = selected_files(root, entries, arguments.paths)
    if not files:
        print("simnet_clang_tidy: no compilation database entries selected", file=sys.stderr)
        return 2

    failures = 0
    for file_index, path in enumerate(files, start=1):
        relative = path.relative_to(root)
        print(f"[{file_index}/{len(files)}] {relative}", flush=True)
        result = subprocess.run(
            [
                executable,
                "--config-file=" + str(root / ".clang-tidy"),
                "-p=" + str(build_dir),
                str(path),
            ],
            cwd=root,
            check=False,
        )
        if result.returncode != 0:
            failures += 1

    if failures:
        print(f"clang-tidy failed to analyze {failures} of {len(files)} files", file=sys.stderr)
        return 1
    print(f"clang-tidy advisory pass completed for {len(files)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
