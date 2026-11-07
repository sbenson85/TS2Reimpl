#!/usr/bin/env python3
"""Tests for tools/simtrace.py (SimAntics trace reading and comparison).

Run: .venv/Scripts/python -I tools/tests/test_simtrace.py
"""
import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import simtrace as S            # noqa: E402

LINE = "{tick} obj {obj} vt dd34ac op 2 -> {res} bhav {bhav} node {node} t/f 1/fffc ops {pv} 0100 0000 0005 190a 0000 0000\n"


def pv(base):
    # The parameter object's vtable (RVA 0xdc941c) as little-endian bytes, split like the host writes them.
    raw = (base + 0xdc941c).to_bytes(4, "little").hex()
    return raw[:4] + " " + raw[4:]


def trace(base, rows):
    text = f"image {base:08X}\nstart 10 at frame 1\n"
    for tick, obj, res, bhav, node in rows:
        text += LINE.format(tick=tick, obj=obj, res=res, bhav=bhav, node=node, pv=pv(base))
    f = tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False)
    f.write(text)
    f.close()
    return f.name


def run_diff(a, b):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        status = S.diff(a, b, 2)
    return status, out.getvalue()


class SimTrace(unittest.TestCase):
    def test_same_behaviour_other_pointers_and_ticks_match(self):
        a = trace(0x02000000, [(5, "AAAA0000", 1, "2010", "0"), (5, "BBBB0000", 0, "1000", "3")])
        # Other image base, object addresses, ticks and high words of the 16-bit fields.
        b = trace(0x03000000, [(9, "CCCC0000", 1, "7b2010", "4294901760"), (9, "DDDD0000", 0, "c71000", "3")])
        status, text = run_diff(a, b)
        self.assertEqual(status, 0, text)
        self.assertIn("match in order", text)

    def test_result_difference_found(self):
        a = trace(0x02000000, [(5, "AAAA0000", 1, "2010", "0"), (5, "BBBB0000", 0, "1000", "3")])
        b = trace(0x02000000, [(5, "AAAA0000", 1, "2010", "0"), (5, "BBBB0000", 1, "1000", "3")])
        status, text = run_diff(a, b)
        self.assertEqual(status, 1)
        self.assertIn("first difference in order at primitive 1", text)

    def test_objects_numbered_by_first_appearance(self):
        a = trace(0x02000000, [(5, "AAAA0000", 1, "2010", "0"), (5, "BBBB0000", 0, "1000", "3")])
        b = trace(0x02000000, [(5, "BBBB0000", 1, "2010", "0"), (5, "AAAA0000", 0, "1000", "3")])
        self.assertEqual(run_diff(a, b)[0], 0)


    def test_objects_keyed_by_frame_ids(self):
        # Same objects (ids 0x3b2, 0x1a) at other addresses: matched by id, not by first appearance.
        a = trace(0x02000000, [(5, "AAAA0000", 1, "2010", "0"), (5, "BBBB0000", 0, "1000", "3")])
        text = open(a).read().replace("obj AAAA0000 vt dd34ac op 2 -> 1 bhav", "obj AAAA0000 vt dd34ac op 2 -> 1 ids 03b203b2 bhav")
        text = text.replace("obj BBBB0000 vt dd34ac op 2 -> 0 bhav", "obj BBBB0000 vt dd34ac op 2 -> 0 ids 001a001a bhav")
        open(a, "w").write(text)
        b = trace(0x02000000, [(5, "CCCC0000", 0, "1000", "3"), (5, "DDDD0000", 1, "2010", "0")])
        text = open(b).read().replace("obj CCCC0000 vt dd34ac op 2 -> 0 bhav", "obj CCCC0000 vt dd34ac op 2 -> 0 ids 001a001a bhav")
        text = text.replace("obj DDDD0000 vt dd34ac op 2 -> 1 bhav", "obj DDDD0000 vt dd34ac op 2 -> 1 ids 03b203b2 bhav")
        open(b, "w").write(text)
        status, out = run_diff(a, b)
        self.assertIn("first difference in order at primitive 0", out)  # the order differs
        self.assertIn("per object: 2 match", out)


    def test_verify_fails_only_differences_before_the_noise_fork(self):
        a, a_bad = (3, "AAAA0000", 1, "2010", "0"), (3, "AAAA0000", 0, "2010", "0")
        b0, b1 = (5, "BBBB0000", 0, "1000", "3"), (5, "BBBB0000", 1, "1000", "3")
        c0, c1 = (5, "CCCC0000", 1, "1001", "1"), (5, "CCCC0000", 0, "1001", "1")
        d0, d1 = (9, "DDDD0000", 1, "1002", "2"), (9, "DDDD0000", 0, "1002", "2")
        # The references disagree on objects 1 and 2 (the noise) at tick 5.
        r1, r2 = trace(0x02000000, [a, b0, c0, d0]), trace(0x02000000, [a, b1, c1, d0])
        ok = trace(0x03000000, [a, b0, c0, d0])
        # Noise on a combination no reference took, and object 3 differs after that (tick 9): passes.
        forked = trace(0x02000000, [a, b1, c0, d1])
        # Object 0 differs at tick 3, before any noise difference: fails.
        bad = trace(0x02000000, [a_bad, b1, c0, d0])
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(S.verify([r1, r2], [ok, forked]), 0)
            self.assertEqual(S.verify([r1, r2], [ok]), 1)   # one passing run is not enough
            self.assertEqual(S.verify([r1, r2], [ok, forked, bad]), 1)
        text = out.getvalue()
        self.assertIn("checked up to tick 5; 0 objects differ beyond the noise; 1 more after the noise fork (3)", text)
        self.assertIn("1 objects differ beyond the noise: 0", text)


    def test_values_compared_when_both_have_them_and_dropped_otherwise(self):
        row = (5, "AAAA0000", 1, "2010", "0")
        a, b, old = trace(0x02000000, [row]), trace(0x02000000, [row]), trace(0x02000000, [row])
        for path, vals in ((a, " vals 19:0=5"), (b, " vals 19:0=6")):
            text = open(path).read().replace(" 0000 0000\n", " 0000 0000" + vals + "\n")
            open(path, "w").write(text)
        self.assertEqual(run_diff(a, b)[0], 1)      # the value differs
        self.assertEqual(run_diff(a, old)[0], 0)    # the old trace has no values: compared without


if __name__ == "__main__":
    unittest.main(verbosity=2)
