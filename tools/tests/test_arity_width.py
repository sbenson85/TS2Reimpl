#!/usr/bin/env python3
"""Tests for tools/arity.py's "width" check (a bool or 8/16-bit declaration whose original leaves a full-width value
in eax) and its warning in tools/guard.py. Synthetic instruction sequences written for these tests (no game bytes);
the two texture slot 7 RVAs of the unpacked exe when it is present (local/).

Run: tools/py -I tools/tests/test_arity_width.py
"""
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import arity  # noqa: E402
import guard  # noqa: E402
from test_arity_return import body, succ  # noqa: E402


def verdict(*lines, bits=1, signed=False, calls=None):
    at = arity.width_at_rets(body(*lines), 0, succ, bits, signed, 0)
    return arity.width_verdict(at, (lambda r: calls.get(r, "weak")) if calls else None)[0]


class WidthAtRets(unittest.TestCase):
    def test_full_width_load_is_strong(self):
        # lane TN's texture slot 7 shape: a count returned whole
        self.assertEqual(verdict("mov eax, dword ptr [ecx + 0x2c]", "ret"), "strong")
        self.assertEqual(verdict("push esi", "mov esi, dword ptr [ecx + 4]", "mov eax, esi", "pop esi", "ret"),
                         "strong")

    def test_low_part_only_is_fine(self):
        self.assertEqual(verdict("cmp dword ptr [ecx + 0x2c], 0", "setne al", "ret"), "none")
        self.assertEqual(verdict("mov al, 1", "ret"), "none")
        self.assertEqual(verdict("xor al, al", "ret"), "none")
        self.assertEqual(verdict("mov al, byte ptr [ecx + 4]", "ret"), "none")

    def test_bool_constants(self):
        self.assertEqual(verdict("mov eax, 1", "ret"), "none")
        self.assertEqual(verdict("xor eax, eax", "ret"), "none")
        self.assertEqual(verdict("mov eax, 2", "ret"), "strong")
        self.assertEqual(verdict("or eax, 0xffffffff", "ret"), "strong")
        self.assertEqual(verdict("mov eax, 0xff", "ret", bits=8), "none")
        self.assertEqual(verdict("mov eax, 0x100", "ret", bits=8), "strong")
        self.assertEqual(verdict("or eax, 0xffffffff", "ret", bits=8, signed=True), "strong")

    def test_one_full_path_is_enough(self):
        self.assertEqual(verdict("cmp ecx, 0", "je 4", "mov al, 1", "ret", "mov eax, dword ptr [ecx]", "ret"),
                         "strong")

    def test_extended_or_masked_is_fine(self):
        self.assertEqual(verdict("movzx eax, byte ptr [ecx + 4]", "ret"), "none")
        self.assertEqual(verdict("mov eax, dword ptr [ecx]", "shr eax, 3", "and eax, 1", "ret"), "none")
        self.assertEqual(verdict("mov eax, dword ptr [ecx]", "neg eax", "sbb eax, eax", "neg eax", "ret"), "none")
        self.assertEqual(verdict("mov eax, dword ptr [ecx]", "shr eax, 0x1f", "ret"), "none")
        self.assertEqual(verdict("movzx eax, word ptr [ecx + 4]", "ret", bits=16), "none")
        self.assertEqual(verdict("movzx eax, word ptr [ecx + 4]", "ret"), "strong")
        self.assertEqual(verdict("mov eax, dword ptr [ecx]", "sbb eax, eax", "ret"), "strong")

    def test_lea_after_a_low_write_stays_low(self):
        # MSVC's `x % n == 0` as a bool: sbb dl, dl; lea eax, [edx + 1] (only al counts)
        self.assertEqual(verdict("cdq", "idiv dword ptr [ecx + 0x38]", "neg edx", "sbb dl, dl", "lea eax, [edx + 1]",
                                 "ret"), "none")
        self.assertEqual(verdict("lea eax, [ecx + 0x10]", "ret"), "strong")  # an address

    def test_weak_cases(self):
        self.assertEqual(verdict("mov eax, dword ptr [esp + 4]", "ret 4"), "weak")  # a stack slot (spilled bool?)
        self.assertEqual(verdict("call 0x100", "ret"), "weak")  # a call's result, unresolved
        self.assertEqual(verdict("mov eax, dword ptr [ecx]", "add eax, edx", "ret"), "weak")
        # an out pointer stored through, then the ret: a void original's leftover
        self.assertEqual(verdict("mov eax, dword ptr [ecx + 8]", "mov dword ptr [eax], 0", "ret 4"), "weak")
        self.assertEqual(verdict("cmp ecx, 0", "je 3", "mov al, 1", "ret"), "weak")  # a path leaves eax unwritten

    def test_calls_resolve_but_stay_weak_when_full(self):
        self.assertEqual(verdict("call 0x100", "ret", calls={0x100: "none"}), "none")
        self.assertEqual(verdict("call 0x100", "ret", calls={0x100: "strong"}), "weak")


