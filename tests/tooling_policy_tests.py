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
            "`for (;;)` is exact code.\n"
            "https://example.test/query;a=b\n"
            "```cpp\nint value;\n```\n"
        )
        self.assertEqual(self.categories("README.md", text), ["prose-semicolon"])

    def test_cpp_checks_comments_without_treating_syntax_as_prose(self) -> None:
        text = (
            "int value; // Bad prose; split it.\n"
            "const char* text = \"literal;value\";\n"
            "// CHECK(result == expected);\n"
        )
        self.assertEqual(self.categories("file.cpp", text), ["prose-semicolon"])

    def test_python_checks_comments_and_docstrings(self) -> None:
        text = '"""Bad prose; split it."""\nvalue = "syntax;data"\n# Also bad; split it.\n'
        self.assertEqual(
            self.categories("file.py", text),
            ["prose-semicolon", "prose-semicolon"],
        )

    def test_json_checks_human_readable_strings(self) -> None:
        self.assertEqual(
            self.categories("file.json", '{"message": "Bad prose; split it."}\n'),
            ["prose-semicolon"],
        )

    def test_unicode_reports_specific_code_points_and_categories(self) -> None:
        diagnostics = policy.check_text(Path("file.md"), "dash - em \u2014 smart \u201c bidi \u202e\n")
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


if __name__ == "__main__":
    unittest.main()
