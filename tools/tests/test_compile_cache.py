#!/usr/bin/env python3
"""Tests for the shared compile cache's wiring (the end of CMakeLists.txt, Get-CompileCacheArguments in common.ps1).

Run: tools/py -I tools/tests/test_compile_cache.py
The tree check reads this checkout's build/ when it is a Ninja tree with the cache, and is skipped otherwise.
"""
import re
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RELATIVE = r"tools\ccache-4.14.2\ccache-4.14.2-windows-x86_64\ccache.exe"
STATEMENT = re.compile(r"^build .*?: (?:CXX|C)_COMPILER__(\w+?)_unscanned_Release ", re.M)


def arguments(local):
    """Get-CompileCacheArguments' output, one per line, with $local pointed at a scratch folder."""
    script = textwrap.dedent(f"""
        . '{ROOT}\\tools\\common.ps1'
        $local = '{local}'
        Get-CompileCacheArguments
    """)
    done = subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True, check=True)
    return done.stdout.split("\n")[:-1]


class Arguments(unittest.TestCase):
    def test_nothing_without_ccache(self):
        with tempfile.TemporaryDirectory() as local:
            self.assertEqual(arguments(local), [])

    def test_both_variables_with_it(self):
        with tempfile.TemporaryDirectory() as local:
            exe = Path(local) / RELATIVE
            exe.parent.mkdir(parents=True)
            exe.touch()
            self.assertEqual(arguments(local), [f"-DTS2_COMPILE_CACHE={exe}",
                                                f"-DTS2_COMPILE_CACHE_DIR={local}\\compile-cache"])


class Tree(unittest.TestCase):
    def setUp(self):
        self.ninja = ROOT / "build" / "CMakeFiles" / "impl-Release.ninja"
        cache = ROOT / "build" / "CMakeCache.txt"
        if not self.ninja.exists() or not re.search(r"^TS2_COMPILE_CACHE:\w+=.+$", cache.read_text(errors="replace"), re.M):
            self.skipTest("no Ninja tree with the cache here")
        text = self.ninja.read_text(errors="replace")
        self.statements = []  # (target, the statement's text)
        for m in STATEMENT.finditer(text):
            self.statements.append((m.group(1), text[m.start():text.index("\n\n", m.start())]))

    def test_difftest_is_left_out(self):
        own = [text for target, text in self.statements if target == "difftest"]
        self.assertTrue(own)
        self.assertEqual([t for t in own if "LAUNCHER" in t or "/d1trimfile" in t], [])

    def test_everything_else_goes_through_it_and_is_trimmed(self):
        rest = [text for target, text in self.statements if target != "difftest"]
        self.assertTrue(rest)
        self.assertEqual([t for t in rest if "ccache" not in t or "/d1trimfile:" not in t], [])


if __name__ == "__main__":
    unittest.main()
