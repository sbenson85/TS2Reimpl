#!/usr/bin/env python3
"""Tests for tools/services.py (reading the host's ts2services.txt for milestone I).

Run: tools/py -I tools/tests/test_services.py
"""
import io
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import services as S  # noqa: E402

# Two sections; the second has an image service (cCTGFramework's vtables as db lists them), one of ours, a mixed one
# with two image slots, one in neither module and a listed-only object.
TEXT = """services 900 menu framework=gz::cCTGFramework ours:0x2a1000
service id=00000001 prio=5 tick=0 idle=0 class=gz::cGZCOM at=+0x0 ours
  +0x0 ours:0x2a2000 slots=12 image=0
1 services: 1 ours, 0 image, 0 mixed (ours with image slots)
services 1200 main-menu framework=cCTGFramework image:0xe995cc
service id=00000003 prio=-3000000 tick=0 idle=0 class=? at=? unknown
  +0x0 unknown slots=0 image=0
service id=0000000a prio=5 tick=1 idle=0 class=cTSPersistSystem at=+0x4 mixed
  +0x0 ours:0x2b0000 slots=5 image=2 image-at=1:0x100,3:0x200
  +0x4 ours:0x2b0100 slots=2 image=0
service id=a3cd8db3 prio=10000000 tick=0 idle=1 class=cCTGFramework at=+0x4 image
  +0x0 image:0xe995cc slots=3 image=3
  +0x4 image:0xe99694 slots=1 image=1
service id=0000000b prio=7 tick=1 idle=1 class=nGZLog::cService at=+0x0 ours
  +0x0 ours:0x2c0000 slots=4 image=0
listed tick=1 idle=0 class=cOdd at=+0x0 image
  +0x0 image:0xd00000 slots=1 image=1
idmap 4 entries, 1 differ
4 services: 1 ours, 1 image, 1 mixed (ours with image slots), 1 unknown
"""

CLASSES = [
    {"name": "cCTGFramework", "vftables": [{"rva": "0xe995cc", "offset": 0, "entries": ["0x300", "0x100", "0x400"]},
                                           {"rva": "0xe99694", "offset": 4, "entries": ["0x500", "0x600"]}]},
    {"name": "cTSPersistSystem", "vftables": [{"rva": "0xda5d74", "offset": 0, "entries": ["0x700"]}]},
]
FUNCS = {"0x100": {"size": 40, "name": "a"}, "0x200": {"size": 400, "name": "b"}, "0x300": {"size": 30, "name": "c"},
         "0x400": {"size": 1000, "name": "d"}, "0x500": {"size": 8, "name": "e"}}
STATUS = {"0x400": {"status": "verified"}, "0x300": {"status": "mapped"}}


class Parse(unittest.TestCase):
    def test_sections(self):
        secs = S.parse(TEXT)
        self.assertEqual([(s["frame"], s["label"]) for s in secs], [(900, "menu"), (1200, "main-menu")])
        self.assertEqual(secs[0]["framework"], "gz::cCTGFramework")
        self.assertEqual(S.pick(secs)["label"], "main-menu")
        self.assertEqual(S.pick(secs, "menu")["frame"], 900)
        with self.assertRaises(SystemExit):
            S.pick(secs, "lot")

    def test_objects(self):
        sec = S.pick(S.parse(TEXT))
        objs = sec["objects"]
        self.assertEqual([o["kind"] for o in objs], ["service"] * 4 + ["listed"])
        mixed = objs[1]
        self.assertEqual((mixed["id"], mixed["prio"], mixed["tick"], mixed["idle"], mixed["verdict"], mixed["at"]),
                         ("0000000a", 5, True, False, "mixed", "+0x4"))
        self.assertEqual(mixed["faces"][0], {"offset": 0, "where": "ours", "vtable": 0x2b0000, "slots": 5, "image": 2,
                                             "image_at": [(1, 0x100), (3, 0x200)]})
        self.assertEqual(objs[0]["faces"][0]["where"], None)
        self.assertEqual(objs[2]["faces"][1]["offset"], 4)
        self.assertIsNone(objs[4]["prio"])
        self.assertEqual(sec["summary"], {"services": 4, "ours": 1, "image": 1, "mixed": 1, "unknown": 1})
        self.assertEqual(sec["idmap"], (4, 1))
        self.assertFalse(sec["incomplete"])
        self.assertEqual(S.pick(S.parse(TEXT), "menu")["summary"]["unknown"], 0)

    def test_incomplete_and_cut(self):
        sec = S.parse("services 1 x\n(containers not read to the end)\n")[0]
        self.assertTrue(sec["incomplete"])
        self.assertIsNone(sec["summary"])


