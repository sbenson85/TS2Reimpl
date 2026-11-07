#!/usr/bin/env python3
"""Tests for tools/matchcheck.py v7: vtables of classes in the standalone shape.

 - our vtable reference stands for the original's vftable of the same RTTI class and subobject offset, with the same
   slot count and no slot known to be another function;
 - vtable ties: a hook whose adapter only forwards (`[[msvc::forceinline_calls]] return self->X::f(args)`) ties the
   RVA to the member our vtable holds in that slot, for that face.

Synthetic only (hand-built objects and vftable facts); no game bytes.

Run: .venv/Scripts/python -I tools/tests/test_matchcheck_v7.py
"""
import os
import struct
import sys
import unittest
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import matchcheck as M  # noqa: E402
from test_matchcheck_v6 import RDATA, DATA, COMDAT, checker, objfile, original  # noqa: E402

BAR, BAZ = "?Bar@Foo@@UAEXXZ", "?Baz@Foo@@UAEHH@Z"  # void Foo::Bar(), int Foo::Baz(int)
BAR_THUNK = "?Bar@Foo@@W3AEXXZ"                         # [thunk] Foo::Bar`adjustor{4}'


def vtable_obj(slots=(BAR, BAZ), offset=0, name="??_7Foo@@6B@", mangled=b".?AVFoo@@"):
    """Our vtable `name` (locator word, then one DIR32 per slot) with its locator (subobject offset) and type
    descriptor, as MSVC lays them out."""
    n = len(slots)
    vt = (".rdata", b"\0" * (4 + 4 * n), RDATA | COMDAT,
          [(0, 1, M.DIR32)] + [(4 + 4 * k, 3 + k, M.DIR32) for k in range(n)])
    col = (".rdata", struct.pack("<5I", 0, offset, 0, 0, 0), RDATA | COMDAT, [(12, 2, M.DIR32)])
    td = (".data", b"\0" * 8 + mangled + b"\0", DATA | COMDAT, [])
    syms = [(name, 4, 0, 2, 0), ("??_R4Foo@@6B@", 0, 1, 2, 0), ("??_R0?AVFoo@@@8", 0, 2, 2, 0)] + \
           [(s, 0, None, 2, 0x20) for s in slots]
    return objfile([vt, col, td], syms)


def vchecker(entries=("0x800", "0x900"), offset=0):
    ck = checker(original({}))
    ck.vftables = {(".?AVFoo@@", offset): {"rva": "0x700", "offset": offset, "entries": list(entries)}}
    return ck


ADAPTERS = """
using A = FooAdapters;
using Self = Foo;
void __fastcall A::Bar(Self *self, void *) { [[msvc::forceinline_calls]] self->Self::Bar(); }
int __fastcall A::Baz(Self *self, void *, int x) { [[msvc::forceinline_calls]] return self->Self::Baz(x); }
void __fastcall A::FaceBar(IFace *face, void *) { [[msvc::forceinline_calls]] static_cast<Self *>(face)->Self::Bar(); }
void __fastcall A::Virtual(Self *self, void *) { [[msvc::forceinline_calls]] self->Bar(); }
int __fastcall A::Swapped(Self *self, void *, int x, int y) { [[msvc::forceinline_calls]] return self->Self::Baz(y, x); }
"""
AD = {m: f"?{m}@FooAdapters@@SIXPAVFoo@@PAX@Z" for m in ("Bar", "Baz", "FaceBar", "Virtual", "Swapped", "Missing")}


