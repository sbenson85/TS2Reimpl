#!/usr/bin/env python3
"""Tests for tools/arity.py's "ecx" check (an OriginalFunction declared with a convention that leaves ecx unset whose
original reads ecx first: a thiscall called as a plain function, lane RD's 0xacadd0). Synthetic instruction sequences
written for these tests (no game bytes); one known RVA of the unpacked exe when it is present (local/).

Run: tools/py -I tools/tests/test_arity_ecx.py
"""
import os
import sys
import unittest
from collections import namedtuple

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import arity  # noqa: E402

Ins = namedtuple("Ins", "mnemonic op_str size")


class FakeImage:
    """Functions of "mnemonic operands" lines, one byte each, from their start RVA; base 0. A function whose first
    line is "frame" stands for __EH_prolog / _chkstk (Image.moves_esp)."""
    base = 0

    def __init__(self, **funcs):
        self.funcs = {}
        for name, lines in funcs.items():
            start = int(name[1:], 16)
            out = {}
            for k, line in enumerate(lines):
                m, _, ops = line.partition(" ")
                out[start + k] = Ins(m, ops.strip(), 1)
            self.funcs[start] = out

    def instructions(self, rva):
        return self.funcs[rva]

    def is_code(self, rva):
        return rva in self.funcs

    def moves_esp(self, rva):
        return rva in self.funcs and self.funcs[rva][rva].mnemonic == "frame"

    def callee(self, address):
        return self, address

    def noreturn(self, rva, depth):
        return False

    def table_targets(self, ins, entry):
        return []


def first(img, rva=0x1000):
    r = arity.ecx_first(img, rva)
    return None if r is None else r[0]


class EcxFirst(unittest.TestCase):
    def test_read_through_this(self):
        self.assertEqual(first(FakeImage(f1000=["push esi", "mov esi, ecx", "ret"])), 0x1001)
        self.assertEqual(first(FakeImage(f1000=["mov eax, dword ptr [ecx + 4]", "ret"])), 0x1000)
        self.assertEqual(first(FakeImage(f1000=["test ecx, ecx", "ret"])), 0x1000)

    def test_written_first_is_none(self):
        self.assertIsNone(first(FakeImage(f1000=["mov ecx, dword ptr [esp + 4]", "mov eax, dword ptr [ecx]",
                                                 "ret 4"])))
        self.assertIsNone(first(FakeImage(f1000=["xor ecx, ecx", "inc ecx", "ret"])))

    def test_push_ecx_reserves_a_slot(self):
        self.assertIsNone(first(FakeImage(f1000=["push ecx", "mov ecx, dword ptr [esp + 8]", "ret"])))

    def test_one_path_reads(self):
        img = FakeImage(f1000=["cmp eax, 0", "je 0x1004", "mov ecx, 1", "ret", "mov eax, ecx", "ret"])
        self.assertEqual(first(img), 0x1004)

    def test_call_clobbers(self):
        img = FakeImage(f1000=["call 0x2000", "mov eax, ecx", "ret"], f2000=["mov eax, 1", "ret"])
        self.assertIsNone(first(img))

    def test_callee_that_reads_it(self):
        img = FakeImage(f1000=["push 5", "call 0x2000", "ret"], f2000=["mov eax, dword ptr [ecx]", "ret 4"])
        self.assertEqual(first(img), 0x1001)

    def test_frame_helper_keeps_it(self):
        img = FakeImage(f1000=["mov eax, 0x10", "call 0x2000", "mov esi, ecx", "ret"], f2000=["frame", "ret"])
        self.assertEqual(first(img), 0x1002)

    def test_tail_jump_followed(self):
        img = FakeImage(f1000=["jmp 0x2000"], f2000=["mov eax, dword ptr [ecx + 8]", "ret"])
        self.assertEqual(first(img), 0x1000)

    def test_implicit_reads(self):
        self.assertEqual(first(FakeImage(f1000=["mov edi, eax", "rep stosd dword ptr es:[edi], eax", "ret"])), 0x1001)
        self.assertEqual(first(FakeImage(f1000=["shl eax, cl", "ret"])), 0x1000)


class PassesEcx(unittest.TestCase):
    def test_conventions(self):
        self.assertTrue(arity.passes_ecx(("thiscall", "void", ["void *"])))
        self.assertTrue(arity.passes_ecx(("fastcall", "void", ["int", "int"])))
        self.assertFalse(arity.passes_ecx(("fastcall", "void", [])))
        self.assertFalse(arity.passes_ecx(("stdcall", "void", ["void *"])))
        self.assertFalse(arity.passes_ecx(("cdecl", "int", ["int"])))


class Check(unittest.TestCase):
    SRC = ('#include "game/original.h"\n'
           'void A(void *p) { OriginalFunction<void(__stdcall *)(void *), 0x4000>()(p); }\n'
           'void B(void *p) { OriginalFunction<void(__thiscall *)(void *), 0x4000>()(p); }\n'
           'void C(void *p) { OriginalFunction<void(__stdcall *)(void *), 0x4100>()(p); }\n')

    def test_only_the_plain_call_of_a_reader(self):
        reads = {0x4000: (0x4002, "0x4002: mov esi, ecx")}
        found = arity.check([("src/x.cpp", self.SRC)], lambda rva: {4}, None, None, ecx=lambda rva: reads.get(rva))
        got = sorted((f["verdict"], f["where"]) for f in found if f["kind"] == "ecx")
        self.assertEqual(got, [("MISMATCH", "src/x.cpp:2"), ("ok", "src/x.cpp:4")])


class Image(unittest.TestCase):
    def test_rd_build_variant(self):
        try:
            img = arity.original_image()
        except Exception as e:  # noqa: BLE001
            self.skipTest(f"no image: {e}")
        self.assertIsNotNone(arity.ecx_first(img, 0xacadd0))  # thiscall (this = the manager), ret 0xc


if __name__ == "__main__":
    unittest.main()
