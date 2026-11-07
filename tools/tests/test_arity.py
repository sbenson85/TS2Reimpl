#!/usr/bin/env python3
"""Tests for tools/arity.py (declared stack-argument bytes against the original's `ret N`) and its use in
tools/guard.py. Synthetic sources and a fake table of rets: no game bytes.

Run: tools/py -I tools/tests/test_arity.py
"""
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import arity  # noqa: E402
import guard  # noqa: E402


def run(files, rets, report=None, funcs=None):
    """findings for {path: text} against {rva: set of ret immediates}."""
    return arity.check(list(files.items()), lambda rva: rets.get(rva, set()), funcs, report)


def verdicts(found, kind=None):
    return [(f["kind"], f["verdict"], f["rva"]) for f in found if kind is None or f["kind"] == kind]


class OriginalFunctionSites(unittest.TestCase):
    def one(self, fn_type, ret, rva=0x1000):
        src = {"src/a.cpp": f"void F() {{ OriginalFunction<{fn_type}, 0x{rva:x}>()(1, 2); }}\n"}
        found = run(src, {rva: set(ret)})
        self.assertEqual(len(found), 1, found)
        return found[0]["verdict"]

    def test_stdcall(self):
        self.assertEqual(self.one("int(__stdcall *)(int, int)", {8}), "ok")
        self.assertEqual(self.one("int(__stdcall *)(int, int)", {12}), "MISMATCH")

    def test_thiscall_this_in_ecx(self):
        self.assertEqual(self.one("bool(__thiscall *)(void *, uint32_t, const char *)", {8}), "ok")
        self.assertEqual(self.one("bool(__thiscall *)(void *, uint32_t, const char *)", {12}), "MISMATCH")

    def test_fastcall_two_register_arguments(self):
        # ecx, edx take the first two dword arguments; the double goes on the stack
        self.assertEqual(self.one("void(__fastcall *)(void *, void *, int, double)", {12}), "ok")
        self.assertEqual(self.one("void(__fastcall *)(void *, void *)", {0}), "ok")

    def test_cdecl_pops_nothing(self):
        self.assertEqual(self.one("int(__cdecl *)(int, int)", {0}), "ok")
        self.assertEqual(self.one("int(__cdecl *)(int, int)", {8}), "MISMATCH")

    def test_small_types_take_a_dword_and_64_bit_two(self):
        self.assertEqual(self.one("void(__stdcall *)(bool, uint8_t, int16_t, int64_t, float)", {24}), "ok")

    def test_rets_that_disagree_or_none_are_unknown(self):
        self.assertEqual(self.one("void(__stdcall *)(int)", {4, 8}), "unknown")
        self.assertEqual(self.one("void(__stdcall *)(int)", set()), "unknown")

    def test_using_alias_in_the_same_file(self):
        src = {"src/a.cpp": "using QueryFn = bool(__thiscall *)(void *, uint32_t, uint32_t);\n"
                            "bool F() { return OriginalFunction<QueryFn, 0x2000>()(nullptr, 1, 2); }\n"}
        self.assertEqual(verdicts(run(src, {0x2000: {8}})), [("OriginalFunction", "ok", "0x2000")])
        self.assertEqual(verdicts(run(src, {0x2000: {4}})), [("OriginalFunction", "MISMATCH", "0x2000")])

    def test_typedef(self):
        src = {"src/a.cpp": "typedef void(__stdcall *SetFn)(int a, int b, int c);\n"
                            "void F() { OriginalFunction<SetFn, 0x2100>()(1, 2, 3); }\n"}
        self.assertEqual(verdicts(run(src, {0x2100: {12}})), [("OriginalFunction", "ok", "0x2100")])

    def test_same_alias_name_defined_differently_in_another_file(self):
        # each file's own alias wins in that file
        src = {"src/a.cpp": "using Fn = void(__stdcall *)(int);\nvoid F() { OriginalFunction<Fn, 0x2200>()(1); }\n",
               "src/b.cpp": "using Fn = void(__stdcall *)(int, int);\nvoid G() { OriginalFunction<Fn, 0x2300>()(1, 2); }\n"}
        found = run(src, {0x2200: {4}, 0x2300: {8}})
        self.assertEqual(sorted(verdicts(found)), [("OriginalFunction", "ok", "0x2200"), ("OriginalFunction", "ok", "0x2300")])

    def test_report_limits_the_files(self):
        src = {"src/a.cpp": "void F() { OriginalFunction<void(__stdcall *)(int), 0x2400>()(1); }\n",
               "src/b.cpp": "void G() { OriginalFunction<void(__stdcall *)(int), 0x2400>()(1); }\n"}
        self.assertEqual(len(run(src, {0x2400: {8}}, report=["src/b.cpp"])), 1)


