#!/usr/bin/env python3
"""Tests for tools/boundaries.py's pure parts: the actions written for tools/ghidra/ApplyBoundaries.java from lane HW's
boundary rows, and the comparison and summary of the script's before/after function lists (no Ghidra).

Run: tools/py -I tools/tests/test_boundaries.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import boundaries  # noqa: E402

ROWS = [
    {"rva": "0x7d6eb", "fix": "resize", "ghidra_size": 10, "size": 161, "evidence": "_EH_prolog caller (missedfuncs)"},
    {"rva": "0x255740", "fix": "resize", "ghidra_size": None, "size": 58, "evidence": "library body std::allocator"},
    {"rva": "0x824940", "fix": "resize", "ghidra_size": 121, "size": 175, "evidence": "library body _dtol3"},
    {"rva": "0x824988", "fix": "remove (part of the function at 0x824940)", "ghidra_size": 54, "offset": 72},
    {"rva": "0x848b5b", "fix": "create", "ghidra_size": None, "size": 167, "evidence": "library body _CIcos_default"},
]


class Actions(unittest.TestCase):
    def test_kinds(self):
        acts = boundaries.actions(ROWS)
        self.assertEqual(acts[0], ("returns", "0x8248b0", ""))
        self.assertEqual(acts[1:], [
            ("expect", "0x7d6eb", "161"),     # an _EH_prolog caller: the repaired flow decides, checked only
            ("resize", "0x255740", "58"),     # library body, no Ghidra function yet: the script creates it
            ("resize", "0x824940", "175"),
            ("remove", "0x824988", "0x824940"),
            ("create", "0x848b5b", "167"),
        ])

    def test_eh_caller_without_function_is_set(self):
        # No Ghidra function to repair the flow of: the size is set (the script creates the function).
        row = {"rva": "0x1cb89d", "fix": "resize", "ghidra_size": None, "size": 115, "evidence": "_EH_prolog caller"}
        self.assertEqual(boundaries.actions([row], returns=()), [("resize", "0x1cb89d", "115")])

    def test_no_returns(self):
        self.assertEqual(boundaries.actions(ROWS[:1], returns=()), [("expect", "0x7d6eb", "161")])

    def test_unknown_fix_rejected(self):
        with self.assertRaises(ValueError):
            boundaries.actions([{"rva": "0x10", "fix": "split"}])

    def test_lines(self):
        text = boundaries.action_lines(boundaries.actions(ROWS[3:4]))
        self.assertEqual(text, "returns\t0x8248b0\nremove\t0x824988\t0x824940\n")


class Compare(unittest.TestCase):
    BEFORE = "0x10\t10\tFUN_a\n0x20\t5\tFUN_b\n0x30\t40\tFUN_c\n0x80\t8\tkeep\n"
    AFTER = "0x10\t100\tFUN_a\n0x30\t16\tFUN_c\n0x80\t8\tkeep\n0x90\t20\tFUN_new\n"

    def test_read_list(self):
        self.assertEqual(boundaries.read_list(self.BEFORE)["0x20"], (5, "FUN_b"))

    def test_compare(self):
        c = boundaries.compare(boundaries.read_list(self.BEFORE), boundaries.read_list(self.AFTER))
        self.assertEqual(c["removed"], [("0x20", 5)])
        self.assertEqual(c["created"], [("0x90", 20)])
        self.assertEqual(c["resized"], [("0x10", 10, 100), ("0x30", 40, 16)])

    def test_compare_sorts_numerically(self):
        c = boundaries.compare({"0x9": (1, ""), "0x10": (1, "")}, {})
        self.assertEqual([r for r, _ in c["removed"]], ["0x9", "0x10"])


class Summary(unittest.TestCase):
    def test_counts_and_problems(self):
        acts = [("returns", "0x8248b0", ""), ("expect", "0x10", "100"), ("remove", "0x20", "0x10"),
                ("resize", "0x30", "16"), ("remove", "0x99", "0x30")]
        results = boundaries.read_results(
            "returns\t0x8248b0\tdone\tcallers 1\n"
            "expect\t0x10\tagree\tsize 100\n"
            "remove\t0x20\tdone\tsize 5\n"
            "resize\t0x30\tresized\tsize 40 -> 16\n"
            "remove\t0x99\tabsent\tno function there\n"
            "orphan\t0x30\tleft\tcode in no function: 0x40+24 (24 B code)\n")
        before = boundaries.read_list(Compare.BEFORE)
        after = boundaries.read_list(Compare.AFTER)
        lines, report = boundaries.summary(acts, results, boundaries.compare(before, after))
        text = "\n".join(lines)
        self.assertIn("1 removed (5 B), 1 created (20 B), 2 resized (1 grown by 90 B, 1 shrunk by 24 B)", text)
        self.assertIn("resized without an action of their own: 0", text)
        # An absent piece is no problem (the db listed a function Ghidra never had); an orphan is reported apart.
        self.assertEqual(report["problems"], [])
        self.assertEqual(len(report["orphans"]), 1)
        self.assertIn("resized functions that left code in no function: 1", text)

    def test_unlisted_resize_and_problems(self):
        acts = [("resize", "0x30", "16")]
        results = [("resize", "0x30", "conflict", "the body would overlap 0x34"),
                   ("expect", "0x50", "differs", "size 9 (0x50+9), expected 12; missing from the body: 3 B code in no "
                    "function, 0 B in other functions, 0 B no code; body beyond: 0 B")]
        change = {"removed": [], "created": [], "resized": [("0x10", 10, 100)]}
        lines, report = boundaries.summary(acts, results, change)
        self.assertEqual(report["resized_unlisted"], [("0x10", 10, 100)])
        self.assertEqual([p[2] for p in report["problems"]], ["conflict", "differs"])
        self.assertIn("expect differs, by what the flow body lacks: free 1", "\n".join(lines))


class DiffersKind(unittest.TestCase):
    def detail(self, free, other, none, beyond):
        return (f"size 1 (0x1+1), expected 2; missing from the body: {free} B code in no function, {other} B in other "
                f"functions{' (0x99)' if other else ''}, {none} B no code; body beyond: {beyond} B")

    def test_kinds(self):
        self.assertEqual(boundaries.differs_kind(self.detail(0, 0, 17, 0)), "none")
        self.assertEqual(boundaries.differs_kind(self.detail(5, 0, 3, 0)), "free+none")
        self.assertEqual(boundaries.differs_kind(self.detail(0, 4, 0, 0)), "other")
        self.assertEqual(boundaries.differs_kind(self.detail(0, 0, 0, 1)), "beyond")
        self.assertEqual(boundaries.differs_kind(self.detail(0, 0, 0, 0)), "ranges")
        self.assertEqual(boundaries.differs_kind("garbled"), "?")


if __name__ == "__main__":
    unittest.main()
