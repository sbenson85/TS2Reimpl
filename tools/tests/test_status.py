#!/usr/bin/env python3
"""Tests for tools/status.py's "reachable" line (milestone H) on small synthetic call graphs: `replaced` functions
count as done and are descended only through their original_calls; verified ones keep the plain walk; import thunks
(`jmp [IAT]`, found in the image bytes) count as done by rule. Also the --check --group filter.

Run: tools/py -I tools/tests/test_status.py
"""
import contextlib
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import status as S      # noqa: E402
import subsystem_progress as P  # noqa: E402
import tsdb             # noqa: E402


def fn(size, *callees):
    return {"size": size, "callees": list(callees), "thunk": False, "namespace": "Global", "name": "f"}


# root -> R (replaced, declares 0x30) -> 0x20 (only R calls it), 0x30 (declared original call)
# root -> V (verified) -> 0x50 (only V calls it: still reached, the plain walk stays for verified code)
# root -> 0x60 (an import thunk) and 0x70 (library) -> 0x80 (game code under library code)
FUNCS = {
    "0x1": fn(20, "0x10", "0x40", "0x60", "0x70"),
    "0x10": fn(100, "0x20", "0x30"),
    "0x20": fn(1000, "0x21"),
    "0x21": fn(500),
    "0x30": fn(30),
    "0x40": fn(40, "0x50"),
    "0x50": fn(50),
    "0x60": fn(6),
    "0x70": fn(70, "0x80"),
    "0x80": fn(80),
}
STATUS = {
    "0x10": {"rva": "0x10", "status": "replaced", "original_calls": ["0x30"]},
    "0x40": {"rva": "0x40", "status": "verified"},
}
LIBRARY = {"0x70"}
THUNKS = {"0x60"}


def line(status=STATUS, thunks=THUNKS):
    return S.reachable_line(FUNCS, status, LIBRARY, ["0x1"], thunks, targets={})


class ReachableLine(unittest.TestCase):
    def test_replaced_is_done(self):
        L = line()
        self.assertIn("0x10", L["done"])
        self.assertEqual(L["replaced"], {"0x10"})

    def test_replaced_descends_only_through_original_calls(self):
        L = line()
        self.assertNotIn("0x20", L["reach"])     # the contract replacement never calls it
        self.assertNotIn("0x21", L["reach"])
        self.assertIn("0x30", L["reach"])        # declared: still runs
        self.assertNotIn("0x30", L["done"])

    def test_replaced_without_original_calls_is_a_leaf(self):
        status = dict(STATUS, **{"0x10": {"rva": "0x10", "status": "replaced"}})
        self.assertFalse({"0x20", "0x21", "0x30"} & line(status)["reach"])

    def test_original_calls_are_normalised(self):
        status = dict(STATUS, **{"0x10": {"rva": "0x10", "status": "replaced", "original_calls": ["0x0030"]}})
        self.assertIn("0x30", line(status)["reach"])

    def test_verified_keeps_the_plain_walk(self):
        L = line()
        self.assertIn("0x40", L["done"])
        self.assertIn("0x50", L["reach"])        # policy 2: dead callees of verified code stay on the line
        self.assertNotIn("0x50", L["done"])

    def test_reimplemented_is_neither_done_nor_a_stop(self):
        status = dict(STATUS, **{"0x10": {"rva": "0x10", "status": "reimplemented", "original_calls": ["0x30"]}})
        L = line(status)
        self.assertNotIn("0x10", L["done"])
        self.assertIn("0x21", L["reach"])

    def test_import_thunk_is_done_by_rule(self):
        L = line()
        self.assertIn("0x60", L["reach"])
        self.assertIn("0x60", L["done"])
        self.assertEqual(L["thunks"], {"0x60"})
        self.assertNotIn("0x60", line(thunks=set())["done"])

    def test_library_left_out_but_followed(self):
        L = line()
        self.assertNotIn("0x70", L["reach"])
        self.assertIn("0x80", L["reach"])

    def test_totals(self):
        L = line()
        self.assertEqual(L["reach"], {"0x1", "0x10", "0x30", "0x40", "0x50", "0x60", "0x80"})
        self.assertEqual(L["done"], {"0x10", "0x40", "0x60"})
        self.assertEqual(L["done"], L["verified"] | L["replaced"] | L["thunks"] | L["other"])

    def test_live_walk_stops_at_all_of_ours(self):
        ours = {r: e for r, e in STATUS.items()}
        self.assertNotIn("0x50", S.walk(FUNCS, ["0x1"], ours))
        self.assertIn("0x30", S.walk(FUNCS, ["0x1"], ours))


