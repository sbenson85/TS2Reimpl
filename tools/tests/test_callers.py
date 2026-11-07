#!/usr/bin/env python3
"""Tests for tools/callers.py (references to an original and whether each is neutralised; the status checks of
"in_effect": "callers"; the trap table), on a small synthetic image.

Run: .venv/Scripts/python -I tools/tests/test_callers.py
"""
import os
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import callers as C  # noqa: E402

T = 0x1010  # the target


def text_section():
    code = bytearray(b"\x90" * 0x80)

    def put(at, data):
        code[at - 0x1000:at - 0x1000 + len(data)] = data

    put(0x1000, b"\xe8" + struct.pack("<i", T - 0x1005) + b"\xc3")          # F1: call T ; ret
    put(0x1008, b"\xb8\x90\xeb\x04\x90\xc3")                               # F0: mov eax, imm (holds EB rel8 to T)
    put(0x1010, b"\x55\x8b\xec\x5d\xc3\x90\x90\x90")                       # T: push ebp ; ... ; ret
    put(0x1020, b"\xeb" + struct.pack("<b", T - 0x1022) + b"\x90\x90")     # F2: jmp short T
    put(0x1030, b"\xc7\x01\x00\x20\x00\x10\xc3")                           # F3: mov dword [ecx], vtable 0x2000 ; ret
    put(0x1040, b"\xb8\xe8" + struct.pack("<i", T - 0x1046)[:3] + b"\xff\xc0\xc3")  # F4: mov eax, imm (holds E8 rel) ; inc eax
    put(0x1050, b"\xe8" + struct.pack("<i", 0x1060 - 0x1055) + b"\xc3")    # A: call B
    put(0x1060, b"\xe8" + struct.pack("<i", 0x1050 - 0x1065) + b"\xc3")    # B: call A
    return bytes(code)


def make_image(entry=0x5000):
    img = C.Image.__new__(C.Image)
    img.base = 0x10000000
    img.entry = entry
    data = bytearray(0x20)
    data[0:4] = struct.pack("<I", img.base + T)      # vtable 0x2000 slot 0
    data[0x10:0x14] = struct.pack("<I", img.base + T)  # a function-pointer table at 0x2010
    data[0x18] = 0xE8                                 # an E8 in data: not code
    img.sections = [(".text", 0x1000, 0x80, True, text_section()), (".rdata", 0x2000, 0x20, False, bytes(data))]
    img.exports = []
    img._relocs = {T: [0x2000, 0x2010], 0x2000: [0x1032], 0x1030: [0x2014]}  # F3 (the vtable's creator) in a table
    img._branches = None
    return img


def make_db(status=None, hooked=(), src_refs=None, host_refs=None, game_address_uses=None, binary=False,
            vtables=((0x2000, 0x2004, "cThing"),)):
    db = C.Db.__new__(C.Db)
    sizes = {0x1000: 6, 0x1008: 6, 0x1010: 8, 0x1020: 6, 0x1030: 7, 0x1040: 8, 0x1050: 6, 0x1060: 6}
    db.funcs = {r: {"rva": hex(r), "size": s, "name": f"F_{r:x}", "callees": []} for r, s in sizes.items()}
    db.starts = sorted(db.funcs)
    db.names = {}
    db.status = status or {}
    db.hooked = set(hooked)
    db.hook_names = {h: f"H_{h:x}" for h in hooked}
    db.vtables = list(vtables)
    db.calls_by_db = {T: {0x1000}, 0x1060: {0x1050}, 0x1050: {0x1060}}
    db.src_refs = src_refs or {}
    db.host_refs = host_refs or {}
    db.game_address_uses = game_address_uses or {}
    db.binary_note = None
    db.bin_refs = {}
    db.object_uses = {}
    db.hook_crcs = set()
    db.vtable_uses = db._vtable_uses(binary)
    return db


def rows(v):
    return {r["site"]: r for r in v["references"]}


def callers_entry(rva, trap="Startup.OwnStart=1,2", **extra):
    e = {"rva": hex(rva), "name": f"F_{rva:x}", "status": "verified", "src": "src/a.cpp", "test": "tests/a.cpp",
         "notes": "", "in_effect": "callers", "callers_check": f"callers.py v{C.VERSION} 2026-10-18", "trap": trap}
    e.update(extra)
    return e


class References(unittest.TestCase):
    def test_every_kind_is_found_and_open_by_default(self):
        v = C.Checker(make_image(entry=T), make_db()).verdict(T)
        r = rows(v)
        self.assertEqual(set(r), {"code:0x1000", "code:0x1020", "code:0x1041", "abs:0x2000", "abs:0x2010", "pe:entry"})
        self.assertIn("call in 0x1000", r["code:0x1000"]["what"])
        self.assertIn("jmp short", r["code:0x1020"]["what"])
        self.assertIn("not confirmed", r["code:0x1041"]["what"])
        self.assertNotIn("not confirmed", r["code:0x1000"]["what"])
        self.assertIn("vtable 0x2000", r["abs:0x2000"]["what"])
        self.assertIn("data", r["abs:0x2010"]["what"])
        self.assertEqual(sorted(v["open"]), sorted(r))

    def test_db_listed_call_without_a_located_site(self):
        db = make_db()
        db.calls_by_db[T] = {0x1000, 0x1030}
        r = rows(C.Checker(make_image(), db).verdict(T))
        self.assertIn("code:0x1030", r)
        self.assertIn("site not located", r["code:0x1030"]["what"])

    def test_hooked_caller_covers(self):
        r = rows(C.Checker(make_image(), make_db(hooked={0x1000})).verdict(T))
        self.assertTrue(r["code:0x1000"]["covered"])
        self.assertIsNone(r["code:0x1000"]["via"])
        self.assertFalse(r["code:0x1020"]["covered"])

    def test_caller_replaced_through_its_callers_covers_when_clean(self):
        db = make_db(status={0x1020: callers_entry(0x1020)})
        r = rows(C.Checker(make_image(), db).verdict(T))
        self.assertTrue(r["code:0x1020"]["covered"])
        self.assertEqual(r["code:0x1020"]["via"], "0x1020")

    def test_assumed_caller_covers(self):
        r = rows(C.Checker(make_image(), make_db(), assume=[0x1020]).verdict(T))
        self.assertTrue(r["code:0x1020"]["covered"])

    def test_caller_with_open_references_does_not_cover(self):
        # F1 replaced through its callers, but F1 itself is still called from original code (F3)
        db = make_db(status={0x1000: callers_entry(0x1000)})
        db.calls_by_db[0x1000] = {0x1030}
        r = rows(C.Checker(make_image(), db).verdict(T))
        self.assertFalse(r["code:0x1000"]["covered"])
        self.assertIn("1 open", r["code:0x1000"]["why"])

    def test_original_status_without_hook_does_not_cover(self):
        db = make_db(status={0x1000: {"rva": "0x1000", "status": "verified"}})
        r = rows(C.Checker(make_image(), db).verdict(T))
        self.assertFalse(r["code:0x1000"]["covered"])

    def test_cycle_is_open(self):
        db = make_db(status={0x1050: callers_entry(0x1050), 0x1060: callers_entry(0x1060)})
        checker = C.Checker(make_image(), db)
        self.assertTrue(checker.verdict(0x1050)["open"])
        self.assertTrue(checker.verdict(0x1060)["open"])

    def test_vtable_covered_when_its_creators_are_ours(self):
        r = rows(C.Checker(make_image(), make_db(hooked={0x1030})).verdict(T))
        self.assertTrue(r["abs:0x2000"]["covered"])
        r = rows(C.Checker(make_image(), make_db()).verdict(T))
        self.assertFalse(r["abs:0x2000"]["covered"])

    def test_vtable_used_through_game_address_is_open(self):
        db = make_db(hooked={0x1030}, game_address_uses={0x2000: [("src/x.cpp", 3)]})
        r = rows(C.Checker(make_image(), db).verdict(T))
        self.assertFalse(r["abs:0x2000"]["covered"])
        self.assertIn("src/x.cpp:3", r["abs:0x2000"]["why"])
        # A named constant not passed to GameAddress (a table row, a helper's argument: lane GT's SimAnticsParams, where
        # VTableOf(kRunPrivateTreeVTable) stored the game's vtable) counts too since v2; before, it was invisible.
        db = make_db(hooked={0x1030}, src_refs={0x2000: [("src/x.h", 3, "hex literal")]})
        r = rows(C.Checker(make_image(), db).verdict(T))["abs:0x2000"]
        self.assertFalse(r["covered"])
        self.assertIn("src/x.h:3 (hex literal)", r["why"])
        # host/ literals too.
        db = make_db(hooked={0x1030}, host_refs={0x2000: {"host/control.cpp"}})
        self.assertFalse(rows(C.Checker(make_image(), db).verdict(T))["abs:0x2000"]["covered"])
        # OriginalFunction sites name functions, not vtables: they don't make the vtable live.
        db = make_db(hooked={0x1030}, src_refs={0x2000: [("src/x.h", 3, "OriginalFunction/OriginalAddress")]})
        self.assertTrue(rows(C.Checker(make_image(), db).verdict(T))["abs:0x2000"]["covered"])

    def test_vtable_creator_called_only_by_hooked_code_is_dead(self):
        img = make_image()
        img._relocs[0x1030] = []
        db = make_db(hooked={0x1000})
        db.calls_by_db[0x1030] = {0x1000}
        self.assertTrue(rows(C.Checker(img, db).verdict(T))["abs:0x2000"]["covered"])
        db.hooked = set()  # the caller is unhooked but referenced from nothing: dead, so the creator is too
        self.assertTrue(rows(C.Checker(img, db).verdict(T))["abs:0x2000"]["covered"])
        img._relocs[0x1000] = [0x2018]  # the caller is in a table: it runs, so does the creator
        self.assertFalse(rows(C.Checker(img, db).verdict(T))["abs:0x2000"]["covered"])

    def test_destructor_reached_only_through_its_own_vtable(self):
        img = make_image()
        img._relocs[0x1030] = [0x2004]  # F3 is slot 1 of the vtable it stores
        db = make_db()
        db.vtables = [(0x2000, 0x2008, "cThing")]
        checker = C.Checker(img, db)
        self.assertTrue(rows(checker.verdict(T))["abs:0x2000"]["covered"])
        listed = {r["rva"]: r for r in C.vtable_only(checker, [0x1030, T])}
        self.assertTrue(listed["0x1030"]["droppable"])
        self.assertFalse(listed[hex(T)]["droppable"])
        self.assertTrue(any("code:0x1000" in k for k in listed[hex(T)]["keep"]))

    def test_a_cycle_nothing_live_enters_is_dead(self):
        img = make_image()
        img._relocs[0x1030] = []
        db = make_db()
        db.calls_by_db[0x1030] = {0x1000}
        db.calls_by_db[0x1000] = {0x1030}
        self.assertTrue(rows(C.Checker(img, db).verdict(T))["abs:0x2000"]["covered"])

    def test_dead_follows_a_bounded_depth(self):
        img = make_image()
        img._relocs[0x1030] = []
        db = make_db(hooked={0x1050})
        db.calls_by_db[0x1030] = {0x1000}
        db.calls_by_db[0x1000] = {0x1020}
        db.calls_by_db[0x1020] = {0x1040}
        db.calls_by_db[0x1040] = {0x1060}
        db.calls_by_db[0x1060] = {0x1050}  # hooked, five callers up from the creator
        # T's own code references sit in that chain (F1, F2, F4): handled here, so that only the vtable's creator starts
        # dead() (from F2, three callers below the hook, it would find the chain dead, and the cache would keep that).
        handled = {T: {"code:0x1000": "x", "code:0x1020": "x", "code:0x1041": "x"}}
        self.assertFalse(rows(C.Checker(img, db, handled=handled).verdict(T))["abs:0x2000"]["covered"])
        checker = C.Checker(img, db, handled=handled)
        checker.DEAD_DEPTH = 8
        self.assertTrue(rows(checker.verdict(T))["abs:0x2000"]["covered"])

    def test_handled_sites_and_stale_handled(self):
        handled = {T: {"abs:0x2010": "the host rewrites the table", "abs:0x9999": "gone"}}
        v = C.Checker(make_image(), make_db(), handled=handled).verdict(T)
        self.assertTrue(rows(v)["abs:0x2010"]["covered"])
        self.assertEqual(v["unused_handled"], ["abs:0x9999"])

    def test_handled_from_the_status_entry(self):
        db = make_db(status={T: callers_entry(T, callers_handled={"abs:0x2010": "x"})})
        self.assertTrue(rows(C.Checker(make_image(), db).verdict(T))["abs:0x2010"]["covered"])

    def test_our_code_naming_the_original_is_never_covered(self):
        db = make_db(src_refs={T: [("src/a.cpp", 7, "OriginalFunction/OriginalAddress")]})
        v = C.Checker(make_image(), db, handled={T: {"src:src/a.cpp:7": "please"}}).verdict(T)
        self.assertFalse(rows(v)["src:src/a.cpp:7"]["covered"])

    def test_host_literal(self):
        db = make_db(host_refs={T: {"host/main.cpp"}})
        r = rows(C.Checker(make_image(), db).verdict(T))
        self.assertFalse(r["host:host/main.cpp"]["covered"])
        r = rows(C.Checker(make_image(), db, handled={T: {"host:host/main.cpp": "mode 1 only"}}).verdict(T))
        self.assertTrue(r["host:host/main.cpp"]["covered"])

    def test_xc_slot_with_an_owned_list_row(self):
        owned = {T: [("src/app/StaticInit.cpp", 5, "hex literal")]}
        with mock.patch.object(C, "XC_TABLE", (0x2010, 0x2014)):  # the test image's table at 0x2010 plays the XC table
            r = rows(C.Checker(make_image(), make_db(src_refs=owned)).verdict(T))
            self.assertTrue(r["abs:0x2010"]["covered"] and r["abs:0x2010"]["xc"])
            self.assertTrue(r["src:src/app/StaticInit.cpp:5"]["covered"])
            self.assertFalse(r["abs:0x2000"]["covered"])  # the vtable slot is not the XC table
            r = rows(C.Checker(make_image(), make_db()).verdict(T))  # no owned-list row: the slot stays open
            self.assertFalse(r["abs:0x2010"]["covered"])
            for src in (("src/app/StaticInit.cpp", 5, "OriginalFunction/OriginalAddress"),  # calls it: never covered
                        ("src/app/Other.cpp", 5, "hex literal")):  # not an owned list
                r = rows(C.Checker(make_image(), make_db(src_refs={T: [src]})).verdict(T))
                self.assertFalse(r["abs:0x2010"]["covered"], src)
                self.assertFalse(r[f"src:{src[0]}:5"]["covered"], src)
        r = rows(C.Checker(make_image(), make_db(src_refs=owned)).verdict(T))  # 0x2010 outside the XC table
        self.assertFalse(r["abs:0x2010"]["covered"])
        self.assertFalse(r["src:src/app/StaticInit.cpp:5"]["covered"])

    def test_recursion_is_covered(self):
        img = make_image()
        db = make_db()
        db.calls_by_db[T] = {T}
        self.assertTrue(rows(C.Checker(img, db).verdict(T))[f"code:{T:#x}"]["covered"])


