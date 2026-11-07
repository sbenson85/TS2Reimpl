#!/usr/bin/env python3
"""Tests for tools/origsyms.py (the symbols behind OriginalFunction<Fn, 0xRVA>()) and for matchcheck reading them.

 - the decorated names match what MSVC 14.40 emits for OriginalEntry<RVA> (names copied from a compiler listing);
 - the generator finds every use, whatever the function type looks like;
 - matchcheck: our `call OriginalEntry<X>` is the original's call to X and to nothing else.

Synthetic only; no game bytes.

Run: .venv/Scripts/python -I tools/tests/test_origsyms.py
"""
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import origsyms as S  # noqa: E402
import matchcheck as M  # noqa: E402
from test_matchcheck_v6 import checker, objfile, original, CODE, COMDAT  # noqa: E402

# From `cl /FAs` on `template <uint32_t Rva> void OriginalEntry();` (MSVC 14.40): value -> decorated name.
COMPILER_NAMES = {
    0xcece60: "??$OriginalEntry@$0MOMOGA@@@YAXXZ",
    0xcece70: "??$OriginalEntry@$0MOMOHA@@@YAXXZ",
    0x5555: "??$OriginalEntry@$0FFFF@@@YAXXZ",
    0x1000: "??$OriginalEntry@$0BAAA@@@YAXXZ",
}


class Names(unittest.TestCase):
    def test_encode_matches_the_compiler(self):
        for rva, name in COMPILER_NAMES.items():
            self.assertEqual(S.encode(rva), name)

    def test_decode_roundtrip(self):
        for rva in (0x1000, 0x4f8e50, 0xcece60, 0xffffff, 0x123456):
            self.assertEqual(S.decode(S.encode(rva)), rva)
        for name in ("?f@@YAXXZ", "_ts2_rva_1000", "??$OriginalEntry@$0MOMOGA@@@YAXH@Z", "??$Other@$0MOMOGA@@@YAXXZ"):
            self.assertIsNone(S.decode(name))

    def test_small_rvas_refused(self):
        with self.assertRaises(ValueError):
            S.encode(0x10)


class Scan(unittest.TestCase):
    def test_finds_every_form(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "src").mkdir()
            (root / "tests").mkdir()
            (root / "src/a.cpp").write_text(
                "void f() {\n"
                "  OriginalFunction<void(__thiscall *)(void *, int), 0x1234>()(p, 1);\n"
                "  OriginalFunction<int(__cdecl *)(std::vector<std::pair<int, int>> *), 0xabcd00>()(v);\n"
                "  OriginalFunction<Fn,\n      0x2000>()();\n"
                "  OriginalFunction<void(__cdecl *)(), kNotLiteral>()();\n"
                "  OriginalFunction<void(__cdecl *)()>(0x3000)();\n"  # the old form: not a symbol
                "}\n")
            (root / "tests/t.cpp").write_text("auto g = OriginalFunction<void(__stdcall *)(int), 0x4000>();\n")
            (root / "src/game").mkdir()
            (root / "src/game/original.h").write_text("OriginalFunction<Fn, 0x5000>()\n")  # the declaration's own file
            self.assertEqual(sorted(S.scan_rvas(root)), [0x1234, 0x2000, 0x4000, 0xabcd00])

    def test_render(self):
        # valued game base + RVA - exe base: the linker adds the image base back to a rel32 reference
        text = S.render({0xcece60, 0x1000}, 0xa0000000, 0x400000)
        self.assertIn("PUBLIC ??$OriginalEntry@$0MOMOGA@@@YAXXZ", text)
        self.assertIn("??$OriginalEntry@$0MOMOGA@@@YAXXZ EQU 0A08ECE60h", text)
        self.assertIn("??$OriginalEntry@$0BAAA@@@YAXXZ EQU 09FC01000h", text)
        self.assertIn("??$OriginalEntry@$0BAAA@@@YAXXZ EQU 0A0001000h", S.render({0x1000}, 0xa0000000, 0))

    def test_owned_statics(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "src/game").mkdir(parents=True)
            (root / "standalone").mkdir()
            (root / "src/a.h").write_text(
                "TS2_OWNED_STATIC(GeneralAllocator, 10cb098);\n"
                "TS2_OWNED_STATIC(std::pair<int, int>, 10c95d4);\n"
                "TS2_OWNED_STATIC(BOOL(__stdcall *)(void *), 10caea0);\n")
            (root / "standalone/s.cpp").write_text("TS2_OWNED_STATIC(GeneralAllocator, 10cb098){};\n")
            (root / "src/game/owned.h").write_text("#define TS2_OWNED_STATIC(Type, hex) extern \"C\" Type ts2_rva_##hex\n")
            self.assertEqual(sorted(S.scan_owned(root)), ["10c95d4", "10caea0", "10cb098"])
            (root / "src/b.cpp").write_text("TS2_OWNED_STATIC(int, 10CB098);\n")  # one RVA, two C names
            with self.assertRaises(ValueError):
                S.scan_owned(root)
        # absolute data symbols: game base + RVA, whatever the exe base
        text = S.render({0x1000}, 0xa0000000, 0x400000, {"10cb098": "src/a.h:1"})
        self.assertIn("PUBLIC _ts2_rva_10cb098", text)
        self.assertIn("_ts2_rva_10cb098 EQU 0A10CB098h", text)
        self.assertNotIn("_ts2_rva_", S.render({0x1000}, 0xa0000000, 0x400000))


