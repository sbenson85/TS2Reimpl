#!/usr/bin/env python3
"""Tests for tools/statusrecord.py (status.py --record / --brief) and guard.py's collapsed callers warning.

Run: .venv/Scripts/python -I -m unittest discover -s tools/tests -p "test_statusrecord.py"
The fast tests use made-up entries. RealGroupTests drafts two merged groups from the repo (CompGeomTriangleSet from
scratch, cGZRegistry from its entries' names) and checks the drafts against the hand-written entries' fields apart
from notes; they read the original image from local/ and take about a minute.
"""
import argparse
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import guard            # noqa: E402
import statusrecord as R  # noqa: E402
import tsdb             # noqa: E402

FIELDS = ("status", "src", "test", "original_calls", "in_effect", "trap")


def entry(rva, **kw):
    e = {"rva": f"{rva:#x}", "name": f"C::f{rva:x}", "status": "verified", "src": "src/a.cpp", "test": "tests/a.cpp",
         "notes": "A_Test: hand-written."}
    e.update(kw)
    return e


def lines_of(*entries):
    return [json.dumps(e) + "\n" for e in entries]


class HelperTests(unittest.TestCase):
    def test_split_name(self):
        self.assertEqual(R.split_name("nGZ::cTriangleSet<float>::Build"), ("cTriangleSet", "Build"))
        self.assertEqual(R.split_name("cGZRegistry::Find (const) / cGZRegistry::Find2"), ("cGZRegistry", "Find"))
        self.assertEqual(R.split_name("FreeFunction"), ("", "FreeFunction"))
        self.assertEqual(R.split_name(""), ("", ""))

    def test_mutate_log_last_run_only(self):
        log = ("clean run passed\n 1. killed   (fail, 3s) old run\nMUTATE DONE\n"
               "clean run passed\n"
               " 1. killed   (fail, 3s) Build: flip a compare <- CompGeomTriangleSet_Data\n"
               " 2. SURVIVED (pass, 4s) Clone: drop a copy\n"
               " 3. [json 2] INVALID  (build error, 1s) Boxes: bad edit\n"
               "MUTATE DONE\n")
        results = R.parse_mutate_log(log)
        self.assertEqual([r["verdict"] for r in results], ["killed", "SURVIVED", "INVALID"])
        self.assertEqual(results[0]["by"], "CompGeomTriangleSet_Data")
        self.assertEqual(results[1]["by"], None)
        self.assertEqual(R.mutant_summary(results),
                         "Mutants: 1 killed, 1 survived (Clone: drop a copy), 1 invalid (Boxes: bad edit).")
        self.assertEqual(R.mutant_summary([]), "")

    def test_class_own_real_class(self):
        name, own = R.class_own("cCompactorBuilder")
        self.assertEqual(name, "nGZSceneGraph::cCompactorBuilder")
        self.assertIn(0xa838c0, own)        # its deleting destructor: a slot only its vtable holds
        self.assertIn(0xa83930, own)        # Create: named after the class
        self.assertIn(0xa837e0, own)        # its constructor
        self.assertNotIn(0xa45530, own)     # the base cGeometryBuilder's Release, inherited into its vtable
        self.assertNotIn(0xa88230, own)     # a sibling class's QueryInterface
        with self.assertRaises(ValueError):
            R.class_own("NoSuchClassAnywhere")

    def test_hook_rows_real_table(self):
        rows = R.hook_rows("CompGeomTriangleSet")
        self.assertTrue(rows)
        hand = {int(e["rva"], 16) for f, e in tsdb.status_entries() if f == "CompGeomTriangleSet.jsonl"
                and e.get("in_effect") != "callers"}
        self.assertEqual({r for _, r, _ in rows}, hand)
        self.assertTrue(all(q for _, _, q in rows))
        self.assertEqual(R.hook_rows("NoSuchGroup"), [])