class HookDeps(unittest.TestCase):
    def test_the_vtables_creator_hook(self):
        # F3 (0x1030) stores the vtable and sits in a table (0x2014): with its hook off the vtable is live again
        checker = C.Checker(make_image(), make_db(hooked={0x1030}))
        self.assertEqual(checker.hook_deps(T), {0x1030})
        self.assertEqual(C.Checker(make_image(), make_db()).hook_deps(T), set())  # nothing hooked: nothing it rests on

    def test_a_hook_reached_only_through_the_vtable_does_not_count(self):
        img = make_image()
        img._relocs[0x1030] = [0x2004]  # F3 is slot 1 of the vtable it stores (a destructor)
        db = make_db(hooked={0x1030})
        db.vtables = [(0x2000, 0x2008, "cThing")]
        self.assertEqual(C.Checker(img, db).hook_deps(T), set())

    def test_the_nearest_hook(self):
        img = make_image()
        img._relocs[0x1030] = []
        img._relocs[0x1000] = [0x2018]  # F1 runs (a table), calls the creator F3
        db = make_db(hooked={0x1000})
        db.calls_by_db[0x1030] = {0x1000}
        self.assertEqual(C.Checker(img, db).hook_deps(T), {0x1000})  # F3 unhooked: F1's hook keeps it from running
        db = make_db(hooked={0x1000, 0x1030})
        db.calls_by_db[0x1030] = {0x1000}
        db.status = {T: {"callers_handled": {"code:0x1000": "x"}}}  # F1's own call of T doesn't count
        self.assertEqual(C.Checker(img, db).hook_deps(T), {0x1030})

    def test_a_creator_replaced_through_its_callers_brings_its_own_hooks(self):
        # F3 (the vtable's creator) is replaced through its callers; its own check rests on F1's hook (F1 calls it), so a
        # slot of F3's vtable rests on that hook too: with F1's hook off, the original F3 runs and stores the vtable.
        img = make_image()
        img._relocs[0x1030] = []
        img._relocs[0x1000] = [0x2018]
        db = make_db(hooked={0x1000}, status={0x1030: callers_entry(0x1030, trap="Hooks.H_1000=1")})
        db.calls_by_db[0x1030] = {0x1000}
        self.assertEqual(C.Checker(img, db).hook_deps(T), {0x1000})
        db.status = {}  # plain original code F3: dead() reaches the same hook
        self.assertEqual(C.Checker(img, db).hook_deps(T), {0x1000})
        db.hooked = {0x1000, 0x1050}
        db.hook_names = {0x1000: "H_1000", 0x1050: "H_1050"}
        db.status = {0x1030: callers_entry(0x1030, trap="Hooks.H_1000=1")}
        db.calls_by_db[0x1030] = {0x1000, 0x1050}  # two hooked callers: the creator's check rests on both
        self.assertEqual(C.Checker(img, db).hook_deps(T), {0x1000, 0x1050})
        db.status[0x1030]["callers_handled"] = {"code:0x1050": "the host"}  # the creator's own handled site doesn't count
        self.assertEqual(C.Checker(img, db).hook_deps(T), {0x1000})

    def test_vtable_only_suggests_the_trap(self):
        img = make_image()
        img._relocs[0x1070] = [0x2004]  # S (0x1070, nops): slot 1 of the vtable F3 stores, nothing else
        db = make_db(hooked={0x1030})
        db.funcs[0x1070] = {"rva": "0x1070", "size": 8, "name": "S", "callees": []}
        db.starts = sorted(db.funcs)
        db.vtables = [(0x2000, 0x2008, "cThing")]
        [row] = C.vtable_only(C.Checker(img, db), [0x1070])
        self.assertTrue(row["droppable"])
        self.assertEqual((row["hooks"], row["trap"]), (["H_1030"], "Hooks.H_1030=1"))
        db = make_db(hooked={0x1030})  # F3 reached only through the vtable: no hook decides, no one-switch trap
        db.funcs[0x1070] = {"rva": "0x1070", "size": 8, "name": "S", "callees": []}
        db.starts = sorted(db.funcs)
        db.vtables = [(0x2000, 0x2008, "cThing")]
        img._relocs[0x1030] = [0x2000 + 8]
        db.vtables = [(0x2000, 0x200c, "cThing")]
        [row] = C.vtable_only(C.Checker(img, db), [0x1070])
        self.assertEqual((row["droppable"], row["hooks"], row["trap"]), (True, [], None))

    def test_vtable_only_suggests_a_conjunction(self):
        # S is slot 1 of the vtable F3 stores; F3 is called by F1 and F2, both hooked and both running (in tables)
        img = make_image()
        img._relocs[0x1070] = [0x2004]
        img._relocs[0x1030] = []
        img._relocs[0x1000] = [0x2018]
        img._relocs[0x1020] = [0x201c]
        db = make_db(hooked={0x1000, 0x1020})
        db.funcs[0x1070] = {"rva": "0x1070", "size": 8, "name": "S", "callees": []}
        db.starts = sorted(db.funcs)
        db.vtables = [(0x2000, 0x2008, "cThing")]
        db.calls_by_db[0x1030] = {0x1000, 0x1020}
        [row] = C.vtable_only(C.Checker(img, db), [0x1070])
        self.assertEqual((row["droppable"], row["hooks"], row["trap"]),
                         (True, ["H_1000", "H_1020"], "Hooks.H_1000=1&Hooks.H_1020=1"))