class Names(unittest.TestCase):
    def test_class_name_and_list(self):
        vt = S.vtable_index(CLASSES)
        objs = S.pick(S.parse(TEXT))["objects"]
        # The image's primary vtable names the class from db; ours keep the RTTI name.
        self.assertEqual(S.class_name(objs[2], vt), "cCTGFramework")
        self.assertEqual(S.class_name(objs[1], vt), "cTSPersistSystem")
        # Any face db places at its own offset names the class; a db vtable at another offset (a base's) doesn't.
        self.assertEqual(S.class_name(dict(objs[2], cls="x", faces=[dict(objs[2]["faces"][1])]), vt), "cCTGFramework")
        self.assertEqual(S.class_name(dict(objs[2], cls="x", faces=[dict(objs[2]["faces"][1], offset=8)]), vt), "x")
        self.assertTrue(S.on_list("cTSPersistSystem"))
        self.assertTrue(S.on_list("gz::cGZPersistResourceManager"))
        self.assertTrue(S.on_list("nGZLog::cService"))
        self.assertFalse(S.on_list("cService"))
        self.assertFalse(S.on_list("cCTGFramework"))

    def test_db_mismatch(self):
        vt = S.vtable_index(CLASSES)
        objs = S.pick(S.parse(TEXT))["objects"]
        self.assertEqual(S.db_mismatches(objs[2], vt), [(4, 2)])  # +4: 1 slot read, db lists 2

    def test_report(self):
        out = io.StringIO()
        S.report(S.pick(S.parse(TEXT)), S.vtable_index(CLASSES), out)
        text = out.getvalue()
        self.assertIn("services at frame 1200, main-menu (framework cCTGFramework image:0xe995cc)", text)
        self.assertIn("totals: 4 services: 1 ours, 1 image, 1 mixed, 1 unknown; on the I list 2 (1 not ours yet)", text)
        self.assertIn("+0x4 image:0xe99694 1/1 (db 2)", text)
        self.assertIn("id map: 1 of 4 entries differ", text)
        self.assertIn("not on the I list (2): ?, cCTGFramework", text)
        row = next(l for l in text.splitlines() if "cTSPersistSystem" in l)
        self.assertRegex(row, r"^\s+5 0000000a T - mixed\s+I\s+cTSPersistSystem\s+\+0x0 ours:0x2b0000 2/5")


class Todo(unittest.TestCase):
    def test_todo(self):
        vt = S.vtable_index(CLASSES)
        done = {"0x400", "0x500"}
        classes = S.todo(S.pick(S.parse(TEXT)), vt, FUNCS, STATUS, done)
        # cTSPersistSystem (mixed): its two image slots 0x200 (400 B) and 0x100 (40 B); cCTGFramework (image): every db
        # entry of both faces, 0x100 shared with the other class, 0x400 and 0x500 done, 0x600 unknown size.
        self.assertEqual([(c[0], c[1], c[2], c[4]) for c in classes],
                         [("cTSPersistSystem", 440, 440, "mixed"), ("cCTGFramework", 70, 1078, "image")])
        rows = classes[1][3]
        self.assertEqual([r[0] for r in rows], ["0x100", "0x300", "0x600", "0x400", "0x500"])
        self.assertEqual(rows[0][4], "0xe995cc:1")
        self.assertEqual(rows[1][3], "mapped")
        out = io.StringIO()
        S.print_todo(classes, 1, out)
        text = out.getvalue()
        self.assertIn("open in services not ours: 510 B in 2 classes", text)
        self.assertIn("cCTGFramework (image): 70 B open in 3 of 5 slot functions (1,078 B); the object still has", text)
        self.assertIn("cTSPersistSystem (mixed): 440 B open in 2 of 2 slot functions (440 B)\n", text)
        self.assertIn("... 2 more", text)
        self.assertNotIn("0x400", text)


if __name__ == "__main__":
    unittest.main()
