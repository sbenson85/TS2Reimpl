#!/usr/bin/env python3
"""Tests for tools/slotarity.py (arity.py's "slot call" kind: RawSlot / VirtualCall / VirtualSlot call sites against
the receiver's slot body `ret N`) and the guard's warning for the calls it can't resolve. Synthetic sources, classes and
rets: no game bytes.

Run: tools/py -I -m pytest tools/tests/test_slotarity.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import arity  # noqa: E402
import guard  # noqa: E402

# A buffer interface (cIBuf) with two implementations that agree, and decoys: classes whose slots pop other amounts.
# Slot bodies are numbered 0x1000 * class + slot; RETS gives each one's `ret N`.
BUF = {3: 12, 6: 4, 7: 4, 9: 0, 10: 0, 18: 8, 21: 8, 30: 12, 32: 16, 33: 20}
DECOY_A = {3: 16, 6: 4, 7: 4, 9: 0, 10: 0, 18: 4, 21: 24, 30: 24, 32: 20, 33: 4}  # KS's wrong counts, few others
DECOY_B = {3: 0, 6: 8, 7: 0, 9: 4, 10: 4, 18: 0, 21: 0, 30: 0, 32: 0, 33: 0}


def vtable(cls, pops, n=40):
    return [cls * 0x1000 + s for s in range(n)], {cls * 0x1000 + s: {pops.get(s, 0)} for s in range(n)}


def build(spec):
    """(classes, rets) for [(name, bases, pops)]."""
    classes, rets = [], {}
    for i, (name, bases, pops) in enumerate(spec, 1):
        es, rs = vtable(i, pops)
        rets.update(rs)
        classes.append({"name": name, "bases": bases, "base_offsets": [0] * len(bases),
                        "vftables": [{"rva": hex(0x900000 + i), "offset": 0, "entries": [hex(e) for e in es]}]})
    return classes, rets


CLASSES, RETS = build([("cBufA", ["cIBuf", "cIGZUnknown"], BUF), ("cBufB", ["cIBuf", "cIGZUnknown"], BUF),
                       ("cDecoyA", ["cIGZUnknown"], DECOY_A), ("cDecoyB", ["cIGZUnknown"], DECOY_B)])
HEADER = ("#pragma once\n#include <cstdint>\nstruct cIGZUnknown;\nstruct cIBuf;\n"
          "template <int S, class R = uint32_t, class... A> R RawSlot(const void *object, A... a);\n")


def run(body, classes=CLASSES, rets=RETS, extra_files=None):
    files = {"src/game/vtable.h": HEADER, "src/x/cUser.cpp": '#include "game/vtable.h"\n' + body}
    files.update(extra_files or {})
    found = arity.check(list(files.items()), lambda rva: rets.get(rva, set()), None, ["src/x/cUser.cpp"], classes, [])
    return [f for f in found if f["kind"] == "slot call"]


def by_line(found):
    """{(slot, receiver text): verdict} (the line's site text, as `where` names it)."""
    out = {}
    for f in found:
        site = f["where"].split(" ", 1)[1]
        out.setdefault(site, set()).add(f["verdict"])
    return {k: (next(iter(v)) if len(v) == 1 else tuple(sorted(v))) for k, v in out.items()}


class TypedReceivers(unittest.TestCase):
    def test_declared_interface_pointer(self):
        ok = run("void F(cIBuf *buf) { RawSlot<21>(buf, 1u, 2u); }\n")
        self.assertEqual([f["verdict"] for f in ok], ["ok"], ok)
        bad = run("void F(cIBuf *buf) { RawSlot<21>(buf, 1u, 2u, 3u); }\n")
        self.assertEqual([f["verdict"] for f in bad], ["MISMATCH"], bad)
        self.assertIn("original ret 8", bad[0]["detail"])

    def test_auto_ref_count_and_alias(self):
        src = ("template <class T> struct cRZAutoRefCount { T *Get() const; };\nusing BufRef = cRZAutoRefCount<cIBuf>;\n"
               "void F() { BufRef b; RawSlot<33, bool>(b.Get(), 0u, nullptr, nullptr, nullptr, nullptr); }\n")
        self.assertEqual([f["verdict"] for f in run(src)], ["ok"])

    def test_cast_in_the_call(self):
        found = run("void F(void *p) { RawSlot<9>(static_cast<cIBuf *>(p), 1u); }\n")
        self.assertEqual([f["verdict"] for f in found], ["MISMATCH"])

    def test_a_long_continued_declarator_list_before_the_receiver_is_fast(self):
        # Lane LO's IAT constants: a constexpr list continued on indented lines made the declaration search backtrack
        # exponentially (the indent matched by both `,\s*` and the next declarator's leading `[*&\s]*`): minutes per file.
        consts = ",\n                   ".join(f"kIatName{i} = 0x{0xd9a100 + 4 * i:x}" for i in range(12))
        src = f"constexpr uint32_t {consts};\nvoid F(cIBuf *buf) {{ RawSlot<21>(buf, 1u, 2u); }}\n"
        import time
        start = time.monotonic()
        found = run(src)
        self.assertLess(time.monotonic() - start, 5)
        self.assertEqual([f["verdict"] for f in found], ["ok"], found)

    def test_this_is_the_enclosing_class(self):
        src = "struct cBufA { void F(); };\nvoid cBufA::F() { RawSlot<18>(this, 0u, 0u); RawSlot<18>(this, 0u); }\n"
        self.assertEqual([f["verdict"] for f in run(src)], ["ok", "MISMATCH"])

    def test_this_of_a_class_with_a_disagreeing_subobject_is_unknown(self):
        # a member hooked at the +8 vtable gets the subobject as `this`; slot 5 pops 0 at offset 0 but 16 at +8
        classes, rets = build([("cMulti", ["cIA"], {5: 0, 6: 4})])
        sub = [0x7000 + s for s in range(40)]
        rets.update({0x7000 + s: {16 if s == 5 else 4} for s in range(40)})
        classes[0]["bases"].append("cIB")
        classes[0]["base_offsets"].append(8)
        classes[0]["vftables"].append({"rva": "0x9f0000", "offset": 8, "entries": [hex(e) for e in sub]})
        src = ("struct cMulti { void F(); };\n"
               "void cMulti::F() { VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), "
               "5>(this, 1u, 2u, 3u, 4u); RawSlot<6>(this, 1u); }\n")
        self.assertEqual([f["verdict"] for f in run(src, classes, rets)], ["unknown", "ok"])

    def test_an_interface_that_is_a_secondary_base_takes_its_subobject_vtable(self):
        classes, rets = build([("cImpl", ["cIA"], {8: 4})])
        sub = [0x7000 + s for s in range(40)]
        rets.update({0x7000 + s: {8 if s == 8 else 0} for s in range(40)})
        classes[0]["bases"].append("cIRec")
        classes[0]["base_offsets"].append(4)
        classes[0]["vftables"].append({"rva": "0x9f0000", "offset": 4, "entries": [hex(e) for e in sub]})
        facts = [{"iface": "cIRec", "class": "cImpl", "slot": 8, "params": []}]
        files = {"src/game/vtable.h": HEADER,
                 "src/x/cUser.cpp": '#include "game/vtable.h"\nvoid F(ifaces::cIRec *r) { RawSlot<8>(r, 1u, 2u); }\n'}
        found = arity.check(list(files.items()), lambda rva: rets.get(rva, set()), None, ["src/x/cUser.cpp"], classes,
                            facts)
        self.assertEqual([f["verdict"] for f in found if f["kind"] == "slot call"], ["ok"], found)

    def test_hint_comment_names_the_class(self):
        src = "void F(void *p) {\n    RawSlot<30>(p, 1u, 2u, 3u); // receiver: cIBuf\n    RawSlot<30>(p, 1u);\n}\n"
        found = run(src)
        self.assertEqual(found[0]["verdict"], "ok", found)

    def test_implementations_that_disagree_fall_back_to_inference(self):
        classes, rets = build([("cOne", ["cIOdd"], {5: 4}), ("cTwo", ["cIOdd"], {5: 8})])
        found = run("void F(cIOdd *p) { RawSlot<5>(p, 1u); }\n", classes, rets)
        self.assertEqual([f["verdict"] for f in found], ["unknown"])


class Inference(unittest.TestCase):
    # Lane KS's shape: an untyped image, called at many slots; the nested pairs split wrongly.
    KS = ("void G(void *image, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a, uint32_t x, uint32_t y) {\n"
          "    RawSlot<3, bool>(image, 1u, 2u, 3u);\n"
          "    RawSlot<6, bool>(image, 1u);\n"
          "    RawSlot<9>(image);\n"
          "    RawSlot<10>(image);\n"
          "    RawSlot<33, bool>(image, RawSlot<21>(image, x, y, r, g, b, a));\n"
          "    RawSlot<18>(image, RawSlot<32>(image, 1u, 2u, 3u, 4u, 5u));\n"
          "    RawSlot<7>(image, 1u);\n"
          "}\n")
    FIXED = KS.replace("RawSlot<21>(image, x, y, r, g, b, a))", "RawSlot<21>(image, x, y), r, g, b, a)") \
              .replace("RawSlot<32>(image, 1u, 2u, 3u, 4u, 5u))", "RawSlot<32>(image, 1u, 2u, 3u, 4u), 5u)")

    def test_nested_split_is_flagged_call_by_call(self):
        got = by_line(run(self.KS))
        for slot in (21, 33, 18, 32):
            self.assertEqual(got[f"RawSlot<{slot}> on image"], "MISMATCH", got)
        for slot in (3, 6, 7, 9, 10):
            self.assertEqual(got[f"RawSlot<{slot}> on image"], "ok", got)

    def test_the_fixed_split_passes(self):
        got = by_line(run(self.FIXED))
        self.assertEqual(set(got.values()), {"ok"}, got)

    def test_too_few_calls_identify_nothing(self):
        found = run("void G(void *p) { RawSlot<21>(p, 1u, 2u, 3u, 4u, 5u, 6u); RawSlot<9>(p); }\n")
        self.assertEqual({f["verdict"] for f in found}, {"unknown"}, found)

    def test_receivers_are_grouped_by_name_across_the_file(self):
        # the calls in G identify the class; H's wrong call on the same name is judged with them when H's other calls
        # confirm it (two too few to identify a class on their own)
        src = self.FIXED + "void H(void *image) { RawSlot<6>(image, 1u); RawSlot<9>(image); RawSlot<30>(image, 1u); }\n"
        got = by_line(run(src))
        self.assertEqual(got["RawSlot<30> on image"], "MISMATCH", got)

    def test_a_lone_call_on_a_reused_name_is_only_probable(self):
        found = [f for f in run(self.FIXED + "void H(void *image) { RawSlot<30>(image, 1u); }\n")
                 if "RawSlot<30>" in f["where"]]
        self.assertEqual([f["verdict"] for f in found], ["unknown"], found)
        self.assertIn("probable mismatch", found[0]["detail"])

    def test_a_poor_fit_is_only_probable(self):
        # half the calls misfit the best class: the class is a guess
        src = self.KS.replace("    RawSlot<7>(image, 1u);\n", "    RawSlot<7>(image, 1u, 2u);\n    RawSlot<3>(image);\n"
                                                                 "    RawSlot<6>(image);\n    RawSlot<30>(image);\n")
        found = [f for f in run(src) if "RawSlot<21>" in f["where"]]
        self.assertEqual([f["verdict"] for f in found], ["unknown"], found)
        self.assertIn("probable mismatch", found[0]["detail"])

    def test_receiver_none_silences_the_inference(self):
        src = self.KS.replace("RawSlot<21>(image, x, y, r, g, b, a));", "RawSlot<21>(image, x, y, r, g, b, a)); "
                                                                           "// receiver: none")
        found = [f for f in run(src) if "RawSlot<21>" in f["where"] or "RawSlot<33>" in f["where"]]
        self.assertEqual([f["verdict"] for f in found], ["unknown", "unknown"], found)
        self.assertTrue(all("receiver: none" in f["detail"] for f in found), found)

    def test_a_member_of_another_struct_type_is_another_object(self):
        # JobA's ref is the buffer (G's calls identify it); JobB's ref of the same name is something else
        body = self.FIXED.replace("void G(void *image,", "struct JobA { void *ref; };\nvoid G(JobA *job,") \
                         .replace("(image", "(job->ref").replace("image)", "job->ref)")
        src = body + "struct JobB { void *ref; };\nvoid H(JobB *job) { RawSlot<30>(job->ref, 1u); }\n"
        found = [f for f in run(src) if "RawSlot<30>" in f["where"]]
        self.assertEqual([f["verdict"] for f in found], ["unknown"], found)
        same = [f for f in run(body + "void H(JobA *job) { RawSlot<30>(job->ref, 1u); }\n")
                if "RawSlot<30>" in f["where"]]
        # judged with JobA's other calls, but a member may hold different objects: a probable mismatch only
        self.assertEqual([f["verdict"] for f in same], ["unknown"], same)
        self.assertIn("probable mismatch", same[0]["detail"])

    def test_a_name_reused_for_another_object_is_not_pooled(self):
        # H's `image` is another kind of object: its calls fit the decoy, not the class G's calls identify, so the
        # file-wide pool doesn't agree and nothing in H is judged by G's class
        src = self.FIXED + ("void H(void *image) { RawSlot<3>(image, 1u, 2u, 3u, 4u); RawSlot<21>(image, 1u, 2u, 3u, "
                            "4u, 5u, 6u); RawSlot<30>(image, 1u); }\n")
        line = self.FIXED.count("\n") + 2  # run() puts an #include first
        found = [f for f in run(src) if f["where"].startswith(f"src/x/cUser.cpp:{line} ")]
        self.assertEqual(len(found), 3, found)
        self.assertEqual({f["verdict"] for f in found}, {"unknown"}, found)


class WhatTheCallPasses(unittest.TestCase):
    def one(self, args, ret, extra=""):
        classes, rets = build([("cImpl", ["cIThing"], {4: ret})])
        found = run(extra + f"void F(cIThing *t) {{ RawSlot<4>(t{args}); }}\n", classes, rets)
        self.assertEqual(len(found), 1, found)
        return found[0]["verdict"]

    def test_64_bit_and_double_take_two_dwords(self):
        self.assertEqual(self.one(", static_cast<int64_t>(1)", 8), "ok")
        self.assertEqual(self.one(", 1.0", 8), "ok")
        self.assertEqual(self.one(", 1.0f", 4), "ok")
        self.assertEqual(self.one(", 1.0f", 8), "MISMATCH")

    def test_variables_by_their_declarations(self):
        self.assertEqual(self.one(", d", 8, "double d;\n"), "ok")
        self.assertEqual(self.one(", f, f", 8, "float f;\n"), "ok")
        self.assertEqual(self.one(", c", 4, "uint8_t c;\n"), "ok")

    def test_structs_by_value_by_their_size(self):
        self.assertEqual(self.one(", rect", 16, "struct Rect { int32_t l, t, r, b; };\nstatic_assert(sizeof(Rect) == 16);\n"
                                  "Rect rect;\n"), "ok")
        self.assertEqual(self.one(", fmt", 8, "struct Fmt {\n    uint32_t a;\n    uint16_t b;\n};\nFmt fmt;\n"), "ok")

    def test_unknown_size_gives_no_mismatch_above_the_dword_count(self):
        self.assertEqual(self.one(", thing", 12, "Mystery thing;\n"), "unknown")
        self.assertEqual(self.one(", thing", 4, "Mystery thing;\n"), "ok")
        self.assertEqual(self.one(", thing, 1u", 4, "Mystery thing;\n"), "MISMATCH")  # passes 8 at least

    def test_class_result_adds_a_hidden_pointer(self):
        self.assertEqual(self.one("", 4), "MISMATCH")  # RawSlot<4>(t) returns uint32_t: passes 0
        classes, rets = build([("cImpl", ["cIThing"], {4: 4})])
        src = ("struct Big { uint32_t a, b, c; };\nstatic_assert(sizeof(Big) == 12);\n"
               "void F(cIThing *t) { RawSlot<4, Big>(t); }\n")
        self.assertEqual([f["verdict"] for f in run(src, classes, rets)], ["ok"])

    def test_virtual_call_from_its_fn_type(self):
        classes, rets = build([("cImpl", ["cIThing"], {4: 8, 5: 12})])
        src = ("void F(cIThing *t) {\n"
               "    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t), 4>(t, 1u, 2u);\n"
               "    VirtualCall<uint32_t(__stdcall *)(void *, uint32_t, uint32_t), 5>(t, 1u, 2u);\n"
               "    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 5>(t, 1u);\n"
               "}\n")
        self.assertEqual([f["verdict"] for f in run(src, classes, rets)], ["ok", "ok", "MISMATCH"])

    def test_a_local_alias_is_the_nearest_definition(self):
        # two functions define `Fn` differently; each call takes its own function's definition
        classes, rets = build([("cImpl", ["cIThing"], {4: 8})])
        src = ("void F(cIThing *t) {\n"
               "    using Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);\n"
               "    VirtualCall<Fn, 4>(t, 1u, 2u);\n"
               "}\n"
               "void G(cIThing *t) {\n"
               "    using Fn = uint32_t(__thiscall *)(void *, uint32_t);\n"
               "    VirtualCall<Fn, 4>(t, 1u);\n"
               "}\n")
        self.assertEqual([f["verdict"] for f in run(src, classes, rets)], ["ok", "MISMATCH"])

    def test_virtual_slot(self):
        classes, rets = build([("cImpl", ["cIThing"], {4: 8})])
        src = ("using Fn = uint32_t(__thiscall *)(void *, uint32_t);\n"
               "void F(cIThing *t) { VirtualSlot<Fn>(t, 4)(t, 1u); }\n")
        self.assertEqual([f["verdict"] for f in run(src, classes, rets)], ["MISMATCH"])

    def test_comments_are_not_calls(self):
        self.assertEqual(run("// RawSlot<21>(image, 1u)\nvoid F() {}\n"), [])


class Guard(unittest.TestCase):
    def test_unknown_slot_calls_are_listed_for_the_lane(self):
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "src" / "game").mkdir(parents=True)
            (root / "src" / "game" / "vtable.h").write_text(HEADER, encoding="utf-8")
            base = '#include "game/vtable.h"\nvoid G(void *p) { RawSlot<21>(p, 1u); }\n'
            text = base + "void H(void *q) { RawSlot<9>(q); }\n"
            errors, warnings = [], []
            orig = guard.git
            guard.git = lambda *a: base  # G's call was there at the base, H's is the branch's
            try:
                guard.check_arity("base", {"src/x/a.cpp": text}, errors, warnings,
                                  rets=lambda rva: RETS.get(rva, set()), root=root, classes=CLASSES)
            finally:
                guard.git = orig
            self.assertEqual(errors, [])
            slot = [w for w in warnings if "slot calls on lines this branch adds or changes not checked" in w]
            self.assertEqual(len(slot), 1, warnings)
            self.assertIn("src/x/a.cpp:3 RawSlot<9> on q", slot[0])
            self.assertNotIn("RawSlot<21> on p", slot[0])
            self.assertIn("1 more unchanged", slot[0])

    def test_a_mismatch_is_an_error(self):
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "src" / "game").mkdir(parents=True)
            (root / "src" / "game" / "vtable.h").write_text(HEADER, encoding="utf-8")
            text = '#include "game/vtable.h"\nvoid G(cIBuf *p) { RawSlot<21>(p, 1u); }\n'
            errors, warnings = [], []
            orig = guard.git
            guard.git = lambda *a: ""  # the base has no such file
            try:
                guard.check_arity("base", {"src/x/a.cpp": text}, errors, warnings,
                                  rets=lambda rva: RETS.get(rva, set()), root=root, classes=CLASSES)
            finally:
                guard.git = orig
            self.assertEqual(len(errors), 1, (errors, warnings))
            self.assertIn("slot call", errors[0])


if __name__ == "__main__":
    unittest.main()
