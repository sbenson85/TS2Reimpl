#!/usr/bin/env python3
"""Tests for tools/gmdc_compare.py on synthetic logs.

Run: tools/py -I tools/tests/test_gmdc_compare.py
"""
import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import gmdc_compare as C  # noqa: E402


def summary(seed):
    parts = ["%s%d:%016x" % (n, seed, seed * 7 + i) for i, n in enumerate("ELGTNSB")]
    return " ".join(parts) + " all:%016x" % (seed * 13)


def expected_line(pkg, inst, grp, res, seed):
    return "  gmdc-expected %s %08x %08x %08x %s" % (pkg, inst, grp, res, summary(seed))


def host_line(inst, grp, res, seed):
    return "[12:00:01.234] gmdc-expected - %08x %08x %08x %s" % (inst, grp, res, summary(seed))


def run(host, expected, *extra):
    with tempfile.TemporaryDirectory() as d:
        hp, ep = os.path.join(d, "h.log"), os.path.join(d, "e.txt")
        open(hp, "w").write("\n".join(host) + "\n")
        open(ep, "w").write("\n".join(expected) + "\n")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = C.main([hp, ep, *extra])
    return code, out.getvalue()


class CompareTest(unittest.TestCase):
    def test_all_cases(self):
        exp = [
            "noise line",
            expected_line("A.package", 1, 0x1c0532fa, 10, 1),
            expected_line("A.package", 2, 0x1c0532fa, 11, 2),
            expected_line("B.package", 3, 0x1c0532fa, 12, 3),
            expected_line("B.package", 4, 0x1c0532fa, 13, 4),
        ]
        host = [
            "[12:00:00] unrelated",
            host_line(1, 0x1c0532fa, 10, 1),  # matches
            host_line(1, 0x1c0532fa, 10, 1),  # same decode again: still one match
            host_line(2, 0x1c0532fa, 11, 99),  # differs
            host_line(3, 0x1c0532fa, 12, 3),  # matches
            host_line(5, 0x1c0532fa, 14, 5),  # only host
        ]
        code, out = run(host, exp)
        self.assertEqual(code, 1)
        self.assertIn("matched 2, differing 1, only-expected 1, only-host 1", out)
        self.assertIn("DIFF instance 00000002", out)
        self.assertIn("only-expected instance 00000004", out)
        self.assertIn("only-host instance 00000005", out)

    def test_clean(self):
        exp = [expected_line("A.package", 1, 2, 3, 1)]
        code, out = run([host_line(1, 2, 3, 1)], exp)
        self.assertEqual(code, 0)
        self.assertIn("matched 1, differing 0, only-expected 0, only-host 0", out)

    def test_disagreeing_decodes_differ(self):
        exp = [expected_line("A.package", 1, 2, 3, 1)]
        code, out = run([host_line(1, 2, 3, 1), host_line(1, 2, 3, 2)], exp)
        self.assertEqual(code, 1)
        self.assertIn("differing 1", out)

    def test_nothing_in_common_fails(self):
        code, out = run([host_line(1, 2, 3, 1)], [expected_line("A", 9, 9, 9, 1)])
        self.assertEqual(code, 1)
        self.assertIn("matched 0", out)


if __name__ == "__main__":
    unittest.main()
