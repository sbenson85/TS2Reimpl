#!/usr/bin/env python3
"""Tests for tools/stagea.py (stage A drafts: kinds, the plan's hooks, the callers check, writing), on a small synthetic
image.

Run: .venv/Scripts/python -I tools/tests/test_stagea.py
"""
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import callers as C  # noqa: E402
import stagea as SA  # noqa: E402

S1, TH, SMALL, CTOR, FACT, OTHER, FOLD, DEL, THD = 0x1000, 0x1010, 0x1020, 0x1030, 0x1040, 0x1050, 0x1060, 0x1070, 0x1080
VT = 0x2000
SLOTS = [S1, TH, SMALL, OTHER, FOLD, THD]


def make_image():
    code = bytearray(b"\xcc" * 0x100)

    def put(at, data):
        code[at - 0x1000:at - 0x1000 + len(data)] = data

    put(S1, b"\x55\x8b\xec\x5d\xc3")                                         # a slot body
    put(TH, b"\x83\xe9\x04\xe9" + struct.pack("<i", S1 - (TH + 8)))           # sub ecx, 4; jmp S1
    put(SMALL, b"\xc3")                                                      # under 5 bytes
    put(CTOR, b"\xc7\x01" + struct.pack("<I", 0x10000000 + VT) + b"\xc3")    # mov [ecx], vtable; ret
    put(FACT, b"\xe8" + struct.pack("<i", CTOR - (FACT + 5)) + b"\xc3")      # call CTOR; ret
    put(OTHER, b"\x55\x8b\xec\x5d\xc3")
    put(FOLD, b"\x55\x8b\xec\x5d\xc3")
    put(DEL, b"\x55\x8b\xec\x5d\xc3")                                        # a deleting destructor only THD reaches
    put(THD, b"\x83\xc1\x08\xe9" + struct.pack("<i", DEL - (THD + 8)))       # add ecx, 8; jmp DEL
    img = C.Image.__new__(C.Image)
    img.base, img.entry, img.exports, img._branches = 0x10000000, 0x5000, [], None
    data = bytearray(0x30)
    for i, s in enumerate(SLOTS):
        data[4 * i:4 * i + 4] = struct.pack("<I", 0x10000000 + s)
    data[0x20:0x24] = struct.pack("<I", 0x10000000 + FACT)  # the factory in a table (a COM director's)
    img.sections = [(".text", 0x1000, 0x100, True, bytes(code)), (".rdata", 0x2000, 0x30, False, bytes(data))]
    img._relocs = {s: [VT + 4 * i] for i, s in enumerate(SLOTS)}
    img._relocs[VT] = [CTOR + 2]
    img._relocs[FACT] = [0x2020]
    return img


def make_db(hooked=None, status=None):
    db = C.Db.__new__(C.Db)
    sizes = {S1: 5, TH: 8, SMALL: 1, CTOR: 7, FACT: 6, OTHER: 5, FOLD: 5, DEL: 5, THD: 8}
    db.funcs = {r: {"rva": hex(r), "size": s, "name": f"F_{r:x}", "callees": []} for r, s in sizes.items()}
    db.starts = sorted(db.funcs)
    db.names = {S1: "cThing::Get", TH: "cThing::Get thunk", OTHER: "cOther::Foo", FOLD: "shared_vf_1060",
                CTOR: "cThing::cThing", FACT: "cThing::Create", DEL: "cThing::DeletingDestructor"}
    db.status = status or {}
    db.hooked = set(hooked or {})
    db.hook_names = dict(hooked or {})
    db.vtables = [(VT, VT + 4 * len(SLOTS), "cThing")]
    db.calls_by_db = {}
    db.src_refs, db.host_refs, db.game_address_uses = {}, {}, {}
    db.binary_note, db.bin_refs, db.hook_crcs = None, {}, set()
    db.vtable_uses = db._vtable_uses(False)
    db.classes = {"cThing": {"name": "cThing", "vftables": [{"rva": hex(VT), "offset": 0,
                                                             "entries": [hex(s) for s in SLOTS]}]}}
    return db