JI_HEADER = """
// The interface (cITSCASSystem): one line per slot, the original named in the comment.
class cITSCASSystem : public cIGZUnknown {
public:
    virtual bool LoadResource(const SkinKey *key, uint32_t iid, void **out) = 0; // 24 (thunk 0x50a970 -> 0x502dc0)
    virtual int32_t QueryFragments(void *bin, CASItemVector *out, uint32_t kind, int32_t a4, const SkinKey *keyA,
                                   const SkinKey *keyB, uint32_t a7, uint32_t a8, uint32_t a9,
                                   uint32_t a10{extra}) = 0;                    // 23 (0x5039b0)
    virtual uint32_t GetEditedSimId() = 0;                                     // 3 (folded 0x145770): +0x28
};
"""
JI_SOURCE = """
int32_t cTSCASSystem::QueryFragments(void *bin, CASItemVector *out, uint32_t kind, int32_t a4, const SkinKey *keyA,
                                     const SkinKey *keyB, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10{extra})
{{
    return OriginalFunction<int32_t(__thiscall *)(cITSCASSystem *, void *, CASItemVector *, uint32_t, int32_t,
        const SkinKey *, const SkinKey *, uint32_t, uint32_t, uint32_t, uint32_t{extra_type}), 0x5039b0>()(
        Face(), bin, out, kind, a4, keyA, keyB, a7, a8, a9, a10{extra_arg});
}}
"""
JI_RETS = {0x5039b0: {0x2c}, 0x50a970: {12}, 0x145770: {0}}


class SeededMismatch(unittest.TestCase):
    """Lane JI's cTSCASSystem slot 23: ten dwords declared, the original pops eleven."""

    def files(self, eleven):
        extra = ", uint32_t unused" if eleven else ""
        return {"src/cas/cTSCASSystem.h": JI_HEADER.replace("{extra}", extra),
                "src/cas/cTSCASSystem.cpp": JI_SOURCE.format(extra=extra, extra_type=", uint32_t" if eleven else "",
                                                             extra_arg=", unused" if eleven else "")}

    def test_ten_dwords_is_caught_twice(self):
        found = run(self.files(False), JI_RETS)
        bad = sorted((f["kind"], f["rva"]) for f in found if f["verdict"] == "MISMATCH")
        self.assertEqual(bad, [("OriginalFunction", "0x5039b0"), ("declaration", "0x5039b0")])
        self.assertIn("11 dwords", next(f for f in found if f["verdict"] == "MISMATCH")["detail"])

    def test_eleven_dwords_passes(self):
        found = run(self.files(True), JI_RETS)
        self.assertEqual([f for f in found if f["verdict"] != "ok"], [])
        self.assertEqual(len(found), 4)  # the forwarder and three slot declarations (folded and thunk included)