class CallerReads(unittest.TestCase):
    def after(self, *lines, narrow=8):
        return arity.eax_width_read_after(body(*lines), 0, succ, narrow)

    def test_low_full_returned(self):
        self.assertEqual(self.after("test al, al", "je 3", "ret", "ret"), "low")
        self.assertEqual(self.after("movzx ecx, al", "ret"), "low")
        self.assertEqual(self.after("mov byte ptr [esi], al", "ret"), "low")
        self.assertEqual(self.after("test eax, eax", "ret"), "full")
        self.assertEqual(self.after("mov ecx, eax", "ret"), "full")
        self.assertEqual(self.after("push eax", "ret"), "full")
        self.assertEqual(self.after("mov ecx, dword ptr [eax]", "ret"), "full")
        self.assertEqual(self.after("add esp, 8", "ret"), "returned")
        self.assertEqual(self.after("xor eax, eax", "ret"), "dropped")
        self.assertEqual(self.after("test ax, ax", "ret", narrow=16), "low")
        self.assertEqual(self.after("test ax, ax", "ret"), "full")


def check(files, width_map, classes=None):
    found = arity.check(list(files.items()), lambda rva: {0}, None, None, classes, facts={},
                        width=lambda rva, bits, signed: width_map.get(rva, ("none", "")))
    return [(f["verdict"], f["where"], f["rva"]) for f in found if f["kind"] == "width"]


UNKNOWN = ("class cIGZUnknown {\npublic:\n    virtual bool QueryInterface(uint32_t riid, void **out) = 0;\n"
           "    virtual uint32_t AddRef() = 0;\n    virtual uint32_t Release() = 0;\n};\n")


