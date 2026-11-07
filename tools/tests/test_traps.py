#!/usr/bin/env python3
"""Tests for tools/traps.py (the JSON-aware trap editor for "in_effect": "callers" status entries).

Run: .venv/Scripts/python -I tools/tests/test_traps.py
"""
import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
import traps as TR  # noqa: E402
import test_callers as TC  # noqa: E402  (the synthetic image and db)

NOTE = 'quotes "trap": "Hooks.Fake=1" and \\u00e9 é in a note'


def entry(rva, trap, **extra):
    e = {"rva": rva, "name": f"F_{rva}", "status": "verified", "src": "src/a.cpp", "test": "tests/a.cpp",
         "notes": NOTE, "in_effect": "callers", "callers_check": "callers.py v1 2026-10-21", "trap": trap}
    e.update(extra)
    return e


class SetField(unittest.TestCase):
    def test_replaces_only_the_value(self):
        line = json.dumps(entry("0x10", "Hooks.B=1"), ensure_ascii=False) + "\n"
        out = TR.set_field(line, "trap", "Hooks.A=1&Hooks.B=1")
        self.assertEqual(out, line.replace('"trap": "Hooks.B=1"', '"trap": "Hooks.A=1&Hooks.B=1"'))
        self.assertIn('\\"trap\\": \\"Hooks.Fake=1\\"', out)  # the note's quoted text untouched
        self.assertIn("é", out)  # non-ASCII kept as written
        self.assertTrue(out.endswith("\n"))

    def test_key_inside_a_note_is_not_the_field(self):
        # the note comes first and quotes the key: the walk must skip it (a regex on '"trap": "' would hit it)
        line = '{"notes": "x \\"trap\\": \\"Hooks.Fake=1\\" y", "trap": "Hooks.B=1"}'
        out = TR.set_field(line, "trap", "Hooks.C=1")
        self.assertEqual(json.loads(out), {"notes": 'x "trap": "Hooks.Fake=1" y', "trap": "Hooks.C=1"})

    def test_missing_key_is_appended_and_order_kept(self):
        line = '{"rva": "0x10", "notes": "n"}\r\n'
        out = TR.set_field(line, "trap", "Hooks.A=1")
        self.assertEqual(out, '{"rva": "0x10", "notes": "n", "trap": "Hooks.A=1"}\r\n')
        self.assertEqual(TR.set_field("{}", "trap", "Hooks.A=1"), '{"trap": "Hooks.A=1"}')

    def test_compact_separators_kept(self):
        line = '{"rva":"0x10","trap":"Hooks.B=1","x":[1,{"trap":"no"}]}'
        out = TR.set_field(line, "trap", "Hooks.A=1")
        self.assertEqual(out, '{"rva":"0x10","trap":"Hooks.A=1","x":[1,{"trap":"no"}]}')  # nested key untouched


class Terms(unittest.TestCase):
    def test_add_sorts_and_keeps_other_terms_first(self):
        self.assertEqual(TR.add_hook("Hooks.b=1&Hooks.Z=1", "a"), "Hooks.Z=1&Hooks.a=1&Hooks.b=1")
        self.assertEqual(TR.add_hook("Startup.OwnStart=1,2", "H"), "Startup.OwnStart=1,2&Hooks.H=1")
        self.assertEqual(TR.add_hook(None, "H"), "Hooks.H=1")
        self.assertEqual(TR.add_hook("Hooks.H=1", "H"), "Hooks.H=1")  # already there: unchanged

    def test_remove(self):
        self.assertEqual(TR.remove_hook("Hooks.A=1&Hooks.B=1", "A"), "Hooks.B=1")
        self.assertEqual(TR.remove_hook("Hooks.A=1&Hooks.B=1", "C"), "Hooks.A=1&Hooks.B=1")
        self.assertIsNone(TR.remove_hook("Hooks.A=1", "A"))  # nothing left to arm on

    def test_sync(self):
        self.assertEqual(TR.sync_trap("Hooks.Old=1&Hooks.B=1", ["B", "A"]), "Hooks.A=1&Hooks.B=1")
        self.assertEqual(TR.sync_trap("X.Y=1&Hooks.Old=1", ["N"]), "X.Y=1&Hooks.N=1")
        self.assertIsNone(TR.sync_trap("Startup.OwnStart=1,2", ["N"]))  # no Hooks. terms: left alone
        self.assertIsNone(TR.sync_trap("Hooks.Old=1", []))  # rests on no hook: left alone

    def test_hook_term(self):
        for t in ("X_1", "Hooks.X_1", "Hooks.X_1=1"):
            self.assertEqual(TR.hook_term(t), "X_1")
        with self.assertRaises(SystemExit):
            TR.hook_term("Hooks.X=2")