class Declarations(unittest.TestCase):
    def test_conventions_of_members_and_free_functions(self):
        src = {"src/x.h": """
namespace ns {
struct cThing {
    cThing(int a, int b);                                   // 0x100 (a constructor: never the boundary)
    void Member(int a, int b);                              // 0x200
    static void __stdcall StaticStd(int a);                 // 0x300
    static int StaticDefault(int a, int b);                 // 0x400
    void Variadic(const char *format, ...);                 // 0x500
    std::pair<int *, bool> Insert(const int &value);        // 0x600 (hidden result pointer)
    bool Inline(int a) { return a != 0; }                   // 0x700
    void __stdcall MemberStd(int a);                        // 0x800 (this on the stack)
};
struct cThingAdapters {
    static bool __fastcall Load(cThing *self, void *edx, const char *name, uint32_t iid, void **out); // 0x900
};
int __stdcall FreeStd(int a, double b);                     // 0xa00
void FreeDefault(int a);                                    // 0xb00
}
"""}
        rets = {0x100: {0}, 0x200: {8}, 0x300: {4}, 0x400: {0}, 0x500: {0}, 0x600: {8}, 0x700: {4}, 0x800: {8},
                0x900: {12}, 0xa00: {12}, 0xb00: {0}}
        found = run(src, rets)
        self.assertEqual([f for f in found if f["verdict"] != "ok"], [])
        self.assertEqual(sorted(int(f["rva"], 16) for f in found), sorted(set(rets) - {0x100}))
        # every one of them, made off by a dword, is a mismatch
        for rva in sorted(set(rets) - {0x100}):
            off = dict(rets)
            off[rva] = {next(iter(rets[rva])) + 4}
            got = {f["rva"]: f["verdict"] for f in run(src, off)}
            self.assertEqual(got[f"0x{rva:x}"], "MISMATCH", hex(rva))

    def test_comment_forms(self):
        src = {"src/x.h": """
struct S {
    virtual void A(int a) = 0;   // 7 (0x1100)
    virtual void B(int a) = 0;   // slot 8 (0x1200): text
    virtual void C(int a) = 0;   // 9 (folded 0x1300)
    virtual void D(int a) = 0;   // 0x1400; thunks 0x1405 (+4)
    virtual void E(int a) = 0;   // +0x28 is the field it reads (not an RVA)
    virtual void F(int a) = 0;   // returns the field at 0x28 (not at the start: not an RVA)
    void (*mCallback)(int);      // 0x1500 (a function pointer field)
};
"""}
        found = run(src, {r: {4} for r in (0x1100, 0x1200, 0x1300, 0x1400)})
        self.assertEqual(sorted(f["rva"] for f in found), ["0x1100", "0x1200", "0x1300", "0x1400"])

    def test_function_start_filter(self):
        src = {"src/x.h": "struct S {\n    void A(int a); // 0x28 bytes\n};\n"}
        self.assertEqual(run(src, {0x28: {0}}, funcs={0x1000}), [])
        self.assertEqual(verdicts(run(src, {0x28: {0}})), [("declaration", "MISMATCH", "0x28")])

    def test_wrapper_of_an_original_is_left_to_its_call_site(self):
        # a non-virtual member that calls the cdecl original it names: not the boundary; its call is
        src = {"src/x.h": "struct G {\n    bool Check(int a, int b); // 0x1600 (original call)\n};\n",
               "src/x.cpp": "bool G::Check(int a, int b) { return OriginalFunction<bool(__cdecl *)(int, int), 0x1600>()(a, b); }\n"}
        self.assertEqual(verdicts(run(src, {0x1600: {0}})), [("OriginalFunction", "ok", "0x1600")])
        # a virtual forwarder is the boundary itself (the game calls our vtable): both are checked
        src["src/x.h"] = "struct G {\n    virtual bool Check(int a, int b); // 0x1600\n};\n"
        src["src/x.cpp"] = src["src/x.cpp"].replace("__cdecl", "__thiscall").replace("(int, int)", "(void *, int, int)")
        self.assertEqual(sorted(verdicts(run(src, {0x1600: {8}}))), [("OriginalFunction", "ok", "0x1600"),
                                                                     ("declaration", "ok", "0x1600")])

    def test_two_declarations_of_one_original(self):
        # the body behind a deleting destructor: the one that agrees is the boundary
        src = {"src/x.h": "struct P {\n    void Destruct() noexcept; // 0x1700 body\n"
                          "    P *DeletingDestructor(uint8_t flags) noexcept; // 0x1700\n};\n"}
        found = run(src, {0x1700: {4}})
        self.assertEqual(sorted(f["verdict"] for f in found), ["ok", "unknown"])
        # neither agrees: both are mismatches
        found = run(src, {0x1700: {8}})
        self.assertEqual([f["verdict"] for f in found], ["MISMATCH", "MISMATCH"])

    def test_class_template_members_are_unknown(self):
        src = {"src/x.h": "template <class T> struct A {\n    T *allocate(size_t n); // 0x1800\n};\n"}
        self.assertEqual(verdicts(run(src, {0x1800: {0}})), [("declaration", "unknown", "0x1800")])

    def test_comments_and_strings_do_not_confuse_the_braces(self):
        src = {"src/x.h": """
#define OPEN {
struct S {
    const char *Text() { return "}{;"; }      /* } */
    // a { in a comment
    char Brace() { return '{'; }
    void A(int a, int b);                      // 0x1900
};
"""}
        self.assertEqual(verdicts(run(src, {0x1900: {8}})), [("declaration", "ok", "0x1900")])

    def test_enum_parameters_and_unknown_types(self):
        src = {"src/x.h": "enum class Mode : uint8_t { A, B };\nstruct Opaque;\n"
                          "struct S {\n    void A(Mode m, int b); // 0x1a00\n    void B(Opaque o); // 0x1b00\n};\n"}
        got = {f["rva"]: f["verdict"] for f in run(src, {0x1a00: {8}, 0x1b00: {4}})}
        self.assertEqual(got, {"0x1a00": "ok", "0x1b00": "unknown"})