class VtableReference(unittest.TestCase):
    def test_same_class_face_and_slots(self):
        self.assertIsNone(vchecker().data_ref(vtable_obj(), 0, 0, 0x700))
        self.assertIsNone(vchecker().data_ref(vtable_obj(), 0, 4, 0x704))  # a slot's address

    def test_refusals(self):
        self.assertIn("vs original", vchecker().data_ref(vtable_obj(), 0, 0, 0x704))
        self.assertIn("vs original", vchecker().data_ref(vtable_obj(), 0, 8, 0x708))  # past the last slot
        self.assertIn("3", vchecker(entries=("0x800", "0x900", "0xa00")).data_ref(vtable_obj(), 0, 0, 0x700))
        self.assertIn("no vftable", vchecker().data_ref(vtable_obj(offset=4), 0, 0, 0x700))
        self.assertIn("no vftable", vchecker().data_ref(vtable_obj(mangled=b".?AVBar@@"), 0, 0, 0x700))

    def test_no_locator_is_refused(self):
        obj = objfile([(".rdata", b"\0" * 8, RDATA, [])], [("??_7Foo@@6B@", 0, 0, 2, 0)])
        self.assertIn("locator", vchecker().data_ref(obj, 0, 0, 0x700))

    def test_slot_conflicts(self):
        ck = vchecker()
        ck.impl_rvas[BAR] = {"0x900"}  # our Bar replaces the original's slot 1, but sits in slot 0
        self.assertIn("slot 0", ck.data_ref(vtable_obj(), 0, 0, 0x700))
        ck = vchecker()
        ck.forwards = {"0x900": dict(cls="Foo", member="Bar")}  # slot 1's hook forwards to Bar, ours holds Baz
        self.assertIn("forwards to Foo::Bar", ck.data_ref(vtable_obj(), 0, 0, 0x700))
        ck.forwards = {"0x900": dict(cls="Foo", member="Baz")}
        self.assertIsNone(ck.data_ref(vtable_obj(), 0, 0, 0x700))


class AdapterTarget(unittest.TestCase):
    def test_forwarders(self):
        t = M.adapter_target(AD["Bar"], {"a.cpp": ADAPTERS})
        self.assertEqual((t["cls"], t["member"], t["arity"], t["receiver"], t["cast"]), ("Foo", "Bar", 0, "Foo", False))
        t = M.adapter_target(AD["Baz"], {"a.cpp": ADAPTERS})
        self.assertEqual((t["member"], t["arity"]), ("Baz", 1))
        t = M.adapter_target(AD["FaceBar"], {"a.cpp": ADAPTERS})
        self.assertEqual((t["receiver"], t["cast"]), ("IFace", True))

    def test_not_forwarders(self):
        self.assertIsInstance(M.adapter_target(AD["Virtual"], {"a.cpp": ADAPTERS}), str)   # virtual dispatch
        self.assertIsInstance(M.adapter_target(AD["Swapped"], {"a.cpp": ADAPTERS}), str)   # arguments reordered
        self.assertIsInstance(M.adapter_target(AD["Missing"], {"a.cpp": ADAPTERS}), str)
        self.assertIsInstance(M.adapter_target(AD["Bar"], {"a.cpp": ADAPTERS, "b.cpp": ADAPTERS}), str)  # two
        two_selves = ADAPTERS + "using Self = Other;\n"
        self.assertIsInstance(M.adapter_target(AD["Bar"], {"a.cpp": two_selves}), str)

    def test_signature_helpers(self):
        self.assertEqual(M.member_of(BAR), ("Foo", "Bar"))
        self.assertEqual(M.member_of(BAR_THUNK), ("Foo", "Bar"))
        self.assertEqual(M.arity(BAR), 0)
        self.assertEqual(M.arity(BAZ), 1)


class Ties(unittest.TestCase):
    def ties(self, targets, obj=None, ck=None):
        ck = ck or vchecker()
        ck.impl_rvas = defaultdict(set)
        return ck.find_ties([obj or vtable_obj()], targets, {"a.cpp": ADAPTERS}), ck

    def test_tie(self):
        ties, ck = self.ties({"0x800": AD["Bar"], "0x900": AD["Baz"]})
        self.assertEqual(ties, {"0x800": [BAR], "0x900": [BAZ]})
        self.assertEqual(ck.impl_rvas[BAR], {"0x800"})  # rule 2: calls to our Bar are calls to 0x800

    def test_no_tie(self):
        self.assertEqual(self.ties({"0x800": AD["Baz"]})[0], {})      # forwards to another member than slot 0's
        self.assertEqual(self.ties({"0x800": AD["Virtual"]})[0], {})  # not a forwarder
        # slot 0 of a secondary face (+4): the adapter must take that face and cast back
        ck = vchecker(offset=4)
        self.assertEqual(self.ties({"0x800": AD["Bar"]}, vtable_obj((BAR_THUNK, BAZ), 4, "??_7Foo@@6BIFace@@@"), ck)[0], {})
        ck = vchecker(offset=4)
        ties = self.ties({"0x800": AD["FaceBar"]}, vtable_obj((BAR_THUNK, BAZ), 4, "??_7Foo@@6BIFace@@@"), ck)[0]
        self.assertEqual(ties, {"0x800": [BAR_THUNK]})