def graph(funcs, calls=(), slots=(), stores=(), tables=(), hooked=(), vtables=None):
    """An image and db made of named functions (0x10 bytes each from 0x3000, outside the bytes the scan reads) and
    vtables (0x10 slots each from 0x4000): calls (caller, callee) as db-listed calls, slots (vtable, index, function),
    stores (function, vtable: the function references the vtable, as a constructor does) and tables (functions whose
    address sits in data: live). Returns (image, db, {name: rva})."""
    at = {n: 0x3000 + 0x10 * i for i, n in enumerate(funcs)}
    vts = vtables or sorted({v for v, _, _ in slots} | {v for _, v in stores})
    at.update({v: 0x4000 + 0x40 * i for i, v in enumerate(vts)})
    img = make_image()
    img._relocs = {}
    db = make_db(hooked={at[h] for h in hooked}, vtables=[(at[v], at[v] + 0x40, v) for v in vts])
    db.hook_names = {at[h]: h for h in hooked}
    db.funcs = {at[n]: {"rva": hex(at[n]), "size": 0x10, "name": n, "callees": []} for n in funcs}
    db.starts = sorted(db.funcs)
    db.calls_by_db = {}
    for caller, callee in calls:
        db.calls_by_db.setdefault(at[callee], set()).add(at[caller])
    for v, i, f in slots:
        img._relocs.setdefault(at[f], []).append(at[v] + 4 * i)
    for f, v in stores:
        img._relocs.setdefault(at[v], []).append(at[f] + 4)
    for i, f in enumerate(tables):
        img._relocs.setdefault(at[f], []).append(0x5000 + 4 * i)  # data outside any section, function or vtable
    return img, db, at


class HookDepsRule(unittest.TestCase):
    """The rule lane LP set (Checker.hook_deps): the nearest hook cuts; a hook reached only through vtables is beside the
    point only when its own references are cut without it."""

    def deps(self, img, db, at, target):
        return {db.hook_names[h] for h in C.Checker(img, db).hook_deps(at[target])}

    def test_a_constructor_reached_through_another_classs_live_vtable(self):
        # cDeviceStateDX (lane LM): its hooked constructor Ctor stores the vtable; Ctor is called from Helper, called from
        # Init, a slot of cOther's vtable, which Maker (in a table: it runs) stores. Ctor is reached only through
        # vtables, but cOther's is live: its hook is the cut. Before LP the climb ended in nothing and gave no hook.
        funcs = ["T", "Ctor", "Helper", "Init", "Maker"]
        img, db, at = graph(funcs, calls=[("Helper", "Ctor"), ("Init", "Helper")],
                            slots=[("cThing", 0, "T"), ("cOther", 2, "Init")],
                            stores=[("Ctor", "cThing"), ("Maker", "cOther")], tables=["Maker"], hooked=["Ctor"])
        self.assertEqual(self.deps(img, db, at, "T"), {"Ctor"})
        [row] = C.vtable_only(C.Checker(img, db), [at["T"]])
        self.assertEqual((row["droppable"], row["trap"]), (True, "Hooks.Ctor=1"))
        # With cOther's maker hooked too, cOther's vtable is dead and so is Ctor without its hook: Maker's hook decides.
        img, db, at = graph(funcs, calls=[("Helper", "Ctor"), ("Init", "Helper")],
                            slots=[("cThing", 0, "T"), ("cOther", 2, "Init")],
                            stores=[("Ctor", "cThing"), ("Maker", "cOther")], tables=["Maker"], hooked=["Ctor", "Maker"])
        self.assertEqual(self.deps(img, db, at, "T"), {"Maker"})

    def test_a_classs_own_constructor_hooks(self):
        # Two hooked constructors called from running code, a destructor re-storing the vtable reached only through it
        # (hooked or not): the trap rests on both constructors and nothing else.
        funcs = ["T", "Ctor1", "Ctor2", "Dtor", "DelDtor", "Live"]
        kw = dict(calls=[("Live", "Ctor1"), ("Live", "Ctor2"), ("DelDtor", "Dtor")],
                  slots=[("cThing", 0, "T"), ("cThing", 1, "DelDtor")],
                  stores=[("Ctor1", "cThing"), ("Ctor2", "cThing"), ("Dtor", "cThing")], tables=["Live"])
        img, db, at = graph(funcs, hooked=["Ctor1", "Ctor2", "DelDtor"], **kw)
        self.assertEqual(self.deps(img, db, at, "T"), {"Ctor1", "Ctor2"})
        img, db, at = graph(funcs, hooked=["Ctor1", "Ctor2", "Dtor"], **kw)
        self.assertEqual(self.deps(img, db, at, "T"), {"Ctor1", "Ctor2"})
        # One constructor unhooked: the vtable is live (the verdict is open) and no hook cuts it, so it names none; the
        # uncut site is recorded (status --check reports it).
        img, db, at = graph(funcs, hooked=["Ctor1"], **kw)
        checker = C.Checker(img, db)
        self.assertEqual(checker.hook_deps(at["T"]), set())
        self.assertEqual(checker.uncut[at["T"]], [f"abs:{at['cThing']:#x}"])
        self.assertFalse(rows(checker.verdict(at["T"]))[f"abs:{at['cThing']:#x}"]["covered"])

    def test_folded_slots(self):
        # One body in the vtables of two classes (a folded QueryInterface): each class's constructor hook counts.
        funcs = ["T", "CtorA", "CtorB", "Live"]
        img, db, at = graph(funcs, calls=[("Live", "CtorA"), ("Live", "CtorB")],
                            slots=[("cA", 0, "T"), ("cB", 3, "T")], stores=[("CtorA", "cA"), ("CtorB", "cB")],
                            tables=["Live"], hooked=["CtorA", "CtorB"])
        self.assertEqual(self.deps(img, db, at, "T"), {"CtorA", "CtorB"})
        [row] = C.vtable_only(C.Checker(img, db), [at["T"]])
        self.assertEqual(row["trap"], "Hooks.CtorA=1&Hooks.CtorB=1")

    def test_a_climb_into_our_code_or_data_keeps_the_hook(self):
        # cTSWantTreeLoader (lane LP): Start (hooked) creates the objects carrying cThing; Start is a slot of the loader's
        # vtable, which LoaderCtor (hooked, running) and Inline store. Inline is a slot of cManager's vtable, which only
        # the hooked ManagerCtor stores: nothing live reaches the loader's vtable without its hooks, so they decide.
        funcs = ["T", "Start", "LoaderCtor", "Inline", "ManagerCtor", "Live"]
        kw = dict(calls=[("Live", "LoaderCtor"), ("Live", "ManagerCtor")],
                  slots=[("cThing", 0, "T"), ("cLoader", 3, "Start"), ("cManager", 8, "Inline")],
                  stores=[("Start", "cThing"), ("LoaderCtor", "cLoader"), ("Inline", "cLoader"),
                          ("ManagerCtor", "cManager")],
                  tables=["Live"], hooked=["Start", "LoaderCtor", "ManagerCtor"])
        img, db, at = graph(funcs, **kw)
        self.assertEqual(self.deps(img, db, at, "T"), {"LoaderCtor", "ManagerCtor"})
        # Our code calls the original Inline (OriginalFunction): it runs whatever the hooks, stores the loader's vtable,
        # and the original Start runs without its hook. Before LP the climb ignored our code's reference.
        img, db, at = graph(funcs, **kw)
        db.src_refs = {at["Inline"]: [("src/ts/x.cpp", 211, "OriginalFunction/OriginalAddress")]}
        self.assertEqual(self.deps(img, db, at, "T"), {"Start"})
        # Inline's address in a data table, or our build holding the loader's vtable: the same.
        img, db, at = graph(funcs, **dict(kw, tables=["Live", "Inline"]))
        self.assertEqual(self.deps(img, db, at, "T"), {"Start"})
        img, db, at = graph(funcs, **kw)
        db.vtable_uses = {at["cLoader"]: ["ts2host.exe 0x1234 in X: an operand holds the game address"]}
        self.assertEqual(self.deps(img, db, at, "T"), {"Start"})

    def test_a_cycle_through_the_hooked_destructor_leans_on_nothing_live(self):
        # The deleting destructor (hooked) is the vtable's only slot reached, and only the constructor stores it: the
        # result found while the vtable was open is not kept for later questions about the destructor alone.
        funcs = ["T", "Ctor", "DelDtor", "Body", "Live"]
        img, db, at = graph(funcs, calls=[("Live", "Ctor"), ("DelDtor", "Body")],
                            slots=[("cThing", 0, "T"), ("cThing", 1, "DelDtor")],
                            stores=[("Ctor", "cThing"), ("DelDtor", "cThing")], tables=["Live"],
                            hooked=["Ctor", "DelDtor"])
        checker = C.Checker(img, db)
        self.assertEqual(checker.hook_deps(at["T"]), {at["Ctor"]})
        # Body (the destructor's body) is called only by the hooked deleting destructor: while T was decided, "DelDtor
        # never runs without its hook" leaned on the open vtable; asked again on its own, it rests on Ctor as well.
        self.assertEqual(checker.hook_deps(at["Body"]), {at["Ctor"]})
        self.assertEqual(checker.hook_deps(at["DelDtor"]), {at["Ctor"]})


S = 0x1070  # a slot body (nops) called only from F1, which only slot 1 of the vtable F3 stores holds (an adjustor thunk)


def thunk_setup(hooked=(0x1030,)):
    img = make_image()
    img._relocs[0x1000] = [0x2004]
    db = make_db(hooked=hooked)
    db.funcs[S] = {"rva": hex(S), "size": 8, "name": "S", "callees": []}
    db.starts = sorted(db.funcs)
    db.vtables = [(0x2000, 0x2008, "cThing")]
    db.calls_by_db[S] = {0x1000}
    return img, db