class HookTables(unittest.TestCase):
    HEADER = """
namespace nX {
struct cWidget {
    bool Draw(int x, int y);
    int Size() const;
    void Resize(int w);
    void Resize(int w, int h);
};
struct cWidgetAdapters {
    static cWidget *__fastcall Construct(void *memory, void *edx, int kind);
};
void FreeHelper(void *p, int n);
}
"""

    def table(self, entries, extra=""):
        return {"src/x/cWidget.h": self.HEADER,
                "src/hook/tables/cWidget.cpp": "#include \"x/cWidget.h\"\nnamespace {\nusing A = nX::cWidgetAdapters;\n"
                                               "#define W(name) MemberAddress(&nX::cWidget::name)\n" + extra +
                                               "const HookSpec kHooks[] = {\n" + "".join(e + "\n" for e in entries) +
                                               "};\n}\n"}

    def test_forms(self):
        files = self.table([
            '    {"Draw", 0x2000, 32, 0x1, MemberAddress(&nX::cWidget::Draw)},',
            '    {"Size", 0x2100, 32, 0x1, MemberAddress<&nX::cWidget::Size>()},',
            '    {"Construct", 0x2200, 32, 0x1, reinterpret_cast<void *>(&A::Construct)},',
            '    {"Free", 0x2300, 32, 0x1, reinterpret_cast<void *>(&nX::FreeHelper)},',
            '    {"DrawMacro", 0x2400, 32, 0x1, W(Draw)},',
        ])
        rets = {0x2000: {8}, 0x2100: {0}, 0x2200: {4}, 0x2300: {0}, 0x2400: {8}}
        found = run(files, rets)
        self.assertEqual(sorted(verdicts(found)), sorted(("hook", "ok", f"0x{r:x}") for r in rets))
        rets[0x2200] = {0}
        self.assertIn(("hook", "MISMATCH", "0x2200"), verdicts(run(files, rets)))

    def test_overloads(self):
        files = self.table(['    {"Resize", 0x2500, 32, 0x1, MemberAddress(&nX::cWidget::Resize)},'])
        self.assertEqual(verdicts(run(files, {0x2500: {4}})), [("hook", "unknown", "0x2500")])
        self.assertEqual(verdicts(run(files, {0x2500: {12}})), [("hook", "MISMATCH", "0x2500")])

    def test_unresolved_is_unknown(self):
        files = self.table(['    {"Base", 0x2600, 32, 0x1, MemberAddress(&nX::cWidget::FromBase)},',
                            '    {"Cast", 0x2700, 32, 0x1, MemberAddress(static_cast<void (nX::cWidget::*)(int)>(&nX::cWidget::Resize))},'])
        self.assertEqual(sorted(verdicts(run(files, {0x2600: {4}, 0x2700: {8}}))),
                         [("hook", "unknown", "0x2600"), ("hook", "unknown", "0x2700")])

    def test_entry_in_a_comment_is_skipped(self):
        files = self.table([], extra='// {"Draw", 0x2800, 32, 0x1, MemberAddress(&nX::cWidget::Draw)},\n')
        self.assertEqual(run(files, {0x2800: {0}}), [])


UNKNOWN_H = """
class cIGZUnknown {
public:
    virtual bool QueryInterface(uint32_t riid, void **out) = 0;
    virtual uint32_t AddRef() = 0;
    virtual uint32_t Release() = 0;
};
"""


def impl(name, iface, entries, offset=8):
    return {"name": name, "bases": [iface, "cIGZUnknown"], "base_offsets": [offset, offset],
            "vftables": [{"rva": "0x9000", "offset": 0, "entries": ["0x7000"]},
                         {"rva": "0x9100", "offset": offset, "entries": [hex(e) for e in entries]}]}


