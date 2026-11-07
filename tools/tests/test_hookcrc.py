#!/usr/bin/env python3
"""Tests for tools/hookcrc.py: hook rows' site CRCs checked the way the host computes them (src/hook/hooks.cpp SiteCrc:
crc32 of checkLen bytes, at most 64), on a fake image.

Run: tools/py -I tools/tests/test_hookcrc.py
"""
import os
import sys
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import hookcrc  # noqa: E402

CODE = bytes((i * 7 + 3) & 0xFF for i in range(0x200))  # the "image" from rva 0x1000


class FakePE:
    def get_data(self, rva, n):
        return CODE[rva - 0x1000:rva - 0x1000 + n]


def crc(rva, n):
    return zlib.crc32(CODE[rva - 0x1000:rva - 0x1000 + n]) & 0xFFFFFFFF


def row(name, rva, n, c):
    return f'    {{"{name}", {rva:#x}, {n}, 0x{c:08x}, reinterpret_cast<void *>(&X::{name})}},\n'


class CheckText(unittest.TestCase):
    def check(self, *rows):
        return hookcrc.check_text("src/hook/tables/G.cpp", "const HookSpec kHooks[] = {\n" + "".join(rows) + "};\n",
                                  FakePE())

    def test_a_row_hookspec_wrote_passes(self):
        self.assertEqual(self.check(row("A", 0x1000, 32, crc(0x1000, 32)), row("B", 0x1040, 6, crc(0x1040, 6))), (2, []))

    def test_the_crc_covers_check_len_bytes_not_32(self):
        # Lane DW's cTSPersistResKeyList_Create row: checkLen 64 (the function's size) with the CRC of 32 bytes. The host
        # CRCs 64 bytes and skips the hook; the old tool took min(checkLen, 32) and passed it.
        n, problems = self.check(row("Create", 0x1000, 64, crc(0x1000, 32)))
        self.assertEqual(n, 1)
        [p] = problems
        self.assertIn("hook Create 0x1000", p)
        self.assertIn(f"the exe's is 0x{crc(0x1000, 64):08x}", p)
        self.assertIn(f"or 32 and 0x{crc(0x1000, 32):08x}", p)  # the row hookspec would write
        self.assertEqual(self.check(row("Create", 0x1000, 64, crc(0x1000, 64))), (1, []))  # what the host accepts

    def test_lengths_the_host_cant_use(self):
        _, problems = self.check(row("Long", 0x1000, 65, crc(0x1000, 65)), row("Short", 0x1000, 4, crc(0x1000, 4)))
        self.assertEqual(len(problems), 2)
        self.assertIn("at most 64 bytes", problems[0])
        self.assertIn("under 5 bytes", problems[1])

    def test_rows_in_other_shapes_are_still_found(self):
        text = f'{{"Spaced",   0x1010,  8,  0x{crc(0x1010, 8):08x}, nullptr}}, {{"Bad", 0x1010, 8, 0x12345678, nullptr}}'
        n, problems = hookcrc.check_text("t.cpp", text, FakePE())
        self.assertEqual((n, len(problems)), (2, 1))
        self.assertIn("hook Bad", problems[0])


if __name__ == "__main__":
    unittest.main()