class WidthCheck(unittest.TestCase):
    W = {0x4000: ("strong", "eax at the ret: a 32-bit load")}

    def test_declarations_naming_their_original(self):
        for ret in ("bool", "uint8_t", "const bool", "int16_t", "wchar_t"):
            h = f"class cA {{\npublic:\n    {ret} Get(); // 0x4000\n}};\n"
            self.assertEqual(check({"src/a.h": h}, self.W), [("strong", "src/a.h:3 cA::Get", "0x4000")], ret)
        for ret in ("uint32_t", "int", "void", "bool *"):
            h = f"class cA {{\npublic:\n    {ret} Get(); // 0x4000\n}};\n"
            self.assertEqual(check({"src/a.h": h}, self.W), [], ret)

    def test_alias_and_slot_comment(self):
        h = "using Flag = bool;\nclass cA {\npublic:\n    Flag Get() override { return x != 0; } // 7: 0x4000 (shared)\n};\n"
        self.assertEqual(check({"src/a.h": h}, self.W), [("strong", "src/a.h:4 cA::Get", "0x4000")])

    def test_marker_silences(self):
        h = "class cA {\npublic:\n    bool Get(); // 0x4000 eax-width: every caller tests al\n};\n"
        self.assertEqual(check({"src/a.h": h}, self.W), [("silenced", "src/a.h:3 cA::Get", "0x4000")])
        h = "class cA {\npublic:\n    bool Get(); // 0x4000 eax-ignored: not this marker\n};\n"
        self.assertEqual(check({"src/a.h": h}, self.W), [("strong", "src/a.h:3 cA::Get", "0x4000")])

    def test_hook_entry(self):
        h = "class cA {\npublic:\n    bool Get();\n};\n"
        table = '#include "x"\nconst Hook kHooks[] = {\n    {"Get", 0x4000, 32, 0x1, MemberAddress(&cA::Get)},\n};\n'
        self.assertEqual(check({"src/a.h": h, "src/hook/tables/cA.cpp": table}, self.W),
                         [("strong", "src/hook/tables/cA.cpp:3 Get", "0x4000")])

    def test_interface_slot_any_implementation(self):
        face = "class cIFace : public cIGZUnknown {\npublic:\n    virtual bool IsAvailable() = 0;\n};\n"
        classes = [{"name": n, "bases": ["cIFace", "cIGZUnknown"], "base_offsets": [0, 0],
                    "vftables": [{"rva": "0x9000", "offset": 0, "entries": ["0x1", "0x2", "0x3", hex(r)]}]}
                   for n, r in (("cA", 0x5000), ("cB", 0x5100))]
        files = {"src/gz/unknown.h": UNKNOWN, "src/x/face.h": face}
        # lane TN: one implementation loads a count, the other is mov eax, 1 (fine for bool)
        got = check(files, {0x5000: ("strong", "a 32-bit load"), 0x5100: ("none", "a constant")}, classes)
        self.assertEqual(got, [("strong", "src/x/face.h:3 cIFace::IsAvailable", "0x5000")])
        self.assertEqual(check(files, {0x5100: ("none", "a constant")}, classes), [])

    def test_interface_defined_in_two_namespaces_uses_the_slot_comment(self):
        # cITexture is defined in nGZGraphic4 and nGZSceneGraph: the layout can't place it by bare name
        other = "namespace nA {\nclass cIFace : public cIGZUnknown {\npublic:\n    virtual int X() = 0;\n};\n}\n"
        face = ("namespace nB {\nclass cIFace : public cIBase {\npublic:\n"
                "    virtual bool IsAvailable() = 0; // 3: true in both\n};\n}\n")
        classes = [{"name": "nB::cImpl", "bases": ["nB::cIFace", "cIGZUnknown"], "base_offsets": [0, 0],
                    "vftables": [{"rva": "0x9000", "offset": 0, "entries": ["0x1", "0x2", "0x3", "0x5000"]}]},
                   {"name": "nA::cImpl", "bases": ["nA::cIFace", "cIGZUnknown"], "base_offsets": [0, 0],
                    "vftables": [{"rva": "0x9100", "offset": 0, "entries": ["0x1", "0x2", "0x3", "0x5100"]}]}]
        files = {"src/gz/unknown.h": UNKNOWN, "src/a/face.h": other, "src/b/face.h": face}
        got = check(files, {0x5000: ("strong", "a 32-bit load"), 0x5100: ("strong", "not this one")}, classes)
        self.assertEqual(got, [("strong", "src/b/face.h:4 cIFace::IsAvailable", "0x5000")])


class GuardWidthWarning(unittest.TestCase):
    def test_strong_width_in_a_changed_file_is_a_warning(self):
        h = "class cA {\npublic:\n    bool Get(); // 0x4000\n    bool Set(); // 0x4100\n};\n"
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "src").mkdir()
            saved = guard.git, guard.arity.function_starts
            guard.arity.function_starts = lambda: None
            guard.git = lambda *a: ""
            try:
                errors, warnings = [], []
                guard.check_arity("base", {"src/a.h": h}, errors, warnings, rets=lambda rva: {0}, root=Path(tmp),
                                  classes=[], width=lambda rva, b, s: {0x4000: ("strong", "x")}.get(rva, ("none", "")))
            finally:
                guard.git, guard.arity.function_starts = saved
        self.assertEqual(errors, [])
        hits = [w for w in warnings if "full-width" in w]
        self.assertEqual(len(hits), 1, warnings)
        self.assertIn("cA::Get", hits[0])
        self.assertNotIn("cA::Set", hits[0])


EXE = Path(guard.tsdb.main_root()) / "local/original/Sims2EP9.exe.unpacked.exe"


@unittest.skipUnless(EXE.exists(), "needs the unpacked exe in local/")
class KnownOriginals(unittest.TestCase):
    def test_texture_slot_7(self):
        width = arity.original_width()
        self.assertEqual(width(0x147770, 1, False)[0], "strong")  # mov eax, [ecx+0x2c]: the device texture count
        self.assertEqual(width(0x15e510, 1, False)[0], "none")  # mov eax, 1


if __name__ == "__main__":
    unittest.main()