class DeadOwners(unittest.TestCase):
    def test_code_only_a_dead_vtable_reaches_covers(self):
        img, db = thunk_setup()
        checker = C.Checker(img, db)
        r = rows(checker.verdict(S))["code:0x1000"]
        self.assertEqual((r["covered"], r["dead"], r["via"]), (True, True, None))
        self.assertIn("never runs", r["why"])
        [row] = C.vtable_only(checker, [S])
        self.assertEqual((row["droppable"], row["hooks"], row["trap"]), (True, ["H_1030"], "Hooks.H_1030=1"))

    def test_code_a_live_vtable_reaches_is_open(self):
        img, db = thunk_setup(hooked=())  # F3 sits in a table (0x2014): the vtable is live, so F1 runs
        checker = C.Checker(img, db)
        r = rows(checker.verdict(S))["code:0x1000"]
        self.assertEqual((r["covered"], r["dead"]), (False, False))
        self.assertFalse(C.vtable_only(checker, [S])[0]["droppable"])

    def test_unreferenced_code_stays_open(self):
        img, db = thunk_setup()
        img._relocs[0x1000] = []  # nothing visibly references F1: maybe a run-time address
        r = rows(C.Checker(img, db).verdict(S))["code:0x1000"]
        self.assertEqual((r["covered"], r["dead"]), (False, False))

    def test_hooked_or_replaced_owners_are_decided_by_their_own_check(self):
        img, db = thunk_setup(hooked=(0x1030, 0x1000))
        r = rows(C.Checker(img, db).verdict(S))["code:0x1000"]
        self.assertEqual((r["covered"], r["dead"]), (True, False))
        img, db = thunk_setup()
        db.status = {0x1000: callers_entry(0x1000)}  # its own check is clean (the slot is covered): covered via F1
        r = rows(C.Checker(img, db).verdict(S))["code:0x1000"]
        self.assertEqual((r["covered"], r["dead"], r["via"]), (True, False, "0x1000"))

    def test_status_check_accepts_the_hook_trap(self):
        img, db = thunk_setup()
        e = callers_entry(S, trap="Hooks.H_1030=1")
        db.status = {S: e}
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "trap_table.cpp")
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(C.trap_table_text(C.trap_rows(db, img)))
            self.assertEqual(C.check_status([("G.jsonl", e)], img, db, path), ([], []))
            e["trap"] = "Startup.OwnStart=1"  # the check rests on F3's hook: a note
            _, n = C.check_status([("G.jsonl", e)], img, db, path)
            self.assertTrue(any("rests on the hooks H_1030" in x for x in n), n)


OBJ = 0x6000  # a constant-initialised static object in a writable section (0x6000..0x6100)


def static_graph(funcs, refs=(), vptrs=(("cThing", OBJ),), writable=True, **kw):
    """graph() plus a writable section at 0x6000 holding static objects' vptr words (vptrs: (vtable, word)) and
    references to addresses in it (refs: (function, address): the function's code holds the address)."""
    img, db, at = graph(funcs, **kw)
    img.writable = [(0x6000, 0x6100)] if writable else []
    for v, word in vptrs:
        img._relocs.setdefault(at[v], []).append(word)
    for f, addr in refs:
        img._relocs.setdefault(addr, []).append(at[f] + 8)
    return img, db, at


class StaticObjects(unittest.TestCase):
    """Lane NI's rule (2026-10-26): a vtable word in writable data is a constant-initialised static object's vptr; the
    object is live unless every reference to it is in code in effect or dead, and its cut is theirs. cImageManipDX
    (lane NG): the static 0xfecf98 carries the vtable 0xd9e844; only the hooked getter 0x16c0c0 and its atexit stub
    0xd90360 (which only the getter registers) reference it."""

    FUNCS = ["T", "Getter", "Stub", "Live", "Getter2"]

    def setup(self, hooked=("Getter",), refs=(("Getter", OBJ), ("Stub", OBJ)), **kw):
        kw.setdefault("calls", [("Live", "Getter"), ("Getter", "Stub")])
        kw.setdefault("slots", [("cThing", 0, "T")])
        return static_graph(self.FUNCS, refs=refs, tables=["Live"], hooked=hooked, **kw)

    def judge(self, img, db, at):
        """(covered, hook names, uncut sites) for the slot T."""
        checker = C.Checker(img, db)
        covered = rows(checker.verdict(at["T"]))[f"abs:{at['cThing']:#x}"]["covered"]
        return covered, sorted(checker.trap_hooks(at["T"])), checker.uncut.get(at["T"], [])

    def test_the_getters_hook_cuts_the_vptr_word(self):
        img, db, at = self.setup()
        self.assertEqual(C.static_objects(img, db), {OBJ: (at["cThing"], OBJ, OBJ + 4)})
        self.assertEqual(self.judge(img, db, at), (True, ["Getter"], []))
        [row] = C.vtable_only(C.Checker(img, db), [at["T"]])
        self.assertEqual((row["droppable"], row["trap"]), (True, "Hooks.Getter=1"))
        self.assertIn("static object(s) 0x6000", rows(C.Checker(img, db).verdict(at["T"]))[f"abs:{at['cThing']:#x}"]["why"])

    def test_status_check_takes_the_getters_hook(self):
        img, db, at = self.setup()
        e = callers_entry(at["T"], trap="Hooks.Getter=1")
        db.status = {at["T"]: e}
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "trap_table.cpp")
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(C.trap_table_text(C.trap_rows(db, img)))
            self.assertEqual(C.check_status([("G.jsonl", e)], img, db, path), ([], []))
            e["trap"] = "Hooks.Getter2=1"
            db.hooked.add(at["Getter2"])
            db.hook_names[at["Getter2"]] = "Getter2"
            p, _ = C.check_status([("G.jsonl", e)], img, db, path)
            self.assertTrue(any("rests on the hooks Getter " in x for x in p), p)

    def test_read_only_data_is_not_an_object(self):
        # A vtable word outside writable sections (a table of vtables in .rdata) stays a live data reference.
        img, db, at = self.setup(writable=False)
        self.assertEqual(C.static_objects(img, db), {})
        self.assertEqual(self.judge(img, db, at), (False, [], [f"abs:{at['cThing']:#x}"]))

    def test_an_unhooked_getter_runs(self):
        img, db, at = self.setup(hooked=())
        self.assertEqual(self.judge(img, db, at), (False, [], [f"abs:{at['cThing']:#x}"]))

    def test_the_stub_alone_runs_when_something_live_registers_it(self):
        img, db, at = self.setup(calls=[("Live", "Getter"), ("Getter", "Stub"), ("Live", "Stub")])
        self.assertEqual(self.judge(img, db, at), (False, [], [f"abs:{at['cThing']:#x}"]))

    def test_other_references_keep_it_live(self):
        cases = {
            "data": lambda img, db: img._relocs[OBJ].append(0x5000),  # a pointer to it in data
            "field": lambda img, db: img._relocs.setdefault(OBJ + 2, []).append(0x3000 + 0x10 * 3 + 8),  # Live, inside
            "src literal": lambda img, db: db.src_refs.update({OBJ: [("src/x.cpp", 3, "hex literal")]}),
            "GameAddress": lambda img, db: db.game_address_uses.update({OBJ: [("src/x.cpp", 4)]}),
            "host": lambda img, db: db.host_refs.update({OBJ: {"host/x.cpp"}}),
            "our build": lambda img, db: db.object_uses.update({OBJ: ["ts2host.exe 0x401234 in X (a.obj): data holds"]}),
        }
        for name, change in cases.items():
            with self.subTest(name):
                img, db, at = self.setup()
                change(img, db)
                self.assertEqual(self.judge(img, db, at), (False, [], [f"abs:{at['cThing']:#x}"]))

    def test_an_object_nothing_references_is_live(self):
        # Maybe reached through an address computed at run time (an array's later element).
        img, db, at = self.setup(refs=())
        self.assertEqual(self.judge(img, db, at), (False, [], [f"abs:{at['cThing']:#x}"]))

    def test_two_objects_rest_on_both_getters(self):
        img, db, at = self.setup(hooked=("Getter", "Getter2"), refs=(("Getter", OBJ), ("Stub", OBJ), ("Getter2", OBJ + 0x10)),
                                 vptrs=(("cThing", OBJ), ("cThing", OBJ + 0x10)),
                                 calls=[("Live", "Getter"), ("Getter", "Stub"), ("Live", "Getter2")])
        self.assertEqual(self.judge(img, db, at), (True, ["Getter", "Getter2"], []))

    def test_several_vptrs(self):
        # A class with two vtables (subobjects at 0 and 8): its static spans both words; a reference anywhere in it
        # counts, whichever vtable is asked about.
        def mi(refs, vptrs=(("cA", OBJ), ("cB", OBJ + 8)), vt="cB"):
            img, db, at = self.setup(refs=refs, vptrs=vptrs, slots=[(vt, 0, "T")], vtables=["cA", "cB"])
            words = ((at["cA"], 0), (at["cB"], 8))
            db.vt_layout = {at["cA"]: (0, words), at["cB"]: (8, words)}
            checker = C.Checker(img, db)
            covered = rows(checker.verdict(at["T"]))[f"abs:{at[vt]:#x}"]["covered"]
            return C.static_objects(img, db), covered, sorted(checker.trap_hooks(at["T"]))

        objs, covered, hooks = mi([("Getter", OBJ), ("Stub", OBJ)])
        self.assertEqual(objs[OBJ + 8][1:], (OBJ, OBJ + 12))
        self.assertEqual(objs[OBJ][1:], (OBJ, OBJ + 12))
        self.assertEqual((covered, hooks), (True, ["Getter"]))
        _, covered, hooks = mi([("Getter", OBJ), ("Stub", OBJ), ("Live", OBJ + 4)])  # a field between, from live code
        self.assertEqual((covered, hooks), (False, []))
        # The first vtable's slot, the second subobject's address (an interface pointer) in live code: live.
        _, covered, hooks = mi([("Getter", OBJ), ("Stub", OBJ)], vt="cA")
        self.assertEqual((covered, hooks), (True, ["Getter"]))
        _, covered, hooks = mi([("Getter", OBJ), ("Stub", OBJ), ("Live", OBJ + 8)], vt="cA")
        self.assertEqual((covered, hooks), (False, []))
        # The word at 8 without the class's other vtable at 0: not taken for an object (live, as before the rule).
        objs, covered, hooks = mi([("Getter", OBJ + 8)], vptrs=(("cB", OBJ + 8),))
        self.assertEqual((objs, covered, hooks), ({}, False, []))

    def test_our_build_holding_the_object(self):
        import ourrefs
        b = make_binary()
        data = bytearray(0x30)
        data[8:12] = struct.pack("<I", ourrefs.game_base() + OBJ)
        b.sections = [b.sections[0], (".rdata", 0x402000, False, bytes(data))]
        img, db, at = self.setup()
        db._image = img
        db.vtable_uses = db._vtable_uses(b)
        self.assertEqual(list(db.object_uses), [OBJ])
        self.assertEqual(self.judge(img, db, at), (False, [], [f"abs:{at['cThing']:#x}"]))