class TinyFolded(unittest.TestCase):
    """Folded leaves under 8 bytes (shared_vf_*: unhookable) count as done once every direct caller in the image is
    verified (notes/decisions.md 2026-10-19)."""
    # root 0x1 -> 0xa (verified) -> f1 f2 f3 f4 f5 f7; root -> 0xb (not verified) -> f2;
    # 0xd (not verified, not reached from the root) -> f7; f5 -> f6
    FUNCS = {
        "0x1": fn(20, "0xa", "0xb"),
        "0xa": fn(50, "0xf1", "0xf2", "0xf3", "0xf4", "0xf5", "0xf7"),
        "0xb": fn(50, "0xf2"),
        "0xd": fn(50, "0xf7"),
        "0xf1": fn(7),
        "0xf2": fn(5),
        "0xf3": fn(8),             # not under 8 bytes
        "0xf4": fn(5),             # not a shared_vf_* body
        "0xf5": fn(7, "0xf6"),     # not a leaf
        "0xf6": fn(30),
        "0xf7": fn(6),
        "0xf8": fn(6),             # no callers at all
    }
    NAMES = [{"rva": r, "name": "shared_vf_" + r[2:]} for r in ("0xf1", "0xf2", "0xf3", "0xf5", "0xf7", "0xf8")]
    STATUS = {"0xa": {"rva": "0xa", "status": "verified"}}

    def line(self, folded):
        return S.reachable_line(self.FUNCS, self.STATUS, set(), ["0x1"], set(), targets={}, folded=folded)

    def test_candidates(self):
        self.assertEqual(S.tiny_folded(self.FUNCS, self.NAMES), {"0xf1", "0xf2", "0xf7", "0xf8"})

    def test_done_only_when_every_caller_is_verified(self):
        L = self.line(S.tiny_folded(self.FUNCS, self.NAMES))
        self.assertEqual(L["folded"], {"0xf1"})
        self.assertIn("0xf1", L["done"])
        self.assertNotIn("0xf2", L["done"])    # 0xb isn't verified
        self.assertNotIn("0xf7", L["done"])    # 0xd isn't verified, though it isn't on the line
        self.assertNotIn("0xf5", L["done"])
        self.assertEqual(L["done"], L["verified"] | L["replaced"] | L["thunks"] | L["folded"] | L["other"])

    def test_no_callers_is_not_done(self):
        self.assertNotIn("0xf8", S.folded_done(self.FUNCS, {"0xf8"}, {"0xa"}))

    def test_caller_verified_later(self):
        status = dict(self.STATUS, **{"0xb": {"rva": "0xb", "status": "verified"}})
        L = S.reachable_line(self.FUNCS, status, set(), ["0x1"], set(), targets={},
                             folded=S.tiny_folded(self.FUNCS, self.NAMES))
        self.assertEqual(L["folded"], {"0xf1", "0xf2"})

    def test_without_the_rule(self):
        L = self.line(frozenset())
        self.assertNotIn("0xf1", L["done"])
        self.assertEqual(L["folded"], set())


class ImportThunks(unittest.TestCase):
    BASE = 0x10000000
    CODE = {
        0x100: b"\xff\x25" + (BASE + 0x9000).to_bytes(4, "little"),   # jmp [IAT slot]
        0x200: b"\xff\x25" + (BASE + 0x5000).to_bytes(4, "little"),   # jmp [a data pointer]: not an import
        0x300: b"\xe9\x00\x00\x00\x00\xcc",                            # jmp rel32
        0x400: b"\xff\x25" + (BASE + 0x9004).to_bytes(4, "little"),   # slot, but Ghidra made it 8 bytes long
    }
    FUNCS = {"0x100": {"size": 6}, "0x200": {"size": 6}, "0x300": {"size": 6}, "0x400": {"size": 8}}

    def test_only_jmp_through_import_slots(self):
        found = tsdb.find_import_thunks(self.FUNCS, lambda rva, n: self.CODE[rva][:n], {0x9000, 0x9004}, self.BASE)
        self.assertEqual(found, {"0x100"})