F = 0x1000
CALLEE = 0x2000


def call_rel(src, target):
    return b"\xe8" + struct.pack("<i", target - (src + 5))


class MatchCall(unittest.TestCase):
    """Original: `call CALLEE; ret` at F. Ours: `call OriginalEntry<X>; ret`."""

    def run_check(self, x, first=b"\xe8\0\0\0\0"):
        orig = original({F: call_rel(F, CALLEE) + b"\xc3"})
        sym = S.encode(x)
        obj = objfile([(".text$mn", first + b"\xc3", CODE | COMDAT, [(1, 1, M.REL32)])],
                      [("?f@@YAXXZ", 0, 0, 2, 0x20), (sym, 0, None, 2, 0)])
        ck = checker(orig, {hex(F): dict(rva=hex(F), size=6, name="f")})
        return ck.check(hex(F), [(obj, None, "?f@@YAXXZ")])

    def test_right_rva_matches(self):
        r = self.run_check(CALLEE)
        self.assertTrue(r["match"], r["reasons"])

    def test_wrong_rva_rejected(self):
        for x in (CALLEE + 0x10, CALLEE - 0x10, 0x4000):
            r = self.run_check(x)
            self.assertFalse(r["match"], x)
            self.assertTrue(any("call/jmp" in s for s in r["reasons"]), r["reasons"])

    def test_resolve_call_directly(self):
        ck = checker(original({}), {})
        self.assertTrue(ck.resolve_call(S.encode(CALLEE), CALLEE)[0])
        self.assertFalse(ck.resolve_call(S.encode(CALLEE), CALLEE + 1)[0])
        self.assertFalse(ck.resolve_call("??$OriginalEntry@$0MOMOGA@@@YAXH@Z", 0xcece60)[0])  # not our generated name

    def test_data_reference(self):
        # `mov eax, offset OriginalEntry<X>` (a function pointer taken, not called) against the original's pointer.
        ck = checker(original({}), {})
        obj = objfile([(".text$mn", b"", CODE | COMDAT, [])], [(S.encode(CALLEE), 0, None, 2, 0)])
        self.assertIsNone(ck.data_ref(obj, 0, 0, CALLEE))
        self.assertIsNotNone(ck.data_ref(obj, 0, 0, CALLEE + 4))


if __name__ == "__main__":
    unittest.main(verbosity=2)