# A two-level data table: 2 row pointers at 0x2020, then 2 rows of 2 words. A and B are functions no code references;
# the reader F3 (0x1030) reads the table's base.
TABLE, A, B, READER, OTHER = 0x2020, 0x1008, 0x1040, 0x1030, 0x1000
TABLE_ROW = {"rva": hex(TABLE), "end": "0x2038", "row_pointers": 2, "rows": 2, "row_words": 2, "reason": "test"}


def table_image(refs=()):
    """make_image's sections with the table; refs: further (target, site) relocations. The reader and OTHER are live
    (a data word holds them, so their hooks are what cuts them) unless refs names the reader."""
    img = make_image()
    words = {0x2020: 0x2028, 0x2024: 0x2030, 0x2028: A, 0x202c: B, 0x2030: B, 0x2034: A, 0x2040: 0}
    data = bytearray(0x60)
    relocs = {TABLE: [0x1032], OTHER: [0x2018]}
    if not any(target == READER for target, _ in refs):
        relocs[READER] = [0x2014]
    for site, value in words.items():
        data[site - 0x2000:site - 0x2000 + 4] = struct.pack("<I", img.base + value)
        if value:
            relocs.setdefault(value, []).append(site)
    for target, site in refs:
        relocs.setdefault(target, []).append(site)
    img.sections = [img.sections[0], (".rdata", 0x2000, 0x60, False, bytes(data))]
    img._relocs = relocs
    return img


def table_db(rows=(TABLE_ROW,), hooked=(READER,), **kw):
    db = make_db(hooked=hooked, **kw)
    db.table_rows = list(rows)
    return db


class DataTables(unittest.TestCase):
    """Every case starts from a table the rule accepts (so it fails without the rule) and changes one thing."""

    def open_of(self, target=A, img=None, db=None):
        return C.Checker(img or table_image(), db or table_db()).verdict(target)["open"]

    def test_slots_of_a_table_only_replaced_code_reads(self):
        checker = C.Checker(table_image(), table_db())
        v = checker.verdict(A)
        self.assertEqual(v["open"], [])
        self.assertEqual(set(rows(v)), {"abs:0x2028", "abs:0x2034"})  # both rows, the table's own pointers no reference
        self.assertEqual(rows(v)["abs:0x2028"]["table"], hex(TABLE))
        self.assertEqual(rows(v)["abs:0x2028"]["readers"], [hex(READER)])
        self.assertEqual(checker.trap_hooks(A), [f"H_{READER:x}"])  # the trap's cut is the reader's
        self.assertTrue(checker.dead(A))

    def test_without_a_declaration_the_slot_is_open(self):
        self.assertEqual(self.open_of(), [])
        self.assertEqual(sorted(self.open_of(db=table_db(rows=()))), ["abs:0x2028", "abs:0x2034"])

    def test_a_reference_outside_the_table_keeps_it_live(self):
        self.assertEqual(self.open_of(), [])
        self.assertEqual(self.open_of(img=table_image([(A, 0x2050)])), ["abs:0x2050"])  # another table's word
        self.assertEqual(self.open_of(img=table_image([(A, 0x1001)])), ["abs:0x1001"])  # code that runs
        self.assertEqual(self.open_of(B, img=table_image([(A, 0x2050)])), [])  # only that function
        checker = C.Checker(table_image([(A, 0x2050)]), table_db())
        self.assertFalse(checker.dead(A))
        self.assertEqual(checker.trap_hooks(A), [f"H_{READER:x}"])  # the verdict decides a top-level data reference

    def test_a_live_reader_keeps_the_table_live(self):
        self.assertEqual(self.open_of(), [])
        self.assertEqual(sorted(self.open_of(db=table_db(hooked=()))), ["abs:0x2028", "abs:0x2034"])
        # Two readers, one live.
        img = table_image([(TABLE, 0x1001)])
        self.assertEqual(sorted(self.open_of(img=img)), ["abs:0x2028", "abs:0x2034"])
        self.assertEqual(self.open_of(img=img, db=table_db(hooked=(READER, OTHER))), [])
        checker = C.Checker(img, table_db())
        self.assertFalse(checker.dead(A))
        self.assertEqual(checker.trap_hooks(A), [])
        self.assertEqual(sorted(checker.uncut[A]), ["abs:0x2028", "abs:0x2034"])

    def test_both_levels_every_address_in_the_table_counts(self):
        self.assertEqual(self.open_of(), [])
        for into in (0x2030, 0x2024, 0x2037):  # a row, a row pointer word, the last byte
            for site in (0x1001, 0x2050):  # from live code, from data outside the table
                img = table_image([(into, site)])
                self.assertEqual(sorted(self.open_of(img=img)), ["abs:0x2028", "abs:0x2034"], (into, site))
                self.assertEqual(sorted(self.open_of(B, img=img)), ["abs:0x202c", "abs:0x2030"], (into, site))
        checker = C.Checker(table_image([(0x2030, 0x2050)]), table_db())
        checker.trap_hooks(A)
        self.assertEqual(sorted(checker.uncut[A]), ["abs:0x2028", "abs:0x2034"])  # the hook walk agrees: data is live
        # A row's address in replaced code is one more reader, whose hook is a cut too.
        img = table_image([(0x2030, 0x1001)])
        checker = C.Checker(img, table_db(hooked=(READER, OTHER)))
        self.assertEqual(checker.verdict(A)["open"], [])
        self.assertEqual(checker.trap_hooks(A), sorted([f"H_{OTHER:x}", f"H_{READER:x}"]))

    def test_a_row_pointer_word_is_only_internal_at_its_own_row(self):
        checker = C.Checker(table_image(), table_db())
        refs = checker.table_references(checker.table_of(0x2028))
        self.assertEqual([r[0] for r in refs], ["abs:0x1032"])

    def test_a_table_nothing_references_stays_live(self):
        self.assertEqual(self.open_of(), [])
        img = table_image()
        img._relocs[TABLE] = []
        self.assertEqual(sorted(self.open_of(img=img)), ["abs:0x2028", "abs:0x2034"])
        checker = C.Checker(img, table_db())
        self.assertFalse(checker.dead(A))
        checker.trap_hooks(A)
        self.assertEqual(sorted(checker.uncut[A]), ["abs:0x2028", "abs:0x2034"])  # no hook cuts it

    def test_a_dead_reader_counts_and_a_reader_nothing_references_does_not(self):
        img = table_image([(READER, 0x1001)])  # only OTHER references the reader
        self.assertEqual(self.open_of(img=img, db=table_db(hooked=(OTHER,))), [])
        self.assertEqual(C.Checker(img, table_db(hooked=(OTHER,))).trap_hooks(A), [f"H_{OTHER:x}"])
        self.assertEqual(sorted(self.open_of(img=img, db=table_db(hooked=()))), ["abs:0x2028", "abs:0x2034"])
        self.assertEqual(sorted(self.open_of(db=table_db(hooked=(OTHER,)))), ["abs:0x2028", "abs:0x2034"])
        img._relocs[READER] = []
        self.assertEqual(sorted(self.open_of(img=img, db=table_db(hooked=()))), ["abs:0x2028", "abs:0x2034"])

    def test_our_sources_host_and_build_keep_it_live(self):
        self.assertEqual(self.open_of(), [])
        name = [("src/a.cpp", 3, "hex literal")]
        # Naming the function.
        self.assertEqual(self.open_of(db=table_db(src_refs={A: name}))[-1], "src:src/a.cpp:3")
        self.assertEqual(self.open_of(db=table_db(host_refs={A: {"host/a.cpp"}})), ["host:host/a.cpp"])
        db = table_db()
        db.bin_refs = {A: ["ts2host.exe 0x401000 in x (o.obj): an operand holds it"]}
        self.assertEqual(self.open_of(db=db), ["bin:0x401000"])
        # Naming an address in the table: every slot goes.
        slots = ["abs:0x2028", "abs:0x2034"]
        self.assertEqual(sorted(self.open_of(db=table_db(src_refs={0x2030: name}))), slots)
        self.assertEqual(sorted(self.open_of(db=table_db(game_address_uses={TABLE: [("src/a.cpp", 3)]}))), slots)
        self.assertEqual(sorted(self.open_of(db=table_db(host_refs={0x2024: {"host/a.cpp"}}))), slots)
        db = table_db()
        db.object_uses = {TABLE: ["ts2host.exe 0x401000 in x (o.obj): data holds it"]}
        self.assertEqual(sorted(self.open_of(db=db)), slots)

    def test_the_binary_scan_looks_for_the_tables_addresses(self):
        import ourrefs
        b = make_binary()
        data = bytearray(0x30)
        data[8:12] = struct.pack("<I", ourrefs.game_base() + 0x2030)
        b.sections = [b.sections[0], (".rdata", 0x402000, False, bytes(data))]
        db = table_db()
        db._image = table_image()
        db.vtable_uses = db._vtable_uses(b)
        self.assertEqual(list(db.object_uses), [TABLE])
        self.assertEqual(sorted(self.open_of(db=db)), ["abs:0x2028", "abs:0x2034"])

    def test_a_declaration_the_image_does_not_bear_out_is_ignored(self):
        def bad(img=None, **change):
            db = table_db(rows=({**TABLE_ROW, **change},))
            checker = C.Checker(img or table_image(), db)
            return len(checker.table_problems()), sorted(checker.verdict(A)["open"])

        slots = (1, ["abs:0x2028", "abs:0x2034"])
        self.assertEqual(bad(), (0, []))
        self.assertEqual(bad(end="0x2040"), slots)
        self.assertEqual(bad(rows=1), slots)
        self.assertEqual(bad(row_pointers=1, rows=1, row_words=3, end="0x2030"), (1, slots[1]))  # row at 0x2024 misread
        moved = table_image()
        moved._relocs[0x2028].remove(0x2020)  # a pointer word that isn't relocated
        self.assertEqual(bad(img=moved), slots)
        midrow = table_image()  # a pointer into the middle of a row
        midrow._relocs[0x2030].remove(0x2024)
        midrow._relocs[0x202c] = [0x2024]
        midrow.sections[1] = (".rdata", 0x2000, 0x60, False, midrow.sections[1][4][:0x24]
                              + struct.pack("<I", midrow.base + 0x202c) + midrow.sections[1][4][0x28:])
        self.assertEqual(bad(img=midrow), slots)
        norow = table_image()  # both pointers on the first row: nothing points at the second
        norow._relocs[0x2030].remove(0x2024)
        norow._relocs[0x2028].append(0x2024)
        norow.sections[1] = (".rdata", 0x2000, 0x60, False, norow.sections[1][4][:0x24]
                             + struct.pack("<I", norow.base + 0x2028) + norow.sections[1][4][0x28:])
        self.assertEqual(bad(img=norow), slots)
        writable = table_image()
        writable.writable = [(0x2000, 0x2060)]
        self.assertEqual(bad(img=writable), slots)
        db = table_db()
        del db.funcs[B]  # a slot that isn't a function start
        db.starts = sorted(db.funcs)
        checker = C.Checker(table_image(), db)
        self.assertEqual((len(checker.table_problems()), sorted(checker.verdict(A)["open"])), slots)
        db = table_db(vtables=((0x2028, 0x202c, "cThing"),))  # overlapping a vtable
        self.assertEqual(len(C.Checker(table_image(), db).table_problems()), 1)
        db = table_db()
        db.funcs[0x2030] = {"rva": "0x2030", "size": 4, "name": "x", "callees": []}  # overlapping a function
        db.starts = sorted(db.funcs)
        self.assertEqual(len(C.Checker(table_image(), db).table_problems()), 1)

    def check(self, entries, refs=((READER, 0x1001),), hooked=(OTHER,), rows_=(TABLE_ROW,), note=None):
        status = {int(e["rva"], 16): e for _, e in entries}
        img, db = table_image(refs), table_db(rows=rows_, hooked=hooked, status=status)
        db.binary_note = note
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "trap_table.cpp")
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(C.trap_table_text(C.trap_rows(db, img)))
            return C.check_status(entries, img, db, path)[0]

    def entries(self, trap=f"Hooks.H_{OTHER:x}=1", reader_trap=None, **extra):
        return [("G.jsonl", callers_entry(READER, trap=reader_trap or trap)),
                ("G.jsonl", callers_entry(A, trap=trap, status="reimplemented", absorbed_by=[hex(READER)], **extra))]

    def test_the_status_check_ties_the_trap_to_the_readers_cut(self):
        self.assertEqual(self.check(self.entries()), [])
        p = self.check(self.entries(), rows_=())
        self.assertTrue(any("open reference abs:0x2028" in x for x in p), p)
        p = self.check(self.entries(trap="Startup.OwnStart=1", reader_trap=f"Hooks.H_{OTHER:x}=1"))
        self.assertTrue(any("isn't replaced under every value" in x for x in p), p)  # the reader may be off
        p = self.check(self.entries(trap=f"Hooks.H_{READER:x}=1"), hooked=(OTHER, READER), refs=())
        self.assertTrue(any("hooked" in x for x in p), p)

    def test_a_table_slots_verdict_needs_a_current_build(self):
        # The reader is a callers entry, so only the slots of A rest on it: take the reader's row out of the question.
        entries = [("G.jsonl", callers_entry(A, trap=f"Hooks.H_{READER:x}=1", status="reimplemented"))]
        kw = dict(refs=(), hooked=(READER,))
        self.assertEqual(self.check(entries, **kw), [])
        p = self.check(entries, note="no build", **kw)
        self.assertTrue(any(x.startswith("no build") for x in p), p)

    def test_a_bad_declaration_is_a_status_problem(self):
        p = self.check(self.entries(), rows_=({**TABLE_ROW, "end": "0x2040"},))
        self.assertTrue(any("db/data-tables.jsonl: 0x2020" in x for x in p), p)

    def test_absorbed_by_needs_only_table_references_and_names_the_readers(self):
        self.assertEqual(self.check(self.entries()), [])
        e = self.entries()
        e[1][1]["absorbed_by"] = ["0x1050"]
        self.assertTrue(any("readers are" in x for x in self.check(e)))
        e = self.entries(callers_handled={"abs:0x2050": "host"})
        p = self.check(e, refs=((READER, 0x1001), (A, 0x2050)))
        self.assertTrue(any("every reference to be a declared data table's slot" in x for x in p), p)


