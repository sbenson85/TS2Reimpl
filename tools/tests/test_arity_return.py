#!/usr/bin/env python3
"""Tests for tools/arity.py's "return" check (a void declaration whose original leaves a value in eax) and its
warning in tools/guard.py. Synthetic instruction sequences written for these tests (no game bytes); two known RVAs
of the unpacked exe when it is present (local/).

Run: tools/py -I tools/tests/test_arity_return.py
"""
import os
import sys
import tempfile
import unittest
from collections import namedtuple
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import arity  # noqa: E402
import guard  # noqa: E402

Ins = namedtuple("Ins", "mnemonic op_str size")


def body(*lines):
    """{index: Ins} for "mnemonic operands" lines at addresses 0, 1, 2, ...; jump targets are line indexes."""
    out = {}
    for k, line in enumerate(lines):
        m, _, ops = line.partition(" ")
        out[k] = Ins(m, ops.strip(), 1)
    return out


def succ(pc, i):
    m = i.mnemonic
    if m in ("ret", "int3"):
        return []
    if m.startswith("j"):
        try:
            t = int(i.op_str, 0)
        except ValueError:
            return []
        return [t] if m == "jmp" else [t, pc + 1]
    return [pc + 1]


def verdict(*lines):
    return arity.eax_verdict(arity.eax_at_rets(body(*lines), 0, succ))[0]


class EaxAtRets(unittest.TestCase):
    def test_out_pointer_getter_is_strong(self):
        # lane OX's GetVector3 shape: copy fields through the out pointer and leave it in eax
        self.assertEqual(verdict("mov eax, dword ptr [esp + 4]", "mov edx, dword ptr [ecx + 8]",
                                 "mov dword ptr [eax], edx", "ret 4"), "strong")

    def test_setter_through_eax_is_weak(self):
        self.assertEqual(verdict("mov eax, dword ptr [esp + 4]", "mov dword ptr [ecx + 8], eax", "ret 4"), "weak")
        self.assertEqual(verdict("xor eax, eax", "mov dword ptr [ecx], eax", "mov dword ptr [ecx + 4], eax", "ret"),
                         "weak")

    def test_this_directly_and_saved(self):
        self.assertEqual(verdict("mov eax, ecx", "ret"), "strong")
        self.assertEqual(verdict("push esi", "mov esi, ecx", "call 0x100", "mov eax, esi", "pop esi", "ret"),
                         "strong")

    def test_last_write_a_call_is_weak(self):
        self.assertEqual(verdict("push esi", "call 0x100", "pop esi", "ret"), "weak")
        self.assertEqual(verdict("call 0x100", "mov esi, eax", "mov dword ptr [ecx], 0", "mov eax, esi", "ret"),
                         "weak")

    def test_call_result_reworked_is_strong(self):
        self.assertEqual(verdict("call 0x100", "mov eax, dword ptr [eax + 4]", "ret"), "strong")

    def test_bool_through_al(self):
        self.assertEqual(verdict("mov al, 1", "ret"), "strong")
        self.assertEqual(verdict("cmp dword ptr [ecx], 0", "sete al", "ret"), "strong")
        self.assertEqual(verdict("mov al, byte ptr [ecx + 4]", "mov byte ptr [edx], al", "ret"), "weak")

    def test_constants_on_every_path(self):
        self.assertEqual(verdict("xor eax, eax", "ret"), "strong")
        self.assertEqual(verdict("cmp dword ptr [ecx], 0", "je 4", "mov eax, 1", "ret", "xor eax, eax", "ret"),
                         "strong")

    def test_conditional_release_differs_between_paths(self):
        # lane OY's setters: eax is a null-tested member on one path, a Release's result on the other
        self.assertEqual(verdict("mov eax, dword ptr [ecx + 4]", "test eax, eax", "je 6", "mov ecx, dword ptr [eax]",
                                 "push eax", "call dword ptr [ecx + 8]", "ret"), "weak")

    def test_a_path_that_leaves_eax_alone_is_weak(self):
        self.assertEqual(verdict("cmp dword ptr [ecx], 0", "je 3", "mov eax, dword ptr [esp + 4]", "ret 4"), "weak")

    def test_eax_never_written_is_none(self):
        self.assertEqual(verdict("mov dword ptr [ecx], 0", "ret"), "none")
        self.assertEqual(verdict("inc eax", "ret"), "none")  # arithmetic on the caller's value

    def test_input_pointer_only_read_through_is_weak(self):
        # cGZDrawContext::Translate's shape: an argument pointer read through, nothing stored through it
        self.assertEqual(verdict("mov eax, dword ptr [esp + 4]", "movd xmm0, dword ptr [eax]",
                                 "movss dword ptr [ecx + 0x1c], xmm0", "ret 4"), "weak")
        self.assertEqual(verdict("mov eax, dword ptr [esp + 4]", "fld dword ptr [eax]", "fstp dword ptr [ecx]",
                                 "ret 4"), "weak")
        self.assertEqual(verdict("mov eax, dword ptr [esp + 4]", "fld dword ptr [ecx]", "fstp dword ptr [eax]",
                                 "ret 4"), "strong")  # a store through it: an out pointer

    def test_consumed_into_another_register_is_weak(self):
        self.assertEqual(verdict("mov eax, dword ptr [ecx + 0x18]", "or edx, eax", "mov dword ptr [ecx + 0x18], edx",
                                 "ret 8"), "weak")
        self.assertEqual(verdict("mov al, byte ptr [edi + 8]", "or al, 1", "movzx eax, al", "cmovne ecx, eax", "mov byte ptr [edi + 8], cl", "ret 4"), "weak")

    def test_switch_index_and_loop_mask_are_weak(self):
        self.assertEqual(verdict("mov eax, dword ptr [esp + 4]", "sub eax, 0", "je 5", "mov dword ptr [ecx], 1",
                                 "ret 4", "mov dword ptr [ecx], 2", "ret 4"), "weak")
        self.assertEqual(verdict("mov eax, 1", "test eax, esi", "rol eax, 1", "sub edx, 1", "jne 1", "ret"), "weak")
        self.assertEqual(verdict("mov eax, dword ptr [ecx]", "shr eax, 3", "and eax, 1", "ret"), "strong")

    def test_pointer_stored_through_that_is_not_an_argument_is_weak(self):
        # std::map operator[]'s node, a member's address: temporaries; an argument stored through is an out pointer
        self.assertEqual(verdict("call 0x100", "mov eax, dword ptr [eax]", "mov dword ptr [eax + 0xc], esi", "ret"),
                         "weak")
        self.assertEqual(verdict("lea eax, [ecx + 0x11]", "and byte ptr [eax], 0xfb", "ret"), "weak")

    def test_loops_reach_a_fixed_point(self):
        self.assertEqual(verdict("mov eax, dword ptr [ecx]", "dec edx", "jne 0", "lea eax, [ecx + 8]", "ret"),
                         "strong")