def plan(hooks=None, db=None, library=frozenset()):
    p = SA.Plan(make_image(), db or make_db(), ["cThing"], hooks or {}, (), library)
    return p, {r["rva"]: r for r in p.check()}


class Kinds(unittest.TestCase):
    def test_every_kind(self):
        _, rows = plan({FACT: "cThing_Create"})
        self.assertEqual(rows[S1]["draft"], "callers")
        self.assertEqual((rows[TH]["kind"], rows[TH]["draft"]), ("thunk", "thunk"))
        self.assertIn("this -= 0x4, to 0x1000", rows[TH]["why"])
        self.assertIsNone(rows[SMALL]["draft"])
        self.assertIn("under 5", rows[SMALL]["why"])
        self.assertEqual((rows[OTHER]["kind"], rows[OTHER]["draft"]), ("other", None))
        self.assertEqual((rows[FOLD]["kind"], rows[FOLD]["draft"]), ("folded", None))
        self.assertEqual((rows[CTOR]["kind"], rows[CTOR]["draft"]), ("stores-vtable", "callers"))
        self.assertEqual((rows[FACT]["kind"], rows[FACT]["draft"]), ("hooked", "hook"))
        self.assertIn("this += 0x8", rows[THD]["why"])
        self.assertEqual((rows[DEL]["kind"], rows[DEL]["draft"]), ("slot", "callers"))  # the body behind a thunk

    def test_code_storing_the_vtable_for_another_class_is_reported_not_drafted(self):
        db = make_db()
        db.names[CTOR] = "cSub::cSub"  # a subclass's constructor with the base inlined
        _, rows = plan({FACT: "cThing_Create"}, db=db)
        self.assertEqual((rows[CTOR]["kind"], rows[CTOR]["draft"]), ("stores-vtable", None))
        self.assertIn("creation site outside the class", rows[CTOR]["why"])

    def test_library_code_has_no_entry(self):
        _, rows = plan({FACT: "cThing_Create"}, library={hex(S1)})
        self.assertIsNone(rows[S1]["draft"])
        self.assertIn("library", rows[S1]["why"])

    def test_adjustor_patterns(self):
        self.assertEqual(SA.adjustor_target(b"\x83\xe9\x1c\xe9\x00\x00\x00\x00"), (0x1c, 8))
        self.assertEqual(SA.adjustor_target(b"\x81\xe9\x00\x01\x00\x00\xe9\x10\x00\x00\x00"), (0x100, 0x1b))
        self.assertIsNone(SA.adjustor_target(b"\x83\xe9\x1c\xeb\x00\x00\x00\x00"))  # a short jmp: not this shape
        self.assertIsNone(SA.adjustor_target(b"\x55\x8b\xec"))

    def test_owner_class(self):
        self.assertEqual(SA.owner_class("nGZ::AnyVector3::Clone (slot 3)"), "nGZ::AnyVector3")
        self.assertEqual(SA.owner_class("cX::cX(event id) (constructor)"), "cX")
        self.assertIsNone(SA.owner_class("shared_vf_1060"))
        self.assertTrue(SA.same_class("nGZ::AnyVector3", ["AnyVector3"]))
        self.assertFalse(SA.same_class("cOther", ["cThing"]))