class GameAddressUses(unittest.TestCase):
    def test_constants_are_resolved_by_scope(self):
        db = C.Db.__new__(C.Db)
        db.sources = {
            "src/a/Foo.h": "struct Foo {\n    static constexpr uint32_t kVTable = 0xd00010;\n};\n",
            "src/a/Bars.h": "class Bar : public X {\n    static constexpr uint32_t kVTable = 0xd00020; // 0xd00099\n};\n",
            "src/a/Foo.cpp": '#include "a/Foo.h"\nvoid f(Foo *p) { p->mVTable = GameAddress<void>(kVTable); }\n',
            "src/b/Use.cpp": "void g() { q = GameAddress<const void>(Bar::kVTable); r = GameAddress<void>(0xd00030); }\n"
                             "// s = GameAddress<void>(0xd00040);\n",
            "src/b/Other.cpp": "void h() { t = GameAddress<void>(kUnknown); }\nconstexpr uint32_t kUnknown = 0xd00050;\n",
            # the template argument defaults (the common form), an alias scope (falls back to every kVTableRec), Opaque
            "src/c/Rec.h": "struct Rec {\n    static constexpr uint32_t kVTableRec = 0xd00060;\n};\n",
            "src/c/Rec.cpp": "using Obj = Rec;\nvoid i(Obj *o) { o->mVTable = GameAddress(Obj::kVTableRec); }\n"
                             "void j() { u = GameAddressOpaque<int>(0xd00070); }\n",
        }
        uses = db._game_address_uses()
        self.assertEqual(uses.get(0xd00010), [("src/a/Foo.cpp", 2)])
        self.assertEqual(uses.get(0xd00020), [("src/b/Use.cpp", 1)])
        self.assertEqual(uses.get(0xd00030), [("src/b/Use.cpp", 1)])
        self.assertEqual(uses.get(0xd00050), [("src/b/Other.cpp", 1)])
        self.assertNotIn(0xd00040, uses)  # in a comment
        self.assertNotIn(0xd00099, uses)
        self.assertEqual(uses.get(0xd00060), [("src/c/Rec.cpp", 2)])
        self.assertEqual(uses.get(0xd00070), [("src/c/Rec.cpp", 3)])


class Traps(unittest.TestCase):
    def test_parse(self):
        self.assertEqual(C.parse_trap("Startup.OwnStart=1,2"), [("Startup", "OwnStart", {1, 2})])
        self.assertEqual(C.parse_trap("Hooks.A=1&Hooks.B_2=1&Startup.OwnStart=2,3"),
                         [("Hooks", "A", {1}), ("Hooks", "B_2", {1}), ("Startup", "OwnStart", {2, 3})])
        for bad in (None, "", "Startup.OwnStart", "OwnStart=2", "Startup.OwnStart=32", "Startup.OwnStart=1;2",
                    "Hooks.A=1&", "&Hooks.A=1", "Hooks.A=1&&Hooks.B=1", "Hooks.A=1&Hooks.B=32", "Hooks.A=1 & Hooks.B=1",
                    "Hooks.A=1&Hooks.A=1", "Hooks.A=1&hooks.a=1"):
            self.assertIsNone(C.parse_trap(bad), bad)

    def test_implies(self):
        p = C.parse_trap
        self.assertTrue(C.trap_implies(p("A.B=1"), p("A.B=1,2")))
        self.assertFalse(C.trap_implies(p("A.B=1,2"), p("A.B=1")))
        self.assertTrue(C.trap_implies(p("A.B=1&C.D=1"), p("C.D=1")))
        self.assertFalse(C.trap_implies(p("C.D=1"), p("A.B=1&C.D=1")))
        self.assertTrue(C.trap_implies(p("C.D=1&A.B=2"), p("A.B=1,2&C.D=1")))
        self.assertFalse(C.trap_implies(p("A.B=1&C.E=1"), p("A.B=1&C.D=1")))
        self.assertEqual(C.trap_text(p("Hooks.B=1&A.B=2,1")), "Hooks.B=1&A.B=1,2")

    def test_table_text(self):
        t = C.trap_table_text([(0x8248a3, "entry", 10, 0x8c45a9e8, "Startup", "OwnStart", 0x6)])
        self.assertIn('{"entry", 0x8248a3, 10, 0x8c45a9e8, "Startup", "OwnStart", 0x6}, // Startup.OwnStart=1,2', t)
        self.assertIn("{nullptr, 0, 0, 0, nullptr, nullptr, 0},", t)
        self.assertNotIn("TrapTerm", t)

    def test_table_text_conjunction(self):
        t = C.trap_table_text([(0x10, "one", 5, 1, "Hooks", "A", 0x2),
                               (0x8248a3, "entry", 10, 0x8c45a9e8, "Hooks", "A", 0x2, [("Hooks", "B", 0x2), ("Startup", "OwnStart", 0x6)])])
        self.assertIn('static const TrapTerm kAlso_8248a3[] = {{"Hooks", "B", 0x2}, {"Startup", "OwnStart", 0x6}, '
                      '{nullptr, nullptr, 0}};', t)
        self.assertIn('{"entry", 0x8248a3, 10, 0x8c45a9e8, "Hooks", "A", 0x2, kAlso_8248a3}, '
                      '// Hooks.A=1 & Hooks.B=1 & Startup.OwnStart=1,2', t)
        self.assertIn('{"one", 0x10, 5, 0x00000001, "Hooks", "A", 0x2}, // Hooks.A=1\n', t)
        self.assertLess(t.index("kAlso_8248a3[]"), t.index("kTrapTable[]"))
        t = C.trap_table_text([(0x20, "a", 5, 1, "S", "X", 0x6, [("S", "Y", 0x2)]),
                               (0x30, "b", 5, 1, "S", "X", 0x6, [("S", "Y", 0x2)]),
                               (0x40, "c", 5, 1, "S", "X", 0x6, [("S", "Y", 0x4)])])
        self.assertEqual(t.count("static const TrapTerm"), 2)  # the same further terms share one array
        self.assertIn('{"b", 0x30, 5, 0x00000001, "S", "X", 0x6, kAlso_20}', t)
        self.assertIn('{"c", 0x40, 5, 0x00000001, "S", "X", 0x6, kAlso_40}', t)

    def test_rows_of_a_conjunction(self):
        img = make_image()
        db = make_db(status={T: callers_entry(T, trap="Hooks.A=1&Startup.OwnStart=1,2")})
        [row] = C.trap_rows(db, img)
        self.assertEqual(row[4:], ("Hooks", "A", 0x2, [("Startup", "OwnStart", 0x6)]))
        db.status[T]["trap"] = "Hooks.A=1"
        [row] = C.trap_rows(db, img)
        self.assertEqual(len(row), 7)