class CallerReads(unittest.TestCase):
    def after(self, *lines):
        return arity.eax_read_after(body(*lines), 0, succ)

    def test_read_dropped_returned(self):
        self.assertEqual(self.after("mov ecx, eax", "ret"), "read")
        self.assertEqual(self.after("test al, al", "je 3", "ret", "ret"), "read")
        self.assertEqual(self.after("push dword ptr [eax + 4]", "ret"), "read")
        self.assertEqual(self.after("add esp, 8", "xor eax, eax", "ret"), "dropped")
        self.assertEqual(self.after("call 0x100", "ret"), "dropped")
        self.assertEqual(self.after("add esp, 8", "ret"), "returned")  # the caller's caller may read it
        self.assertEqual(self.after("cmp ecx, 0", "je 3", "mov eax, 1", "ret"), "returned")
        self.assertEqual(self.after("movzx eax, al", "ret"), "read")
        self.assertEqual(self.after("nop word ptr [eax + eax]", "cvttss2si eax, dword ptr [esi]", "ret"), "dropped")


class cDataListItemLike:
    HEADER = ("class cItem {\npublic:\n"
              "    {ret} GetVector3(float *out); // 21 (0x4000){note}\n"
              "    void SetVector3(const float *value); // 20 (0x4100)\n"
              "};\n")


def run(header, eax_map, extra=None):
    files = {"src/sg/item.h": header}
    files.update(extra or {})
    found = arity.check(list(files.items()), lambda rva: {4}, None, None,
                        eax=lambda rva: eax_map.get(rva, ("none", "")))
    return [(f["verdict"], f["where"], f["rva"]) for f in found if f["kind"] == "return"]


