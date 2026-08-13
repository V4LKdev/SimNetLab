#!/usr/bin/env python3
"""Check maintained authored text for ASCII and prose punctuation policy."""

from __future__ import annotations

import argparse
import ast
import io
import json
import re
import subprocess
import sys
import tokenize
import unicodedata
from dataclasses import dataclass
from pathlib import Path


TEXT_SUFFIXES = {".cmake", ".cpp", ".cppm", ".h", ".hpp", ".json", ".md", ".py", ".sh", ".txt"}
EXCLUDED_PREFIXES = ("assets/", "build/", "logs/", "results/", "src/render/assets/", "vcpkg/")
DASH_LOOKALIKES = {0x2010, 0x2011, 0x2012, 0x2013, 0x2014, 0x2212, 0xFE58, 0xFE63, 0xFF0D}
SMART_QUOTES = {0x2018, 0x2019, 0x201A, 0x201B, 0x201C, 0x201D, 0x201E, 0x201F}
UNICODE_SPACES = {0x00A0, 0x2007, 0x202F}
ZERO_WIDTH = {0x200B, 0x200C, 0x200D, 0x2060, 0xFEFF}
BIDI_CONTROLS = set(range(0x202A, 0x202F)) | set(range(0x2066, 0x206A)) | {0x061C, 0x200E, 0x200F}
INLINE_CODE = re.compile(r"`[^`]*`")
URL = re.compile(r"(?:https?://|file://)\S+")
EXACT_MARKER = "text-policy: exact"


@dataclass(frozen=True)
class Diagnostic:
    path: Path
    line: int
    column: int
    code_point: str
    category: str
    message: str

    def render(self) -> str:
        return (
            f"{self.path}:{self.line}:{self.column}: {self.code_point} "
            f"{self.category}: {self.message}"
        )


def maintained_files(root: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=root,
        check=True,
        capture_output=True,
    )
    files: list[Path] = []
    for raw_path in result.stdout.split(b"\0"):
        if not raw_path:
            continue
        relative = raw_path.decode()
        path = root / relative
        if relative.startswith(EXCLUDED_PREFIXES) or not path.is_file():
            continue
        if path.name in {"CMakeLists.txt", "README"} or path.suffix in TEXT_SUFFIXES:
            files.append(path)
    return sorted(files)


def requested_files(root: Path, values: list[str]) -> list[Path]:
    if not values:
        return maintained_files(root)
    files: set[Path] = set()
    for value in values:
        candidate = (root / value).resolve()
        if candidate.is_dir():
            for path in candidate.rglob("*"):
                is_text_file = path.name == "CMakeLists.txt" or path.suffix in TEXT_SUFFIXES
                if path.is_file() and is_text_file:
                    relative = path.relative_to(root).as_posix()
                    if not relative.startswith(EXCLUDED_PREFIXES):
                        files.add(path)
        elif candidate.is_file():
            files.add(candidate)
        else:
            raise ValueError(f"path does not exist: {value}")
    return sorted(files)


def unicode_category(character: str, line: str, column: int) -> tuple[str, str]:
    code_point = ord(character)
    if code_point in DASH_LOOKALIKES:
        return "unicode-dash", "use ASCII hyphen-minus U+002D"
    if code_point in SMART_QUOTES:
        return "smart-quote", "use an ASCII quote"
    if code_point == 0x2026:
        return "unicode-ellipsis", "use three ASCII periods"
    if code_point in UNICODE_SPACES:
        return "unicode-space", "use ASCII space U+0020"
    if code_point in ZERO_WIDTH:
        return "zero-width", "zero-width characters are forbidden"
    if code_point in BIDI_CONTROLS:
        return "bidi-control", "bidirectional control characters are forbidden"
    previous = line[column - 2] if column > 1 else ""
    following = line[column] if column < len(line) else ""
    if unicodedata.category(character)[0] in {"L", "M", "N"} and (
        previous == "_" or following == "_" or previous.isidentifier() or following.isidentifier()
    ):
        return "identifier-confusable", "project identifiers must use ASCII characters"
    if unicodedata.category(character) in {"So", "Sk"}:
        return "decorative-glyph", "emoji and decorative glyphs are forbidden"
    return "non-ascii", "maintained authored text must use ASCII"


def character_diagnostics(path: Path, text: str, allow_unicode: bool) -> list[Diagnostic]:
    diagnostics: list[Diagnostic] = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        for column, character in enumerate(line, start=1):
            if character == "\t":
                diagnostics.append(
                    Diagnostic(path, line_number, column, "U+0009", "tab", "use spaces")
                )
            elif not allow_unicode and ord(character) > 0x7F:
                category, message = unicode_category(character, line, column)
                diagnostics.append(
                    Diagnostic(
                        path,
                        line_number,
                        column,
                        f"U+{ord(character):04X}",
                        category,
                        message,
                    )
                )
    return diagnostics


def masked_prose(line: str) -> str:
    masked = INLINE_CODE.sub("", line)
    return URL.sub("", masked)


def semicolon_diagnostics(path: Path, spans: list[tuple[int, int, str]]) -> list[Diagnostic]:
    diagnostics: list[Diagnostic] = []
    for line_number, base_column, prose in spans:
        if EXACT_MARKER in prose:
            continue
        masked = masked_prose(prose)
        for match in re.finditer(";", masked):
            diagnostics.append(
                Diagnostic(
                    path,
                    line_number,
                    base_column + match.start(),
                    "U+003B",
                    "prose-semicolon",
                    "split prose into separate sentences",
                )
            )
    return diagnostics