class Check(unittest.TestCase):
    def test_clean_plan_with_the_factory_hook(self):
        _, rows = plan({FACT: "cThing_Create"})
        for r in (S1, CTOR, DEL):
            self.assertEqual(rows[r]["open"], [], r)
            self.assertEqual(rows[r]["trap"], "Hooks.cThing_Create=1", r)
        self.assertEqual(rows[TH]["open"], [])

    def test_without_a_hook_the_creator_is_listed_and_slots_stay_open(self):
        _, rows = plan()
        self.assertEqual((rows[FACT]["kind"], rows[FACT]["draft"]), ("creator", None))
        self.assertIn("creation site", rows[FACT]["why"])
        self.assertTrue(rows[S1]["open"])
        self.assertIsNone(rows[S1]["trap"])

    def test_a_creator_hooked_now_is_kept(self):
        p, rows = plan(db=make_db(hooked={FACT: "Old_Create"}))
        self.assertEqual(p.hooks, {FACT: "Old_Create"})
        self.assertEqual(rows[S1]["trap"], "Hooks.Old_Create=1")

    def test_hooks_on_drafted_functions_are_dropped(self):
        p, rows = plan({FACT: "cThing_Create"}, db=make_db(hooked={S1: "cThing_Get", TH: "cThing_GetThunk"}))
        self.assertEqual(p.dropped, [S1, TH])
        self.assertNotIn(S1, p.db.hooked)
        self.assertEqual(rows[S1]["open"], [])

    def test_a_thunk_recorded_through_callers_stays_so(self):
        db = make_db(status={TH: {"rva": hex(TH), "status": "verified", "in_effect": "callers", "trap": "Hooks.X=1"}})
        _, rows = plan({FACT: "cThing_Create"}, db=db)
        self.assertEqual((rows[TH]["kind"], rows[TH]["draft"]), ("thunk", "callers"))
        self.assertEqual(rows[TH]["trap"], "Hooks.cThing_Create=1")


class Drafts(unittest.TestCase):
    def test_entries(self):
        _, rows = plan({FACT: "cThing_Create"})
        e = SA.draft_entry(rows[S1], None, "src/x/cThing.cpp")
        self.assertEqual(list(e), ["rva", "name", "status", "src", "test", "notes", "in_effect", "callers_check", "trap"])
        self.assertEqual((e["rva"], e["status"], e["trap"]), ("0x1000", "reimplemented", "Hooks.cThing_Create=1"))
        self.assertTrue(C.CHECK_FIELD.fullmatch(e["callers_check"]))
        t = SA.draft_entry(rows[TH], None, "src/x/cThing.cpp")
        self.assertNotIn("in_effect", t)
        self.assertIn("adjustor thunk", t["notes"])
        old = {"rva": "0x1000", "name": "cThing::Get", "status": "verified", "notes": "kept", "x": 1}
        e = SA.draft_entry(rows[S1], old, "src/x/cThing.cpp")
        self.assertEqual((e["status"], e["notes"], e["x"]), ("verified", "kept", 1))

    def test_write(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "cThing.jsonl"
            keep = json.dumps({"rva": "0x1000", "name": "cThing::Get", "status": "verified", "notes": 'n "trap": "x"'})
            path.write_text(keep + "\n" + json.dumps({"rva": "0x9", "status": "verified"}), encoding="utf-8")
            drafts = [dict(json.loads(keep), in_effect="callers", callers_check="callers.py v1 2026-10-21", trap="Hooks.A=1"),
                      {"rva": "0x1030", "name": "cThing::cThing", "status": "reimplemented"},
                      {"rva": "0x1040", "name": "elsewhere"}]
            added, updated, left = SA.write(path, drafts, {"0x1040": "Other.jsonl"})
            self.assertEqual((added, updated, left), (["0x1030"], ["0x1000"], [("0x1040", "Other.jsonl")]))
            lines = path.read_text(encoding="utf-8").splitlines()
            self.assertEqual(lines[0], keep[:-1] + ', "in_effect": "callers", "callers_check": "callers.py v1 2026-10-21", '
                                                   '"trap": "Hooks.A=1"}')
            self.assertEqual(json.loads(lines[1]), {"rva": "0x9", "status": "verified"})
            self.assertEqual(json.loads(lines[2])["rva"], "0x1030")
            self.assertEqual(SA.write(path, drafts, {"0x1040": "Other.jsonl"})[:2], ([], []))  # idempotent


if __name__ == "__main__":
    unittest.main()