class MergeTests(unittest.TestCase):
    def test_new_entries_appended_and_notes_kept(self):
        old = lines_of(entry(0x10))
        draft = {0x10: entry(0x10, notes="A_Test: TODO"), 0x20: entry(0x20, notes="A_Test: TODO")}
        lines, added, updated, kept = R.merge(old, draft)
        self.assertEqual(added, [0x20])
        self.assertEqual(updated, {})
        self.assertEqual(lines[0], old[0])  # byte for byte: the hand notes stay
        self.assertEqual(json.loads(lines[1])["rva"], "0x20")

    def test_trap_refreshed_with_its_check_date(self):
        old = entry(0x10, in_effect="callers", callers_check="callers.py v1 2026-10-01", trap="Hooks.A=1")
        new = dict(old, callers_check="callers.py v1 2026-10-29", trap="Hooks.A=1&Hooks.B=1")
        lines, _, updated, kept = R.merge(lines_of(old), {0x10: new})
        got = json.loads(lines[0])
        self.assertEqual(updated, {0x10: ["callers_check", "trap"]})
        self.assertEqual((got["trap"], got["callers_check"]), ("Hooks.A=1&Hooks.B=1", "callers.py v1 2026-10-29"))
        self.assertEqual(got["notes"], old["notes"])

    def test_same_plan_keeps_the_check_date(self):
        old = entry(0x10, in_effect="callers", callers_check="callers.py v1 2026-10-01", trap="Hooks.A=1")
        new = dict(old, callers_check="callers.py v1 2026-10-29")
        lines, _, updated, _ = R.merge(lines_of(old), {0x10: new})
        self.assertEqual(updated, {})
        self.assertEqual(lines, lines_of(old))

    def test_unsure_and_todo_never_overwrite(self):
        old = entry(0x10, in_effect="callers", callers_check="callers.py v1 2026-10-01", trap="Hooks.A=1")
        new = dict(old, callers_check="callers.py v1 2026-10-29", trap="TODO")
        for unsure in ((), {0x10}):
            lines, _, updated, kept = R.merge(lines_of(old), {0x10: new}, unsure)
            self.assertEqual(updated, {})
            self.assertEqual(json.loads(lines[0])["trap"], "Hooks.A=1")
            self.assertIn((0x10, "trap", "Hooks.A=1", "TODO"), kept)
        lines, _, updated, _ = R.merge(lines_of(old), {0x10: dict(new, trap="Hooks.B=1")}, {0x10})
        self.assertEqual(json.loads(lines[0])["trap"], "Hooks.A=1")

    def test_differing_src_and_status_kept(self):
        old = entry(0x10, status="reimplemented")
        lines, _, updated, kept = R.merge(lines_of(old), {0x10: entry(0x10, src="src/b.cpp")})
        self.assertEqual(lines, lines_of(old))
        self.assertEqual(kept, [(0x10, "src", "src/a.cpp", "src/b.cpp")])

    def test_missing_field_added(self):
        old = {k: v for k, v in entry(0x10).items() if k != "test"}
        lines, _, updated, _ = R.merge(lines_of(old), {0x10: entry(0x10, original_calls=["0x500"])})
        self.assertEqual(updated, {0x10: ["test", "original_calls"]})
        got = json.loads(lines[0])
        self.assertEqual((got["test"], got["original_calls"]), ("tests/a.cpp", ["0x500"]))


class BriefTests(unittest.TestCase):
    def run_brief(self, **kw):
        args = argparse.Namespace(group="G", rva=None, status=None, in_effect=None, test_has=None, name_has=None,
                                  no_test=False)
        vars(args).update(kw)
        out = io.StringIO()
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "db/status").mkdir(parents=True)
            (Path(tmp) / "db/status/G.jsonl").write_text("".join(lines_of(
                entry(0x10, original_calls=["0x1", "0x2"]),
                entry(0x20, status="reimplemented", test=None, name="D::g"),
                entry(0x30, in_effect="callers", trap="Hooks.A=1"))), encoding="utf-8")
            with mock.patch.object(R.tsdb, "ROOT", Path(tmp)):
                R.brief(args, out)
        return out.getvalue().splitlines()

    def test_all_rows(self):
        rows = self.run_brief()
        self.assertEqual(len(rows), 5)
        self.assertTrue(rows[1].startswith("0x10") and " 2 a.cpp" in rows[1])
        self.assertEqual(rows[-1], "3 entries: 1 reimplemented, 2 verified")

    def test_filters(self):
        self.assertEqual(self.run_brief(in_effect="callers")[-1], "1 entries: 1 verified")
        self.assertEqual(self.run_brief(status="reimplemented,mapped")[-1], "1 entries: 1 reimplemented")
        self.assertEqual(self.run_brief(no_test=True)[1].split()[0], "0x20")
        self.assertEqual(self.run_brief(name_has="d::G")[1].split()[0], "0x20")
        self.assertEqual(self.run_brief(rva="0x10,0x30")[-1], "2 entries: 2 verified")
        self.assertEqual(self.run_brief(test_has="b.cpp")[-1], "0 entries: ")


