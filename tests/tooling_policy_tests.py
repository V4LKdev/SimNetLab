#!/usr/bin/env python3
"""Deterministic tests for repository text-policy contexts."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[1] / "scripts/simnet_text_policy.py"
sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("simnet_text_policy", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
policy = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = policy
SPEC.loader.exec_module(policy)


class TextPolicyTests(unittest.TestCase):
    def categories(self, path: str, text: str) -> list[str]:
        return [item.category for item in policy.check_text(Path(path), text)]

    def test_markdown_distinguishes_prose_from_code_and_urls(self) -> None:
        text = (
            "Bad prose; split it.\n"
            "`tool --expression='for (;;)'` is an exact command.\n"
            "https://example.test/query;a=b\n"
            "```cpp\nint value;\n```\n"
        )
        self.assertEqual(self.categories("README.md", text), ["prose-semicolon"])

    def test_cpp_checks_comments_without_treating_syntax_as_prose(self) -> None:
        text = (
            'int value; // Bad prose (with "quotes"); split it.\n'
            "const char* text = \"literal;value\";\n"
            "// CHECK(result == expected); text-policy: exact\n"
        )
        self.assertEqual(self.categories("file.cpp", text), ["prose-semicolon"])

    def test_python_checks_comments_and_docstrings(self) -> None:
        text = (
            '"""Bad prose; split it."""\n'
            'value = "syntax;data"\n'
            '# Also bad (with "quotes"); split it.\n'
        )
        self.assertEqual(
            self.categories("file.py", text),
            ["prose-semicolon", "prose-semicolon"],
        )

    def test_json_checks_human_readable_strings(self) -> None:
        self.assertEqual(
            self.categories(
                "file.json", '{"message": "Bad prose (with quotes); split it."}\n'
            ),
            ["prose-semicolon"],
        )

    def test_cmake_and_shell_check_comments_without_treating_commands_as_prose(self) -> None:
        cmake = 'set(values "first;second")\n# Bad prose (with "quotes"); split it.\n'
        shell = 'printf "%s;\\n" "$value"\n# Bad prose (with "quotes"); split it.\n'
        self.assertEqual(self.categories("CMakeLists.txt", cmake), ["prose-semicolon"])
        self.assertEqual(self.categories("script.sh", shell), ["prose-semicolon"])

    def test_unicode_reports_specific_code_points_and_categories(self) -> None:
        diagnostics = policy.check_text(
            Path("file.md"), "dash - em \u2014 smart \u201c bidi \u202e\n"
        )
        self.assertEqual(
            [(item.code_point, item.category) for item in diagnostics],
            [
                ("U+2014", "unicode-dash"),
                ("U+201C", "smart-quote"),
                ("U+202E", "bidi-control"),
            ],
        )

    def test_exact_marker_is_a_narrow_semicolon_exception(self) -> None:
        text = "Captured output; exact text. text-policy: exact\n"
        self.assertEqual(self.categories("file.txt", text), [])

    def test_unicode_fixture_exception_does_not_allow_tabs(self) -> None:
        diagnostics = policy.check_text(
            Path("fixture.txt"), "allowed \u2014\ttext\n", allow_unicode=True
        )
        self.assertEqual([item.category for item in diagnostics], ["tab"])


if __name__ == "__main__":
    unittest.main()