class StatusCheck(unittest.TestCase):
    def check(self, entries, hooked=(), table_rows=None):
        status = {int(e["rva"], 16): e for _, e in entries}
        img, db = make_image(entry=T), make_db(status=status, hooked=hooked)
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "trap_table.cpp")
            rows_ = C.trap_rows(db, img) if table_rows is None else table_rows
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(C.trap_table_text(rows_))
            return C.check_status(entries, img, db, path)

    def clean_t(self, **extra):
        handled = {"pe:entry": "host", "code:0x1000": "x", "code:0x1020": "x", "code:0x1041": "x", "abs:0x2000": "x",
                   "abs:0x2010": "x"}
        return callers_entry(T, callers_handled=handled, **extra)

    def test_hook_entries_are_not_checked(self):
        p, n = C.check_status([("G.jsonl", {"rva": "0x1", "status": "verified"})], table=os.devnull + "missing")
        self.assertEqual(len(p), 1)  # only the missing trap table
        self.assertIn("out of date", p[0])

    def test_clean_entry(self):
        p, n = self.check([("G.jsonl", self.clean_t())])
        self.assertEqual(p, [])

    def test_open_reference(self):
        e = self.clean_t()
        del e["callers_handled"]["abs:0x2010"]
        p, _ = self.check([("G.jsonl", e)])
        self.assertTrue(any("open reference abs:0x2010" in x for x in p), p)

    def test_fields(self):
        p, _ = self.check([("G.jsonl", self.clean_t(in_effect="bogus"))])
        self.assertTrue(any("unknown \"in_effect\"" in x for x in p), p)
        p, _ = self.check([("G.jsonl", self.clean_t(status="mapped"))])
        self.assertTrue(any("on a mapped entry" in x for x in p), p)
        p, _ = self.check([("G.jsonl", self.clean_t(callers_check="by eye"))])
        self.assertTrue(any("callers_check" in x for x in p), p)
        p, _ = self.check([("G.jsonl", self.clean_t(trap="OwnStart"))])
        self.assertTrue(any("needs \"trap\"" in x for x in p), p)
        _, n = self.check([("G.jsonl", self.clean_t(trap="Startup.OwnStart=0,2"))])
        self.assertTrue(any("includes 0" in x for x in n), n)
        _, n = self.check([("G.jsonl", self.clean_t(callers_check="callers.py v0 2026-01-01"))])
        self.assertTrue(any("rerun" in x for x in n), n)

    def test_also_hooked(self):
        p, _ = self.check([("G.jsonl", self.clean_t())], hooked={T})
        self.assertTrue(any("also hooked" in x for x in p), p)

    def test_stale_table(self):
        p, _ = self.check([("G.jsonl", self.clean_t())], table_rows=[])
        self.assertTrue(any("out of date" in x for x in p), p)

    def test_hook_switch_traps(self):
        e = self.clean_t(trap="Hooks.H_1030=1")
        del e["callers_handled"]["abs:0x2000"]  # covered by the hooked creator F3 instead
        p, n = self.check([("G.jsonl", e)], hooked={0x1030})
        self.assertEqual((p, n), ([], []))
        e["trap"] = "Hooks.H_1030=0,1"
        p, _ = self.check([("G.jsonl", e)], hooked={0x1030})
        self.assertTrue(any("=1 only" in x for x in p), p)
        e["trap"] = "Hooks.Nope=1"
        p, _ = self.check([("G.jsonl", e)], hooked={0x1030})
        self.assertTrue(any("no hook named Nope" in x for x in p), p)
        e["trap"] = "Hooks.H_1000=1"  # a hook, but not the one the check rests on
        p, _ = self.check([("G.jsonl", e)], hooked={0x1030, 0x1000})
        self.assertTrue(any("must name exactly the hooks" in x for x in p), p)
        e["trap"] = "Hooks.H_1030=1&Hooks.H_1000=1"  # one too many: H_1000 isn't one the check rests on
        p, _ = self.check([("G.jsonl", e)], hooked={0x1030, 0x1000})
        self.assertTrue(any("must name exactly the hooks" in x for x in p), p)
        e["trap"] = "Startup.OwnStart=1"
        p, n = self.check([("G.jsonl", e)], hooked={0x1030})
        self.assertEqual(p, [])
        self.assertTrue(any("rests on the hooks H_1030" in x for x in n), n)

    def two_hooks(self, trap):
        # T rests on two hooks: F3 (stores its vtable) and F1 (calls it; F1 itself sits in a table, so it runs)
        e = self.clean_t(trap=trap)
        del e["callers_handled"]["abs:0x2000"]
        del e["callers_handled"]["code:0x1000"]
        img, db = make_image(entry=T), make_db(status={T: e}, hooked={0x1030, 0x1000})
        img._relocs[0x1000] = [0x2018]
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "trap_table.cpp")
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(C.trap_table_text(C.trap_rows(db, img)))
            return C.check_status([("G.jsonl", e)], img, db, path), C.Checker(img, db).hook_deps(T)

    def test_conjunction_of_hook_switches(self):
        (p, n), deps = self.two_hooks("Hooks.H_1030=1&Hooks.H_1000=1")
        self.assertEqual(deps, {0x1000, 0x1030})
        self.assertEqual((p, n), ([], []))
        (p, n), _ = self.two_hooks("Hooks.H_1000=1&Hooks.H_1030=1")  # order doesn't matter
        self.assertEqual((p, n), ([], []))
        for one in ("Hooks.H_1030=1", "Hooks.H_1000=1"):  # one of the two: the other may be off while the trap is armed
            (p, _), _ = self.two_hooks(one)
            self.assertTrue(any("rests on the hooks H_1000, H_1030" in x and "must name exactly" in x for x in p), (one, p))
        (p, _), _ = self.two_hooks("Hooks.H_1030=1&Hooks.H_1000=0,1")
        self.assertTrue(any("=1 only" in x for x in p), p)
        (p, _), _ = self.two_hooks("Hooks.H_1030=1&Hooks.Nope=1")
        self.assertTrue(any("no hook named Nope" in x for x in p), p)
        (p, _), _ = self.two_hooks("Hooks.H_1030=1&Hooks.H_1030=1")
        self.assertTrue(any("needs \"trap\"" in x for x in p), p)
        (p, _), _ = self.two_hooks("Hooks.H_1030=1&Hooks.H_1000=1&Startup.OwnStart=1")  # a further condition: still safe
        self.assertEqual(p, [])
        (p, _), _ = self.two_hooks("Hooks.H_1030=1&Hooks.H_1000=1&Startup.OwnStart=0")
        self.assertEqual(p, [])

    def test_covering_entry_under_a_conjunction(self):
        f2 = callers_entry(0x1020, trap="Startup.OwnStart=1,2&Hooks.H_1030=1")
        f2["callers_handled"] = {}
        e = self.clean_t(trap="Startup.OwnStart=1")
        del e["callers_handled"]["code:0x1020"]
        p, _ = self.check([("G.jsonl", e), ("G.jsonl", f2)], hooked={0x1030})
        self.assertTrue(any("isn't replaced under every value" in x for x in p), p)  # e's trap doesn't need H_1030
        e["trap"] = "Hooks.H_1030=1&Startup.OwnStart=1"
        p, _ = self.check([("G.jsonl", e), ("G.jsonl", f2)], hooked={0x1030})
        self.assertFalse(any("isn't replaced under every value" in x for x in p), p)

    def test_xc_entry_needs_the_runners_switches(self):
        e = self.clean_t(trap=C.XC_TRAP)
        del e["callers_handled"]["abs:0x2010"]  # covered as the XC slot instead
        status = {T: e}
        img, db = make_image(entry=T), make_db(status=status, src_refs={T: [("src/app/StaticInit.cpp", 5, "hex literal")]})
        with mock.patch.object(C, "XC_TABLE", (0x2010, 0x2014)), tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "trap_table.cpp")
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(C.trap_table_text(C.trap_rows(db, img)))
            p, _ = C.check_status([("G.jsonl", e)], img, db, path)
            self.assertEqual(p, [])
            for trap in ("Startup.OwnStart=1,2", "Startup.OwnStatics=1", "Startup.OwnStart=0,1,2&Startup.OwnStatics=1"):
                e["trap"] = trap
                with open(path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(C.trap_table_text(C.trap_rows(db, img)))
                p, _ = C.check_status([("G.jsonl", e)], img, db, path)
                self.assertTrue(any("XC slot" in x for x in p), (trap, p))
            e["trap"] = "Startup.OwnStatics=1&Startup.OwnStart=2"  # narrower: still implies the runner's switches
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(C.trap_table_text(C.trap_rows(db, img)))
            p, _ = C.check_status([("G.jsonl", e)], img, db, path)
            self.assertEqual(p, [])

    def test_covering_entry_must_be_on_under_every_value(self):
        f2 = callers_entry(0x1020, trap="Startup.OwnStart=2")
        f2["callers_handled"] = {}
        e = self.clean_t()
        del e["callers_handled"]["code:0x1020"]
        p, _ = self.check([("G.jsonl", e), ("G.jsonl", f2)])
        self.assertTrue(any("isn't replaced under every value" in x for x in p), p)
        f2["trap"] = "Startup.OwnStart=1,2"
        p, _ = self.check([("G.jsonl", e), ("G.jsonl", f2)])
        self.assertEqual(p, [])
        f2["trap"] = "Other.Switch=1,2"
        p, _ = self.check([("G.jsonl", e), ("G.jsonl", f2)])
        self.assertTrue(any("isn't replaced under every value" in x for x in p), p)


def make_binary():
    """A synthetic build of ours (ourrefs.Binary without a file): code and data holding the test vtable 0x2000 and the
    target T in the forms our code uses, plus look-alikes that are not values. Objects as the map names them
    (lib:object for ts2core)."""
    import ourrefs
    gb = ourrefs.game_base()
    b = ourrefs.Binary.__new__(ourrefs.Binary)
    b.exe = C.Path("ts2host.exe")
    b.base = 0x400000
    code = bytearray(b"\xcc" * 0x40)
    code[0:6] = b"\xc7\x01" + struct.pack("<I", 0x2000)                 # 401000 Build: mov [ecx], 0x2000 (the bare RVA)
    code[6:7] = b"\xc3"
    code[0x10:0x16] = b"\xb0\x00\x20\x00\x00\x00"                         # 401010 Other: mov al,0 ; and [eax],al ; add [eax],al
    code[0x16:0x17] = b"\xc3"                                            #   (00 20 00 00 straddles instructions: not a value)
    code[0x30:0x35] = b"\x68" + struct.pack("<I", gb + T)                # 401030 Fn: push game address of T
    data = bytearray(0x30)
    data[0:4] = struct.pack("<I", 1)
    data[4:8] = struct.pack("<I", gb + 0x2000)                           # 402000 kTable: a row holding the game address
    data[0x11:0x15] = struct.pack("<I", 0x2000)                          # 402011 in an unwind map, unaligned: packed metadata
    data[0x18:0x1c] = struct.pack("<I", 0x2000)                          # 402018 aligned (maybe a symbol-less table of ours)
    data[0x2c:0x30] = struct.pack("<I", gb + 0x2000)                     # 40202c a hook row's CRC that looks like one
    b.sections = [(".text", 0x401000, True, bytes(code)), (".rdata", 0x402000, False, bytes(data))]
    b.symbols = [(0x401000, "?Build@XPrimParam@@SGPAV1@I@Z", "ts2core:SimAnticsParams.size.obj"),
                 (0x401010, "?Other@A@@QAEXXZ", "ts2core:a.obj"),
                 (0x401030, "?Fn@B@@QAEXXZ", "ts2core:b.obj"),
                 (0x402000, "?kTable@@3QBUEntry@@B", "ts2core:SimAnticsParams.size.obj"),
                 (0x402010, "unwindtable$?Fn@B@@QAEXXZ", "ts2core:b.obj"),
                 (0x402020, "?kHooks@?A0x1@@3QBUHookSpec@@B", "COMDirectors.obj")]
    b._starts = [s[0] for s in b.symbols]
    b._decoded = {}
    return b


class BinaryScan(unittest.TestCase):
    """v1 saw a vtable only in GameAddress(<literal or k-constant>) (lane GT's SimAnticsParams drop went through it: our
    XPrimParam::BuildForOpcode stored the game's vtables from a table of RVAs through a helper). The scan of our build
    sees every form."""

    def test_scan_finds_values_and_skips_look_alikes(self):
        import ourrefs
        b, gb = make_binary(), ourrefs.game_base()
        hits = b.scan({0x2000, gb + 0x2000, gb + T})
        self.assertEqual([h[:3] for h in hits[0x2000]], [(0x401002, "?Build@XPrimParam@@SGPAV1@I@Z",
                                                          "ts2core:SimAnticsParams.size.obj"),
                                                         (0x402018, "unwindtable$?Fn@B@@QAEXXZ", "ts2core:b.obj")])
        self.assertEqual([h[0] for h in hits[gb + 0x2000]], [0x402004, 0x40202c])  # the table row (data); the CRC
        self.assertEqual([h[0] for h in hits[gb + T]], [0x401031])

    def test_vtable_held_by_our_build_is_open(self):
        db = make_db(hooked={0x1030}, binary=make_binary())
        r = rows(C.Checker(make_image(), db).verdict(T))
        self.assertFalse(r["abs:0x2000"]["covered"])
        self.assertIn("XPrimParam::Build (ts2core:SimAnticsParams.size.obj): an operand holds the RVA 0x2000",
                      r["abs:0x2000"]["why"])
        # our build holding the target's game address is a reference of ours, never neutralised
        bin_rows = [s for s in r if s.startswith("bin:")]
        self.assertEqual(bin_rows, ["bin:0x401031"])
        self.assertFalse(r["bin:0x401031"]["covered"])

    def test_a_pointer_into_the_vtable_counts(self):
        # our code reading a slot of the game's vtable (GameAddress(kVTable + 4)): the build holds an interior address
        import ourrefs
        b = make_binary()
        b.sections = [(n, va, x, b"\0" * len(d)) for n, va, x, d in b.sections]
        data = bytearray(0x20)
        data[4:8] = struct.pack("<I", ourrefs.game_base() + 0x2004)
        b.sections[1] = (".rdata", 0x402000, False, bytes(data))
        db = make_db(hooked={0x1030}, binary=b, vtables=[(0x2000, 0x2008, "cThing")])
        self.assertIn(0x2000, db.vtable_uses)
        self.assertFalse(rows(C.Checker(make_image(), db).verdict(T))["abs:0x2000"]["covered"])

    def test_a_hook_rows_crc_is_not_a_reference(self):
        # COMDirectors_ResourceW32_Get's CRC 0xa0dcc66c lies in cTSNeighborManager's vtable 0xdcc5e4: before this rule
        # it made 24 entries look live
        import ourrefs
        gb = ourrefs.game_base()

        def only_crc(obj):
            b = make_binary()
            data = bytearray(0x30)
            data[0x2c:0x30] = struct.pack("<I", gb + 0x2000)
            b.sections = [(n, va, x, b"\xcc" * len(d)) if x else (n, va, x, bytes(data)) for n, va, x, d in b.sections]
            b.symbols = [s if s[0] != 0x402020 else (s[0], s[1], obj) for s in b.symbols]
            return b

        def live(b, crcs):
            db = C.Db.__new__(C.Db)
            db.hook_crcs = crcs
            db.__dict__.update({k: v for k, v in make_db(hooked={0x1030}).__dict__.items()
                                if k not in ("vtable_uses", "hook_crcs")})
            return 0x2000 in db._vtable_uses(b)

        self.assertFalse(live(only_crc("COMDirectors.obj"), {gb + 0x2000}))
        self.assertTrue(live(only_crc("COMDirectors.obj"), set()))  # not a known CRC
        self.assertTrue(live(only_crc("ts2core:COMDirectors.obj"), {gb + 0x2000}))  # our code's data, not a hook row

    def test_with_a_build_unused_literals_dont_count(self):
        b = make_binary()
        b.sections = [(n, va, x, b"\0" * len(d)) for n, va, x, d in b.sections]  # a build that holds nothing
        db = make_db(hooked={0x1030}, src_refs={0x2000: [("src/x.h", 3, "hex literal")]}, binary=b)
        self.assertTrue(rows(C.Checker(make_image(), db).verdict(T))["abs:0x2000"]["covered"])
        db = make_db(hooked={0x1030}, game_address_uses={0x2000: [("src/x.cpp", 3)]}, binary=b)
        self.assertFalse(rows(C.Checker(make_image(), db).verdict(T))["abs:0x2000"]["covered"])

    def test_missing_build_is_a_problem_when_a_verdict_rests_on_a_vtable(self):
        e = callers_entry(T, trap="Hooks.H_1030=1", callers_handled={
            "pe:entry": "host", "code:0x1000": "x", "code:0x1020": "x", "code:0x1041": "x", "abs:0x2010": "x"})
        img, db = make_image(entry=T), make_db(status={T: e}, hooked={0x1030})
        db.binary_note = "no build of our host"
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "trap_table.cpp")
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(C.trap_table_text(C.trap_rows(db, img)))
            p, n = C.check_status([("G.jsonl", e)], img, db, path)
            self.assertTrue(any("no build of our host" in x and "rest on vtable slots" in x for x in p), p)
            e["callers_handled"]["abs:0x2000"] = "x"  # no vtable verdict left: a note only
            p, n = C.check_status([("G.jsonl", e)], img, db, path)
            self.assertFalse(any("no build" in x for x in p), p)
            self.assertTrue(any("no build of our host" in x for x in n), n)