class GuardWarningTests(unittest.TestCase):
    def test_one_line_per_file(self):
        items = {"db/status/G.jsonl": [("0x10", "Hooks.A=1"), ("0x20", "Hooks.A=1"), ("0x30", "Hooks.B=1")],
                 "db/status/H.jsonl": [("0x40", "Hooks.C=1")]}
        out = guard.callers_warnings(items)
        self.assertEqual(len(out), 2)
        self.assertIn("3 new entries", out[0])
        self.assertIn("traps Hooks.A=1, Hooks.B=1", out[0])
        self.assertIn("1 new entry ", out[1])
        detail = guard.callers_warnings(items, detail=True)
        self.assertEqual(len(detail), 4)
        self.assertTrue(detail[0].startswith("db/status/G.jsonl: 0x10 in effect through its callers"))


class RealGroupTests(unittest.TestCase):
    """Drafts of two merged groups against their hand-written entries."""

    @classmethod
    def setUpClass(cls):
        import callers
        try:
            cls.image = callers.Image()
        except Exception as e:  # noqa: BLE001
            raise unittest.SkipTest(f"no original image: {e}")
        cls.db = callers.Db(binary=False)

    def hand(self, group):
        return {int(e["rva"], 16): e for f, e in tsdb.status_entries() if f == group + ".jsonl"}

    def compare(self, hand, drafts, unsure, allowed):
        diffs = []
        for r, h in hand.items():
            self.assertIn(r, drafts, f"{r:#x} not drafted")
            d = drafts[r]
            for k in FIELDS + ("callers_check",):
                if k == "trap" and r in unsure:
                    continue  # the check found open references here: the draft says TODO and --record keeps it
                if k == "callers_check":
                    if "callers_check" in h:
                        self.assertEqual(d.get(k, "").rsplit(" ", 1)[0], h[k].rsplit(" ", 1)[0], f"{r:#x} {k}")
                    continue
                if h.get(k) != d.get(k) and (r, k) not in allowed:
                    diffs.append((hex(r), k, h.get(k), d.get(k)))
        self.assertEqual(diffs, [])

    def test_compgeom_triangle_set_from_scratch(self):
        """No existing entries used: names from the annotations, blocks, tests, original calls and the callers plan
        all derived. Two hand entries differ on purpose: 0x195a40 is named by the test only in a comment saying it
        is not reached (hand: reimplemented); 0x1a26d0's body is in src/compgeom/TriangleSet.cpp, its entry says Boxes.cpp."""
        group = "CompGeomTriangleSet"
        hand = self.hand(group)
        rec = R.Recorder(group, add=sorted(hand), use_existing=False, db=self.db, image=self.image)
        drafts = rec.draft()
        self.assertEqual(set(drafts), set(hand))
        self.compare(hand, drafts, rec.unsure, {(0x195a40, "status"), (0x1a26d0, "src")})
        self.assertEqual(rec.unsure, set())
        for r, d in drafts.items():
            self.assertIn("TODO", d["notes"])
            self.assertTrue(d["notes"].startswith("CompGeomTriangleSet_"), d["notes"])

    def test_cgzregistry_from_entries(self):
        """The registry's Ops slots carry vtable names (cGZRegistry::vfN) in the annotations, so their blocks are found
        from the entries' names. Without a built host the hex literal in cGZRegistry.h leaves the vtables' references
        open, so the traps are unsure here and --record keeps them."""
        group = "cGZRegistry"
        hand = self.hand(group)
        rec = R.Recorder(group, db=self.db, image=self.image)
        drafts = rec.draft()
        self.assertEqual(set(drafts), set(hand))
        self.compare(hand, drafts, rec.unsure, set())
        lines = (tsdb.ROOT / "db/status" / f"{group}.jsonl").read_text(encoding="utf-8").splitlines(keepends=True)
        merged, added, updated, kept = R.merge(lines, drafts, rec.unsure)
        self.assertEqual((added, updated), ([], {}))
        self.assertEqual(merged, lines)


if __name__ == "__main__":
    unittest.main()