class Files(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "db/status").mkdir(parents=True)
        (self.root / "src/hook/tables").mkdir(parents=True)
        (self.root / "src/hook/tables/G.cpp").write_text('{"H_1030", 0x1030, 32, 0x0, x},\n{"New", 0x1060, 6, 0x0, y},\n')
        self.lines = [json.dumps(entry("0x1010", "Hooks.H_1030=1&Hooks.Stale=1"), ensure_ascii=False),
                      json.dumps(entry("0x1020", "Startup.OwnStart=1,2")),
                      json.dumps({"rva": "0x1030", "name": "hooked", "status": "verified", "notes": NOTE}),
                      json.dumps(entry("0x1050", "Hooks.H_1030=1"))]
        self.path = self.root / "db/status/G.jsonl"
        self.path.write_bytes(("\n".join(self.lines) + "\n").encode("utf-8"))

    def tearDown(self):
        self.tmp.cleanup()

    def run_main(self, *argv):
        out = io.StringIO()
        with mock.patch.object(TR.tsdb, "ROOT", self.root), contextlib.redirect_stdout(out):
            TR.main(list(argv))
        return out.getvalue()

    def read(self):
        return self.path.read_bytes().decode("utf-8").split("\n")

    def test_add_where_and_rva(self):
        self.run_main("--group", "G", "--add", "Hooks.New=1", "--where", "Stale")
        got = self.read()
        self.assertEqual(json.loads(got[0])["trap"], "Hooks.H_1030=1&Hooks.New=1&Hooks.Stale=1")
        self.assertEqual(got[1:], self.lines[1:] + [""])  # the others byte for byte
        self.run_main("--group", "G", "--add", "New", "--rva", "0x1020")
        self.assertEqual(json.loads(self.read()[1])["trap"], "Startup.OwnStart=1,2&Hooks.New=1")
        self.assertEqual(self.read()[3], self.lines[3])

    def test_add_refuses_an_unknown_hook_and_dry_run_writes_nothing(self):
        with self.assertRaises(SystemExit):
            self.run_main("--group", "G", "--add", "Nope")
        before = self.path.read_bytes()
        out = self.run_main("--group", "G", "--add", "New", "--dry-run")
        self.assertIn("3 trap(s) would change", out)
        self.assertEqual(self.path.read_bytes(), before)

    def test_remove_keeps_a_last_term(self):
        out = self.run_main("--group", "G", "--remove", "H_1030")
        got = self.read()
        self.assertEqual(json.loads(got[0])["trap"], "Hooks.Stale=1")
        self.assertEqual(got[3], self.lines[3])  # its only term: left, with a note
        self.assertIn("only term", out)

    def test_sync_sets_the_hooks_the_check_rests_on(self):
        img, db = TC.make_image(), TC.make_db(hooked={0x1030})  # T = 0x1010 rests on F3's hook (H_1030)
        with mock.patch.object(TR.callers, "Image", lambda: img), \
                mock.patch.object(TR.callers, "Db", lambda binary=None: db):
            out = self.run_main("--sync", "--group", "G")
        got = self.read()
        self.assertEqual(json.loads(got[0])["trap"], "Hooks.H_1030=1")
        self.assertEqual(got[1], self.lines[1])  # Startup trap: not a Hooks. trap, left
        self.assertEqual(got[3], self.lines[3])  # 0x1050 rests on no hook: left, with a note
        self.assertIn("rests on no hook", out)
        self.assertEqual(json.loads(got[0])["notes"], NOTE)

    def test_sync_uses_the_checks_db_and_refuses_a_stale_build(self):
        # The hooks depend on which vtables our build holds, so --sync builds the Db status.py --check does (the binary
        # scan, not binary=False: lane LP found the two disagreeing) and stops when that scan is missing or stale.
        img, db = TC.make_image(), TC.make_db(hooked={0x1030})
        asked = []

        def make(binary=None):
            asked.append(binary)
            return db
        before = self.path.read_bytes()
        db.binary_note = "build/Release/ts2host.exe is older than src/ or host/"
        with mock.patch.object(TR.callers, "Image", lambda: img), mock.patch.object(TR.callers, "Db", make):
            with self.assertRaises(SystemExit) as stop:
                self.run_main("--sync", "--group", "G")
        self.assertIn("older than src/", str(stop.exception))
        self.assertEqual(asked, [None])
        self.assertEqual(self.path.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