NS_BAR, NS_BAZ = "?Bar@Foo@gz@@UAEXXZ", "?Baz@Foo@gz@@UAEHH@Z"  # gz::Foo::Bar(), gz::Foo::Baz(int)
NS_BAR_THUNK = "?Bar@Foo@gz@@W3AEXXZ"


def ns_vtable(slots=(NS_BAR, NS_BAZ), offset=0, name="??_7Foo@gz@@6B@"):
    return vtable_obj(slots, offset, name, b".?AVFoo@gz@@")


class Namespaces(unittest.TestCase):
    """v8: our class in a namespace is the original's class of the same unqualified name, unless that is ambiguous."""

    def tearDown(self):
        M.Checker.use_v8 = True

    def test_bare_names(self):
        self.assertEqual(M.rtti_bare(".?AVcGZFramework@gz@@"), ".?AVcGZFramework@@")
        self.assertEqual(M.rtti_bare(".?AVcGZFramework@@"), ".?AVcGZFramework@@")
        self.assertEqual(M.rtti_bare(".?AUInner@Outer@ns@@"), ".?AUInner@@")
        self.assertIsNone(M.rtti_bare(".?AV?$cT@H@rz@@"))  # templates: exact names only
        self.assertIsNone(M.rtti_bare(".?AVFoo@?A0x1234@@"))  # anonymous namespace

    def test_reference_and_tie_through_namespace(self):
        ck = vchecker()
        self.assertEqual(ck.game_class(".?AVFoo@gz@@"), (".?AVFoo@@", None))
        self.assertIsNone(ck.data_ref(ns_vtable(), 0, 0, 0x700))
        ck.impl_rvas = defaultdict(set)
        ties = ck.find_ties([ns_vtable()], {"0x800": AD["Bar"], "0x900": AD["Baz"]}, {"a.cpp": ADAPTERS})
        self.assertEqual(ties, {"0x800": [NS_BAR], "0x900": [NS_BAZ]})
        self.assertEqual(ck.tie_refused, {})

    def test_rules7_needs_the_exact_name(self):
        M.Checker.use_v8 = False
        ck = vchecker()
        self.assertIn("no vftable", ck.data_ref(ns_vtable(), 0, 0, 0x700))
        ck.impl_rvas = defaultdict(set)
        self.assertEqual(ck.find_ties([ns_vtable()], {"0x800": AD["Bar"]}, {"a.cpp": ADAPTERS}), {})

    def test_two_original_classes_refused(self):
        ck = vchecker()
        ck.vftables = dict(ck.vftables)
        ck.vftables[(".?AVFoo@nOther@@", 0)] = {"rva": "0x780", "offset": 0, "entries": ["0x800", "0x900"]}
        self.assertIn("ambiguous", ck.data_ref(ns_vtable(), 0, 0, 0x700))
        self.assertIn("2 original classes", ck.tie_refused[".?AVFoo@gz@@"])
        ck.impl_rvas = defaultdict(set)
        self.assertEqual(ck.find_ties([ns_vtable()], {"0x800": AD["Bar"]}, {"a.cpp": ADAPTERS}), {})
        # an exact name still ties as in v7 (a new checker: our_vtable caches by object path and symbol)
        ck2 = vchecker()
        ck2.vftables = ck.vftables
        self.assertIsNone(ck2.data_ref(vtable_obj(), 0, 0, 0x700))

    def test_two_of_ours_refused(self):
        for other in (".?AVFoo@rz@@", ".?AVFoo@@"):  # another namespace, or a global class of that name
            ck = vchecker()
            ck.note_our_classes({".?AVFoo@gz@@", other})
            why = ck.game_class(".?AVFoo@gz@@")[1]
            self.assertIn("our classes", why)
            self.assertIn(other, why)
            self.assertIn("ambiguous", ck.data_ref(ns_vtable(), 0, 0, 0x700))
            ck.impl_rvas = defaultdict(set)
            self.assertEqual(ck.find_ties([ns_vtable()], {"0x800": AD["Bar"]}, {"a.cpp": ADAPTERS}), {})
        ck = vchecker()
        ck.note_our_classes({".?AVFoo@gz@@", ".?AVFoo@@"})
        self.assertIsNone(ck.data_ref(vtable_obj(), 0, 0, 0x700))  # the exact one keeps its v7 tie

    def test_qualified_face(self):
        for face in ("??_7Foo@gz@@6BIFace@gz@@@", "??_7Foo@gz@@6BIFace@1@@"):  # back-reference to gz
            ck = vchecker(offset=4)
            obj = ns_vtable((NS_BAR_THUNK, NS_BAZ), 4, face)
            self.assertEqual(ck.our_vtable(obj, 0)["face"], "IFace")
            ck.impl_rvas = defaultdict(set)
            ties = ck.find_ties([obj], {"0x800": AD["FaceBar"]}, {"a.cpp": ADAPTERS})
            self.assertEqual(ties, {"0x800": [NS_BAR_THUNK]})
        self.assertIsNone(vchecker().our_vtable(ns_vtable(name="??_7Foo@gz@@6BA@@B@@@"), 0)["face"])  # two bases
        M.Checker.use_v8 = False
        self.assertIsNone(vchecker().our_vtable(ns_vtable(name="??_7Foo@gz@@6BIFace@gz@@@"), 0)["face"])

    def test_rtti_classes_of_an_object_file(self):
        import tempfile
        from pathlib import Path
        names = [b"??_R0?AVFoo@gz@@@8", b"??_R0?AVExtern@@@8", b"??_7Foo@gz@@6B@"]
        strtab = b"".join(n + b"\0" for n in names)
        syms, off = b"", 4
        for n, sec in zip(names, (1, 0, 1)):  # the second is undefined (section 0)
            syms += struct.pack("<II", 0, off) + struct.pack("<IhHBB", 0, sec, 0, 2, 0)
            off += len(n) + 1
        data = struct.pack("<HHIIIHH", 0x14c, 0, 0, 20, 3, 0, 0) + syms + struct.pack("<I", 4 + len(strtab)) + strtab
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "t.obj"
            p.write_bytes(data)
            self.assertEqual(M.obj_rtti_classes(p), {".?AVFoo@gz@@"})