def markdown_spans(text: str) -> list[tuple[int, int, str]]:
    spans: list[tuple[int, int, str]] = []
    fence = ""
    for line_number, line in enumerate(text.splitlines(), start=1):
        stripped = line.lstrip()
        if stripped.startswith(("```", "~~~")):
            marker = stripped[:3]
            fence = "" if fence == marker else marker if not fence else fence
            continue
        if not fence:
            spans.append((line_number, 1, line))
    return spans


def c_family_comment_spans(text: str) -> list[tuple[int, int, str]]:
    spans: list[tuple[int, int, str]] = []
    in_block = False
    for line_number, line in enumerate(text.splitlines(), start=1):
        cursor = 0
        quote = ""
        while cursor < len(line):
            if in_block:
                end = line.find("*/", cursor)
                if end < 0:
                    spans.append((line_number, cursor + 1, line[cursor:]))
                    break
                spans.append((line_number, cursor + 1, line[cursor:end]))
                cursor = end + 2
                in_block = False
                continue
            character = line[cursor]
            if quote:
                if character == "\\":
                    cursor += 2
                    continue
                if character == quote:
                    quote = ""
                cursor += 1
                continue
            if character in {'"', "'"}:
                quote = character
                cursor += 1
                continue
            if line.startswith("//", cursor):
                spans.append((line_number, cursor + 3, line[cursor + 2 :]))
                break
            if line.startswith("/*", cursor):
                cursor += 2
                in_block = True
                continue
            cursor += 1
    return spans


def python_spans(text: str) -> list[tuple[int, int, str]]:
    spans: list[tuple[int, int, str]] = []
    try:
        tokens = list(tokenize.generate_tokens(io.StringIO(text).readline))
        tree = ast.parse(text)
    except (IndentationError, SyntaxError, tokenize.TokenError):
        return spans
    for token in tokens:
        if token.type == tokenize.COMMENT:
            spans.append((token.start[0], token.start[1] + 2, token.string[1:]))
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)):
            body = node.body
            if body and isinstance(body[0], ast.Expr) and isinstance(body[0].value, ast.Constant):
                value = body[0].value.value
                if isinstance(value, str):
                    spans.append((body[0].lineno, body[0].col_offset + 1, value))
    return spans


def hash_comment_spans(text: str) -> list[tuple[int, int, str]]:
    spans: list[tuple[int, int, str]] = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        quote = ""
        for index, character in enumerate(line):
            if quote:
                if character == quote and (index == 0 or line[index - 1] != "\\"):
                    quote = ""
            elif character in {'"', "'"}:
                quote = character
            elif character == "#" and not (index == 0 and line.startswith("#!")):
                spans.append((line_number, index + 2, line[index + 1 :]))
                break
    return spans


def json_spans(text: str) -> list[tuple[int, int, str]]:
    try:
        json.loads(text)
    except json.JSONDecodeError:
        return []
    spans: list[tuple[int, int, str]] = []
    string_pattern = re.compile(r'"(?:\\.|[^"\\])*"')
    for line_number, line in enumerate(text.splitlines(), start=1):
        for match in string_pattern.finditer(line):
            try:
                value = json.loads(match.group())
            except json.JSONDecodeError:
                continue
            spans.append((line_number, match.start() + 2, value))
    return spans


def prose_spans(path: Path, text: str) -> list[tuple[int, int, str]]:
    if path.suffix == ".md" or path.name == "README":
        return markdown_spans(text)
    if path.suffix in {".cpp", ".cppm", ".h", ".hpp"}:
        return c_family_comment_spans(text)
    if path.suffix == ".py":
        return python_spans(text)
    if path.suffix == ".json":
        return json_spans(text)
    if path.suffix in {".cmake", ".sh"} or path.name == "CMakeLists.txt":
        return hash_comment_spans(text)
    return [(line_number, 1, line) for line_number, line in enumerate(text.splitlines(), start=1)]


def check_text(path: Path, text: str, allow_unicode: bool = False) -> list[Diagnostic]:
    diagnostics = character_diagnostics(path, text, allow_unicode)
    diagnostics.extend(semicolon_diagnostics(path, prose_spans(path, text)))
    return sorted(diagnostics, key=lambda item: (item.line, item.column, item.category))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths", nargs="*", help="optional repository-relative files or directories"
    )
    parser.add_argument(
        "--allow-unicode",
        action="append",
        default=[],
        metavar="PATH",
        help="explicit exact path for a deliberate Unicode fixture",
    )
    arguments = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    try:
        files = requested_files(root, arguments.paths)
    except (OSError, ValueError) as error:
        print(f"simnet_text_policy: {error}", file=sys.stderr)
        return 2
    allowed = {(root / value).resolve() for value in arguments.allow_unicode}

    diagnostics: list[Diagnostic] = []
    for path in files:
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as error:
            print(f"simnet_text_policy: cannot read {path}: {error}", file=sys.stderr)
            return 2
        relative = path.relative_to(root)
        diagnostics.extend(check_text(relative, text, path.resolve() in allowed))

    for diagnostic in sorted(
        diagnostics,
        key=lambda item: (item.path.as_posix(), item.line, item.column, item.category),
    ):
        print(diagnostic.render())
    if diagnostics:
        print(f"text policy found {len(diagnostics)} violations", file=sys.stderr)
        return 1
    print(f"text policy check passed for {len(files)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