class ReturnCheck(unittest.TestCase):
    EAX = {0x4000: ("strong", "eax at the ret: a stack argument"), 0x4100: ("weak", "a call's result")}

    def header(self, ret="void", note=""):
        return cDataListItemLike.HEADER.replace("{ret}", ret).replace("{note}", note)

    def test_void_getter_is_reported(self):
        got = run(self.header(), self.EAX)
        self.assertIn(("strong", "src/sg/item.h:3 cItem::GetVector3", "0x4000"), got)
        self.assertIn(("weak", "src/sg/item.h:4 cItem::SetVector3", "0x4100"), got)

    def test_declared_return_is_not_checked(self):
        got = run(self.header("float *"), self.EAX)
        self.assertEqual([g for g in got if g[2] == "0x4000"], [])

    def test_marker_silences(self):
        got = run(self.header(note=" eax-ignored: no caller reads it"), self.EAX)
        self.assertIn(("silenced", "src/sg/item.h:3 cItem::GetVector3", "0x4000"), got)

    def test_hook_entry_to_a_void_member(self):
        header = "class cItem {\npublic:\n    void Get(float *out);\n};\n"
        table = '#include "x"\nconst Hook kHooks[] = {\n    {"Get", 0x4000, 32, 0x1, MemberAddress(&cItem::Get)},\n};\n'
        got = run(header, self.EAX, {"src/hook/tables/cItem.cpp": table})
        self.assertEqual(got, [("strong", "src/hook/tables/cItem.cpp:3 Get", "0x4000")])
        silenced = table.replace("},\n};", "}, // eax-ignored: tested\n};")
        got = run(header, self.EAX, {"src/hook/tables/cItem.cpp": silenced})
        self.assertEqual([g[0] for g in got], ["silenced"])

    def test_interface_slot_all_implementations(self):
        unknown = ("class cIGZUnknown {\npublic:\n    virtual bool QueryInterface(uint32_t riid, void **out) = 0;\n"
                   "    virtual uint32_t AddRef() = 0;\n    virtual uint32_t Release() = 0;\n};\n")
        face = "class cIFace : public cIGZUnknown {\npublic:\n    virtual void Get(float *out) = 0;\n};\n"
        classes = [{"name": n, "bases": ["cIFace", "cIGZUnknown"], "base_offsets": [0, 0],
                    "vftables": [{"rva": "0x9000", "offset": 0, "entries": ["0x1", "0x2", "0x3", hex(r)]}]}
                   for n, r in (("cA", 0x5000), ("cB", 0x5100))]
        ours = "class cImpl : public cBase, public cIFace {\npublic:\n    void Get(float *out) override;\n};\n"

        def go(eax_map, impl=True):
            files = [("src/gz/unknown.h", unknown), ("src/x/face.h", face)] + ([("src/x/impl.h", ours)] if impl else [])
            found = arity.check(files, lambda rva: {4}, None, None, classes, facts={},
                                eax=lambda rva: eax_map.get(rva, ("none", "")))
            return [(f["verdict"], f["where"]) for f in found if f["kind"] == "return"]
        both = {0x5000: ("strong", "x"), 0x5100: ("strong", "y")}
        self.assertEqual(go(both), [("strong", "src/x/face.h:3 cIFace::Get")])
        mixed = {0x5000: ("strong", "x"), 0x5100: ("weak", "y")}
        self.assertEqual(go(mixed), [("weak", "src/x/face.h:3 cIFace::Get")])
        self.assertEqual(go({}), [])
        # nothing of ours implements the pure virtual: we only call it, and the game never reads our missing value
        self.assertEqual(go(both, impl=False), [("weak", "src/x/face.h:3 cIFace::Get")])


class GuardReturnWarning(unittest.TestCase):
    def test_strong_void_in_a_changed_file_is_a_warning(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "src").mkdir()
            saved = guard.git, guard.arity.function_starts
            guard.arity.function_starts = lambda: None
            guard.git = lambda *a: ""
            try:
                errors, warnings = [], []
                guard.check_arity("base", {"src/sg/item.h": cDataListItemLike.HEADER.replace("{ret}", "void")
                                           .replace("{note}", "")}, errors, warnings,
                                  rets=lambda rva: {4}, root=Path(tmp), classes=[],
                                  eax=lambda rva: {0x4000: ("strong", "eax at the ret: x")}.get(rva, ("none", "")))
            finally:
                guard.git, guard.arity.function_starts = saved
        self.assertEqual(errors, [])
        hits = [w for w in warnings if "declared void" in w]
        self.assertEqual(len(hits), 1, warnings)
        self.assertIn("cItem::GetVector3", hits[0])
        self.assertNotIn("SetVector3", hits[0])


EXE = Path(guard.tsdb.main_root() if hasattr(guard, "tsdb") else ".") / "local/original/Sims2EP9.exe.unpacked.exe"


@unittest.skipUnless(EXE.exists(), "needs the unpacked exe in local/")
class KnownOriginals(unittest.TestCase):
    """The case that crashed (lane OX's cDataListItem GetVector3, its out pointer in eax) and lane OY's setter whose
    eax is a Release's leftover."""

    def test_known_rvas(self):
        eax = arity.original_eax()
        self.assertEqual(eax(0xac6db0)[0], "strong")  # cDataListItem::GetVector3
        self.assertEqual(eax(0xa47400)[0], "strong")  # standard light slot 51 GetColor (lane OR)
        self.assertEqual(eax(0xa99430)[0], "weak")    # cDataListExtension setter (lane OY): correct as void
        self.assertEqual(eax(0xaf7420)[0], "strong")  # nGZCommandParser CommandTable Find: callers read [eax+4]
        self.assertEqual(eax(0x1c9368)[0], "weak")    # cRZStopwatch ElapsedAsFloat: none of 7 call sites reads eax
        self.assertEqual(eax(0x8bb480)[0], "weak")    # cGZDrawContext::Translate: an input pointer, only read


if __name__ == "__main__":
    unittest.main()