class InterfaceSlots(unittest.TestCase):
    """Lane KF: an interface slot declared without the argument its implementations pop (`ret 4`)."""

    def run_iface(self, body, classes, rets, extra=""):
        src = {"src/gz/unknown.h": UNKNOWN_H, "src/x/face.h": "class cIFace : public cIGZUnknown {\npublic:\n" + body
               + "};\n" + extra}
        found = arity.check(list(src.items()), lambda rva: rets.get(rva, set()), None, None, classes)
        return [(f["verdict"], f["where"].split(" ")[-1], f["rva"], f["detail"]) for f in found
                if f["kind"] == "interface slot" and "cIGZUnknown::" not in f["where"]]  # its slots: fake RVAs, no rets

    def test_a_missing_argument_is_a_mismatch(self):
        classes = [impl("cImpl", "cIFace", [0x5001, 0x5002, 0x5003, 0x5000])]
        bad = self.run_iface("    virtual const char *GetHelp() = 0;\n", classes, {0x5000: {4}})
        self.assertEqual([b[:3] for b in bad], [("MISMATCH", "cIFace::GetHelp", "0x5000")])
        self.assertIn("slot 3", bad[0][3])
        ok = self.run_iface("    virtual const char *GetHelp(uint32_t unused) = 0;\n", classes, {0x5000: {4}})
        self.assertEqual([b[:3] for b in ok], [("ok", "cIFace::GetHelp", "0x5000")])

    def test_slots_follow_the_base_a_virtual_destructor_and_skip_overrides(self):
        body = ("    virtual ~cIFace() {}\n    uint32_t AddRef() override;\n    virtual void A(int a) = 0;\n"
                "    virtual void B() = 0;\n")
        classes = [impl("cImpl", "cIFace", [1, 2, 3, 0x5003, 0x5004, 0x5005])]
        got = {b[1]: b for b in self.run_iface(body, classes, {0x5004: {4}, 0x5005: {4}})}
        self.assertEqual(got["cIFace::A"][:3], ("ok", "cIFace::A", "0x5004"))  # slot 4: the destructor is slot 3
        self.assertEqual(got["cIFace::B"][:3], ("MISMATCH", "cIFace::B", "0x5005"))

    def test_a_derived_interface_extends_its_base(self):
        extra = "class cIMore : public cIFace {\npublic:\n    virtual void C(int a, int b) = 0;\n};\n"
        classes = [impl("cImpl", "cIMore", [1, 2, 3, 4, 0x5004])]
        got = self.run_iface("    virtual void A() = 0;\n", classes, {0x5004: {8}}, extra)
        self.assertEqual([g[:3] for g in got], [("ok", "cIMore::C", "0x5004")])

    def test_overloads_rva_comments_and_disagreement(self):
        classes = [impl("cImpl", "cIFace", [1, 2, 3, 0x5003, 0x5004])]
        over = self.run_iface("    virtual void A() = 0;\n    virtual void A(int a) = 0;\n", classes, {0x5003: {4}})
        self.assertEqual(over, [])  # MSVC's order for overloads isn't modelled: no finding
        named = self.run_iface("    virtual void A() = 0; // 3 (0x5003)\n", classes, {0x5003: {4}})
        self.assertEqual(named, [])  # the declaration check covers it
        two = classes + [impl("cOther", "cIFace", [1, 2, 3, 0x6003])]
        got = self.run_iface("    virtual void A() = 0;\n", two, {0x5003: {4}, 0x6003: {0}})
        self.assertEqual(got[0][0], "unknown")
        self.assertIn("disagree", got[0][3])

    def test_without_classes_nothing_is_checked(self):
        src = {"src/gz/unknown.h": UNKNOWN_H, "src/x/face.h": "class cIFace : public cIGZUnknown {\n"
               "    virtual void A() = 0;\n};\n"}
        self.assertEqual(arity.check(list(src.items()), lambda rva: {4}, None, None), [])


class GuardArity(unittest.TestCase):
    """guard.check_arity: a new mismatch in a changed file is an error, one already at the merge base a warning."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        (root / "src").mkdir()
        (root / "src/other.cpp").write_text("void H() { OriginalFunction<void(__stdcall *)(int), 0x3000>()(1); }\n")
        self.root = root
        self.saved = guard.git, guard.arity.function_starts
        guard.arity.function_starts = lambda: None

    def tearDown(self):
        guard.git, guard.arity.function_starts = self.saved
        self.tmp.cleanup()

    def check(self, base_text, branch_text):
        guard.git = lambda *a: base_text
        errors, warnings = [], []
        guard.check_arity("base", {"src/a.cpp": branch_text}, errors, warnings,
                          rets=lambda rva: {0x3000: {4}, 0x3100: {8}}.get(rva, set()), root=self.root, classes=[])
        return errors, warnings

    def test_new_mismatch_is_an_error(self):
        errors, _ = self.check("", "void F() { OriginalFunction<void(__stdcall *)(int), 0x3100>()(1); }\n")
        self.assertEqual(len(errors), 1)
        self.assertIn("0x3100", errors[0])

    def test_mismatch_already_on_the_base_is_a_warning(self):
        text = "void F() { OriginalFunction<void(__stdcall *)(int), 0x3100>()(1); }\n"
        errors, warnings = self.check("// moved down a line\n" + text, text)
        self.assertEqual(errors, [])
        self.assertTrue(any("already on the base" in w for w in warnings))

    def test_agreeing_and_unchanged_files_pass(self):
        errors, _ = self.check("", "void F() { OriginalFunction<void(__stdcall *)(int, int), 0x3100>()(1, 2); }\n")
        self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main()