class GroupFilter(unittest.TestCase):
    ENTRIES = [("A.jsonl", {"rva": "0x1", "src": "src/a.cpp", "test": "tests/a.cpp"}),
               ("B.jsonl", {"rva": "0x2", "src": "src/b.cpp", "test": "tests/b.cpp"})]
    PROBLEMS = ["A.jsonl: 0x1 F: missing ['notes']", "src/a.cpp:12: 0x9 not declared", "tests/a.cpp: x",
                "B.jsonl: 0x2 G: bad", "src/b.cpp:3: 0x9 not declared",
                "no build of our host at C:\\x\\ts2host.exe", "src/hook/trap_table.cpp is out of date: rerun"]

    def test_split(self):
        mine, glob, other = S.group_problems(self.PROBLEMS, self.ENTRIES, "A")
        self.assertEqual(mine, self.PROBLEMS[:3])
        self.assertEqual(glob, self.PROBLEMS[5:])
        self.assertEqual(other, 2)

    def test_hook_table_crc_problems_go_to_the_tables_group(self):
        # hookcrc.check_tables() problems start with the table's path: src/hook/tables/<group>.cpp is that group's.
        problems = ["src/hook/tables/A.cpp: hook A_Create 0x1f4dc0: crc 0x1 of its 64 bytes, the exe's is 0x2",
                    "src/hook/tables/B.cpp: hook B_Get 0x10: checkLen 3: under 5 bytes"]
        mine, glob, other = S.group_problems(problems, self.ENTRIES, "A")
        self.assertEqual((mine, glob, other), (problems[:1], [], 1))


class Absorbed(unittest.TestCase):
    READER = {"rva": "0x10", "status": "verified", "test": "tests/a.cpp"}

    def entry(self, **change):
        return {"rva": "0x20", "status": "reimplemented", "test": "tests/a.cpp", "in_effect": "callers",
                "absorbed_by": ["0x10"], **change}

    def problems(self, e, reader=READER):
        return S.absorbed_problems(e, {reader["rva"]: reader})

    def test_fields(self):
        self.assertEqual(self.problems(self.entry()), [])
        self.assertEqual(self.problems({"rva": "0x20", "status": "verified"}), [])
        self.assertTrue(self.problems(self.entry(status="verified")))
        self.assertTrue(self.problems(self.entry(in_effect="hook")))
        self.assertTrue(self.problems(self.entry(absorbed_by=[])))
        self.assertTrue(self.problems(self.entry(absorbed_by=["0x11"])))
        self.assertTrue(self.problems(self.entry(test="tests/b.cpp")))
        self.assertTrue(self.problems(self.entry(), {**self.READER, "status": "reimplemented"}))
        self.assertTrue(self.problems(self.entry(), {**self.READER, "verify": "match"}))

    def test_their_own_line_outside_verified_and_the_status_table(self):
        funcs = {"0x10": fn(100), "0x20": fn(1000)}
        entries = [("G.jsonl", self.READER), ("H.jsonl", self.entry(test="tests/a.cpp"))]
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            S.print_totals(funcs, entries)
        self.assertNotIn("H.jsonl", out.getvalue())
        self.assertRegex(out.getvalue(), r"bytes of code +0 +0 +0 +0 +100\n")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            S.summary(funcs, {e["rva"]: e for _, e in entries}, list_rvas=False)
        text = out.getvalue()
        self.assertRegex(text, r"absorbed by a template.*1 functions, 1,000 bytes \(tests/a.cpp\)")
        self.assertRegex(text, r"verified: 100 bytes")


