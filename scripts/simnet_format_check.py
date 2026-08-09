#!/usr/bin/env python3
"""Check maintained C++ files with the repository clang-format policy."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


CPP_SUFFIXES = {".cpp", ".cppm", ".h", ".hpp"}
EXCLUDED_PARTS = {"build", "vcpkg"}


def repository_root() -> Path:
    return Path(__file__).resolve().parents[1]


def tracked_cpp_files(root: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=root,
        check=True,
        capture_output=True,
    )
    paths = [root / item.decode() for item in result.stdout.split(b"\0") if item]
    return sorted(path for path in paths if path.suffix in CPP_SUFFIXES)


def requested_cpp_files(root: Path, requested: list[str]) -> list[Path]:
    if not requested:
        return tracked_cpp_files(root)

    files: set[Path] = set()
    for value in requested:
        candidate = (root / value).resolve()
        if candidate.is_dir():
            for path in candidate.rglob("*"):
                relative_parts = path.relative_to(root).parts
                if (
                    path.is_file()
                    and path.suffix in CPP_SUFFIXES
                    and not EXCLUDED_PARTS.intersection(relative_parts)
                ):
                    files.add(path)
        elif candidate.is_file() and candidate.suffix in CPP_SUFFIXES:
            files.add(candidate)
        else:
            raise ValueError(f"not a C++ file or directory: {value}")
    return sorted(files)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", help="optional repository-relative files or directories")
    parser.add_argument("--clang-format", default="clang-format", dest="clang_format")
    arguments = parser.parse_args()

    executable = shutil.which(arguments.clang_format)
    if executable is None:
        print(f"simnet_format_check: executable not found: {arguments.clang_format}", file=sys.stderr)
        return 2

    root = repository_root()
    try:
        files = requested_cpp_files(root, arguments.paths)
    except (OSError, ValueError) as error:
        print(f"simnet_format_check: {error}", file=sys.stderr)
        return 2
    if not files:
        print("simnet_format_check: no C++ files selected", file=sys.stderr)
        return 2

    result = subprocess.run(
        [executable, "--style=file", "--dry-run", "--Werror", *map(str, files)],
        cwd=root,
        check=False,
    )
    if result.returncode == 0:
        print(f"clang-format check passed for {len(files)} files")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