class V6Verdict(unittest.TestCase):
    """--brief prints v6's verdict next to v7's: as_v6() applies v6's rules on the same facts."""

    def test_as_v6_refuses_vtable_references_and_drops_ties(self):
        ck = vchecker()
        ck.impl_rvas = defaultdict(set)
        targets = {"0x800": AD["Bar"], "0x900": AD["Baz"]}
        ck.set_hooks(targets)
        ck.find_ties([vtable_obj()], targets, {"a.cpp": ADAPTERS})
        v6 = ck.as_v6(targets)
        self.assertIn("named data", v6.data_ref(vtable_obj(), 0, 0, 0x700))
        self.assertEqual(v6.impl_rvas[BAR], set())  # no tie: calls to our Bar aren't calls to 0x800 under v6
        self.assertEqual(v6.impl_rvas[AD["Bar"]], {"0x800"})
        # the v7 checker is unchanged
        self.assertIsNone(ck.data_ref(vtable_obj(), 0, 0, 0x700))
        self.assertEqual(ck.impl_rvas[BAR], {"0x800"})

    def test_brief_columns(self):
        match = {"match": True, "score": 1.0, "reasons": []}
        miss = {"match": False, "score": 0.91234, "reasons": ["our vtable reference vs original 0x700"]}
        self.assertEqual(M.brief_verdict(match, None), "MATCH")
        self.assertEqual(M.brief_verdict(match, "stale object"), "MATCH*")
        self.assertEqual(M.brief_verdict(miss, None), "0.912")
        self.assertEqual(M.brief_v6(miss, None), "0.912 our vtable reference vs original 0x700")


if __name__ == "__main__":
    unittest.main()