# The 16 rows lane GT dropped (reverted in 8e52f9b): SimAnticsParams Read wrappers, Convert slots, XPrimParam::Write.
GT_DROPPED = [0xc0ab29, 0xc0ab4c, 0xc0ab83, 0xc0abf3, 0xc0accb, 0xc0ad18, 0xc0ad32, 0xc0ad87, 0xc0addb, 0xc0ae3f,
              0xc0ae6e, 0xc0afd2, 0xc0afe6, 0xc0b02b, 0xc0b072, 0xc0b0b7]
HOST = C.Path(C.tsdb.ROOT) / "build/Release/ts2host.exe"


@unittest.skipUnless(HOST.exists() and C.tsdb.unpacked_exe().exists(), "needs our host build and the unpacked exe")
class SimAnticsParamsRegression(unittest.TestCase):
    """Lane GT's plan, replayed on the real image and our real build: drop the 16 hook rows and trap the originals.
    callers.py must refuse (open references: the param vtables our BuildForOpcode stores)."""

    @classmethod
    def setUpClass(cls):
        cls.img = C.Image()
        cls.db = C.Db(binary=HOST, image=cls.img)

    def plan(self, vtable_uses):
        db = C.Db.__new__(C.Db)
        db.__dict__.update(self.db.__dict__)
        db.hooked = self.db.hooked - set(GT_DROPPED)
        db.vtable_uses = vtable_uses
        ck = C.Checker(self.img, db, assume=GT_DROPPED)
        return {r: ck.verdict(r) for r in GT_DROPPED}

    def test_the_param_vtables_are_held_by_our_build(self):
        uses = self.db.vtable_uses
        self.assertTrue(any("SimAnticsParams.size.obj" in w for w in uses.get(0xdc923c, [])), uses.get(0xdc923c))
        self.assertTrue(any("SimAnticsParams.size.obj" in w for w in uses.get(0xdc93f4, [])), uses.get(0xdc93f4))

    def test_the_drop_is_refused(self):
        verdicts = self.plan(self.db.vtable_uses)
        for rva, v in verdicts.items():
            vt_open = [r for r in v["references"] if not r["covered"] and "SimAnticsParams" in r["why"]]
            self.assertTrue(vt_open, f"{rva:#x}: {v['open']}")
        # v1's rule (GameAddress uses only) let it through: the test can fail
        old = {}
        for value, sites in self.db.game_address_uses.items():
            vt = self.db.vtable_at(value)
            if vt:
                old.setdefault(vt[0], []).extend(f"{p}:{l}" for p, l in sites)
        self.assertFalse(any(r["covered"] is False and "SimAnticsParams" in r["why"]
                             for v in self.plan(old).values() for r in v["references"]))
        self.assertTrue(any(not any(not r["covered"] for r in v["references"] if r["site"].startswith("abs:"))
                            for v in self.plan(old).values()))


if __name__ == "__main__":
    unittest.main()