class Headline(unittest.TestCase):
    """"No longer needs decompiling": each band, what it leaves out, and that the verified measure doesn't move."""
    FUNCS = {r: fn(size) for r, size in (("0x1", 1), ("0x2", 2), ("0x4", 4), ("0x8", 8), ("0x10", 16), ("0x20", 32),
                                         ("0x40", 64), ("0x80", 128), ("0x100", 256))}
    STATUS = {"0x1": {"status": "verified"}, "0x2": {"status": "verified", "verify": "match"},
              "0x4": {"status": "reimplemented", "absorbed_by": ["0x1"], "test": "t"}, "0x8": {"status": "replaced"},
              "0x10": {"status": "trace-verified"}, "0x20": {"status": "reimplemented"}, "0x40": {"status": "mapped"},
              "0x80": {"status": "verified"}, "0x100": {"status": "verified"}, "0x999": {"status": "verified"}}

    def result(self, status=None):
        return S.headline(self.FUNCS, self.STATUS if status is None else status, {"0x80"}, {"0x100"})

    def test_bands_and_exclusions(self):
        h = self.result()
        self.assertEqual(h["bands"], {"verified": 3, "absorbed": 4, "replaced": 8, "dead": 0, "trace-verified": 16})
        self.assertEqual((h["done"], h["total"]), (31, 127))
        self.assertEqual((h["bare_reimplemented"], h["rest"]), (32, 64))
        self.assertEqual((h["library"], h["third_party"]), (128, 256))

    def test_verified_wins_a_band_and_only_status_verified_is_verified(self):
        self.assertEqual(tsdb.headline_band({"status": "verified", "absorbed_by": ["0x1"]}), "verified")
        for status in ("reimplemented", "mapped"):
            self.assertIsNone(tsdb.headline_band({"status": status}))

    def test_other_bands_never_move_the_verified_measure(self):
        only = {r: e for r, e in self.STATUS.items() if e["status"] == "verified"}
        out = [io.StringIO(), io.StringIO()]
        funcs = dict(self.FUNCS)
        for buffer, status in zip(out, (only, self.STATUS)):
            with contextlib.redirect_stdout(buffer):
                S.summary(funcs, {r: dict(e, rva=r) for r, e in status.items() if r in funcs}, list_rvas=False)
        pick = re.compile(r"^  verified: ([\d,]+) bytes", re.M)
        self.assertEqual(pick.search(out[0].getvalue())[1], pick.search(out[1].getvalue())[1])

    def test_the_real_measures_are_the_status_field_alone(self):
        """On the real db: both verified figures are the plain sums of status == verified, computed here apart from
        status.py, and the headline's verified band is EA's verified figure, not a new number."""
        funcs = tsdb.functions()
        sizes = {f["rva"]: f["size"] for f in funcs}
        library = tsdb.library_rvas()
        third = {e["rva"] for e in tsdb.jsonl("subsystems.jsonl") if e["subsystem"] == "third-party"}
        rvas = {e["rva"] for _, e in tsdb.status_entries() if e["status"] == "verified"} & set(sizes)
        game = sum(sizes[r] for r in rvas - library)
        ea = sum(sizes[r] for r in rvas - library - third)
        text = subprocess.run([sys.executable, "-I", str(Path(S.__file__)), "--metrics"], capture_output=True, text=True,
                              check=True).stdout
        number = lambda pattern: int(re.search(pattern, text)[1].replace(",", ""))
        self.assertEqual(number(r"(?m)^  verified: ([\d,]+) bytes"), game)
        self.assertEqual(number(r"EA's own code: [\d,]+ bytes, verified ([\d,]+)"), ea)
        self.assertEqual(number(r"no longer needs decompiling.*: verified ([\d,]+),"), ea)


class ProgressPage(unittest.TestCase):
    """subsystem_progress.py's snapshot: the bands beside an unchanged verified series."""

    def test_snapshot(self):
        lines = [json.dumps(e) for e in ({"rva": "0x1", "status": "verified"},
                                         {"rva": "0x2", "status": "reimplemented", "absorbed_by": ["0x1"]},
                                         {"rva": "0x4", "status": "reimplemented"}, {"rva": "0x8", "status": "replaced"},
                                         {"rva": "0x10", "status": "trace-verified"}, {"rva": "0x20", "status": "mapped"},
                                         {"rva": "0x40", "status": "verified"})]     # 0x40: library, not in the universe
        tar = io.BytesIO()
        with tarfile.open(fileobj=tar, mode="w") as archive:
            data = "\n".join(lines).encode()
            info = tarfile.TarInfo("db/status/x.jsonl")
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
        universe = {r: {"size": int(r, 16)} for r in ("0x1", "0x2", "0x4", "0x8", "0x10", "0x20")}
        subs = {r: {"subsystem": "a"} for r in universe}
        with mock.patch.object(P, "git", return_value=tar.getvalue()), mock.patch.object(P, "stamp", return_value="t"):
            got = P.snapshot("c", "d", universe, subs)
        self.assertEqual(got["verified"], {"a": 1})
        self.assertEqual(got["bands"], {"verified": 1, "absorbed": 2, "replaced": 8, "trace-verified": 16})
        self.assertEqual(got["no_decompile"], {"a": 27})


if __name__ == "__main__":
    unittest.main()
