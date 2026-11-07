#!/usr/bin/env python3
"""Tests for tools/guard.py's check of a branch's db/missed-functions.jsonl change (manual entries allowed).

Run: .venv/Scripts/python -I tools/tests/test_guard_missed.py
"""
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import guard as G  # noqa: E402


def line(rva, kind="missed", size=25, source=None, **extra):
    e = {"rva": rva, "size": size, "kind": kind, "callers": [], "callees": []}
    if source:
        e["source"] = source
    e.update(extra)
    return json.dumps(e)


SCAN = [line("0x100"), line("0x200", kind="resized", size=90)]
MANUAL = line("0x300", source="manual: a factory (lane X)")


class MissedFunctions(unittest.TestCase):
    def test_adding_a_manual_entry_is_a_warning(self):
        errors, warnings = G.missed_functions_change("\n".join(SCAN), "\n".join(SCAN + [MANUAL]))
        self.assertEqual(errors, [])
        self.assertEqual(len(warnings), 1)
        self.assertIn("adds manual entry 0x300", warnings[0])
        self.assertIn("a factory (lane X)", warnings[0])

    def test_unchanged_is_silent(self):
        self.assertEqual(G.missed_functions_change("\n".join(SCAN + [MANUAL]), "\n".join(SCAN + [MANUAL])), ([], []))

    def test_removing_a_manual_entry_is_an_error(self):
        errors, _ = G.missed_functions_change("\n".join(SCAN + [MANUAL]), "\n".join(SCAN))
        self.assertEqual(len(errors), 1)
        self.assertIn("removes manual entry 0x300", errors[0])

    def test_changing_a_manual_entry_is_a_warning(self):
        changed = line("0x300", size=141, source="manual: a factory (lane X)")
        errors, warnings = G.missed_functions_change(MANUAL, changed)
        self.assertEqual(errors, [])
        self.assertIn("changes manual entry 0x300", warnings[0])
        self.assertIn("141 bytes", warnings[0])

    def test_regenerated_scan_entries_are_one_warning(self):
        new = [line("0x100", size=20), line("0x400"), MANUAL]  # 0x100 resized, 0x200 gone, 0x400 new
        errors, warnings = G.missed_functions_change("\n".join(SCAN), "\n".join(new))
        self.assertEqual(errors, [])
        scan = [w for w in warnings if "scan entries" in w]
        self.assertEqual(len(scan), 1)
        self.assertIn("3 scan entries differ (0x100, 0x200, 0x400", scan[0])

    def test_a_broken_line_is_an_error(self):
        errors, _ = G.missed_functions_change("", "not json\n" + MANUAL)
        self.assertEqual(len(errors), 1)
        self.assertIn("line 1", errors[0])


if __name__ == "__main__":
    unittest.main()
