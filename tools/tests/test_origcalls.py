#!/usr/bin/env python3
"""Tests for tools/origcalls.py (the rules for calling original functions from src/).

Run: .venv/Scripts/python -I tools/tests/test_origcalls.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import origcalls as O           # noqa: E402

FUNCS = {"0x100": {"size": 50}, "0x200": {"size": 9}, "0x300": {"size": 7}, "0x400": {"size": 7}}
LIBRARY = {"0x300"}


def entry(rva, src, calls=None, status="verified"):
    e = {"rva": rva, "name": "F" + rva, "status": status, "src": src}
    if calls is not None:
        e["original_calls"] = calls
    return ("G.jsonl", e)


def run(text, entries, path="src/a.cpp"):
    return O.check(O.scan({path: text}), entries, FUNCS, LIBRARY)


class OrigCalls(unittest.TestCase):
    def test_nested_template_argument_and_literal(self):
        text = "OriginalFunction<int(__thiscall *)(std::vector<std::pair<int, int>> *, int), 0x200>()(p, 1);"
        self.assertEqual(O.uses(text), [(1, "0x200")])

    def test_old_form_is_not_a_literal(self):
        self.assertEqual(O.uses("OriginalFunction<void (*)()>(0x200)();"), [(1, None)])

    def test_multiline_arguments(self):
        self.assertEqual(O.uses("OriginalFunction<void (__thiscall *)(void *, int),\n  0x200>()(p, 1);"), [(1, "0x200")])

    def test_declared_call_passes(self):
        p, w = run("OriginalFunction<void (*)(), 0x200>()();", [entry("0x100", "src/a.cpp", ["0x200"])])
        self.assertEqual((p, w), ([], []))

    def test_undeclared_call(self):
        p, _ = run("OriginalFunction<void (*)(), 0x200>()();", [entry("0x100", "src/a.cpp")])
        self.assertTrue(any("not declared" in x for x in p))

    def test_declared_in_another_file_does_not_count(self):
        p, _ = run("OriginalFunction<void (*)(), 0x200>()();", [entry("0x100", "src/b.cpp", ["0x200"])])
        self.assertTrue(any("not declared" in x for x in p))

    def test_stale_declaration(self):
        p, _ = run("int x;", [entry("0x100", "src/a.cpp", ["0x200"])])
        self.assertTrue(any("never calls it" in x for x in p))

    def test_non_literal_rva(self):
        p, _ = run("OriginalFunction<void (*)(), kSomething>()();", [])
        self.assertTrue(any("hex literal" in x for x in p))

    def test_not_a_function_start(self):
        p, _ = run("OriginalFunction<void (*)(), 0x999>()();", [entry("0x100", "src/a.cpp", ["0x999"])])
        self.assertTrue(any("not a function start" in x for x in p))

    def test_library_callee(self):
        p, _ = run("OriginalFunction<void (*)(), 0x300>()();", [entry("0x100", "src/a.cpp", ["0x300"])])
        self.assertTrue(any("library code" in x for x in p))

    def test_wrapping_own_original(self):
        p, _ = run("OriginalFunction<void (*)(), 0x100>()();", [entry("0x100", "src/a.cpp", ["0x100"])])
        self.assertTrue(any("own original" in x for x in p))
        self.assertTrue(any("implemented in this file" in x for x in p))

    def test_callee_replaced_elsewhere_warns(self):
        p, w = run("OriginalFunction<void (*)(), 0x400>()();",
                   [entry("0x100", "src/a.cpp", ["0x400"]), entry("0x400", "src/b.cpp")])
        self.assertEqual(p, [])
        self.assertTrue(any("switch to it" in x for x in w))

    def test_stale_declaration_listing(self):
        entries = [entry("0x100", "src/a.cpp", ["0x400", "0x200"]), entry("0x400", "src/b.cpp"),
                   entry("0x200", "src/c.cpp", status="mapped")]
        lines = O.stale_lines(entries)
        self.assertEqual(len(lines), 1)
        self.assertIn("0x400", lines[0])
        self.assertEqual(O.stale_lines([entry("0x100", "src/a.cpp", ["0x100"])]), [])  # own rva is not "stale"

    def test_mapped_callee_is_fine(self):
        p, w = run("OriginalFunction<void (*)(), 0x400>()();",
                   [entry("0x100", "src/a.cpp", ["0x400"]), entry("0x400", "src/a.cpp", status="mapped")])
        self.assertEqual((p, w), ([], []))


if __name__ == "__main__":
    unittest.main(verbosity=2)
