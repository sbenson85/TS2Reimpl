#!/usr/bin/env python3
"""Tests for tools/checkgamebase.py: the linked-map checks, the fixed-image check and the object scan that keeps
every reference to an OriginalEntry symbol a rel32 call or jump.

Synthetic only (a hand-built COFF object, map text and PE header); no game bytes.

Run: .venv/Scripts/python -I tools/tests/test_checkgamebase.py
"""
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import checkgamebase as C  # noqa: E402
import origsyms as S  # noqa: E402

REL32, DIR32 = 0x14, 0x06


def coff(relocs, names):
    """A COFF object with one section (4 bytes of code) and the relocations [(symbol index, type)]. names: symbol
    names (long ones go to the string table, as the compiler does for decorated names)."""
    strtab = bytearray(4)
    syms = bytearray()
    for n in names:
        raw = n.encode()
        if len(raw) <= 8:
            syms += raw.ljust(8, b"\0")
        else:
            syms += struct.pack("<II", 0, len(strtab))
            strtab += raw + b"\0"
        syms += struct.pack("<IhHBB", 0, 0, 0x20, 2, 0)
    strtab[0:4] = struct.pack("<I", len(strtab))
    raw_ptr = 20 + 40
    rel_ptr = raw_ptr + 4
    sym_ptr = rel_ptr + 10 * len(relocs)
    out = struct.pack("<HHIIIHH", 0x14C, 1, 0, sym_ptr, len(names), 0, 0)
    out += struct.pack("<8sIIIIIIHHI", b".text", 0, 0, 4, raw_ptr, rel_ptr, 0, len(relocs), 0, 0x60000020)
    out += b"\0" * 4
    for sym, typ in relocs:
        out += struct.pack("<IIH", 0, sym, typ)
    return bytes(out + syms + strtab)


class ObjectScan(unittest.TestCase):
    NAME = S.encode(0xcece60)

    def test_rel32_is_fine(self):
        self.assertEqual(C.non_rel32_refs(coff([(0, REL32)], [self.NAME])), [])

    def test_address_taken_is_refused(self):
        self.assertEqual(C.non_rel32_refs(coff([(0, DIR32)], [self.NAME])), [(".text", DIR32)])

    def test_other_symbols_are_not_ours(self):
        self.assertEqual(C.non_rel32_refs(coff([(0, DIR32), (1, REL32)], ["_ts2_rva_0", self.NAME])), [])

    def test_mixed(self):
        self.assertEqual(C.non_rel32_refs(coff([(1, REL32), (1, DIR32), (0, DIR32)], ["_ts2_rva_0", self.NAME])),
                         [(".text", DIR32)])

    def test_directory(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "good.obj").write_bytes(coff([(0, REL32)], [self.NAME]))
            self.assertEqual(C.check_objects(d), 0)
            (Path(d) / "bad.obj").write_bytes(coff([(0, DIR32)], [self.NAME]))
            self.assertEqual(C.check_objects(d), 1)


class MapCheck(unittest.TestCase):
    def check(self, value, base="00400000", kind="<absolute>"):
        text = (f" Preferred load address is {base}\n"
                f" 0000:a0000000       _ts2_rva_0                 a0000000     <absolute>\n"
                f" 0000:{value}       {S.encode(0xcece60)}   {value}     {kind}\n")
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.map"
            p.write_text(text)
            return C.check_map(str(p))

    def test_value_is_target_minus_image_base(self):
        # game base 0xa0000000 + 0xcece60 - 0x400000
        self.assertEqual(self.check(f"{(C.BASE + 0xcece60 - 0x400000):08x}"), 0)

    def test_target_itself_is_wrong(self):
        self.assertEqual(self.check(f"{C.BASE + 0xcece60:08x}"), 1)

    def test_wrong_rva_and_wrong_base(self):
        self.assertEqual(self.check(f"{(C.BASE + 0xcece70 - 0x400000):08x}"), 1)
        self.assertEqual(self.check(f"{(C.BASE + 0xcece60 - 0x400000):08x}", base="00500000"), 1)

    def test_not_absolute(self):
        self.assertEqual(self.check(f"{(C.BASE + 0xcece60 - 0x400000):08x}", kind="ts2core:a.obj"), 1)


class FixedImage(unittest.TestCase):
    def exe(self, characteristics, dll_characteristics):
        pe = 0x80
        data = bytearray(0x200)
        data[0:2] = b"MZ"
        struct.pack_into("<I", data, 0x3C, pe)
        data[pe:pe + 4] = b"PE\0\0"
        struct.pack_into("<H", data, pe + 22, characteristics)
        struct.pack_into("<H", data, pe + 24 + 70, dll_characteristics)
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.exe"
            p.write_bytes(bytes(data))
            return C.check_exe(str(p))

    def test_fixed(self):
        self.assertEqual(self.exe(0x0001 | 0x0100, 0x8000), 0)

    def test_relocatable_refused(self):
        self.assertEqual(self.exe(0x0100, 0x8000), 1)
        self.assertEqual(self.exe(0x0001, 0x8000 | 0x0040), 1)
        self.assertEqual(self.exe(0x0100, 0x8000 | 0x0040), 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
