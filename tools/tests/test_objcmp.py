#!/usr/bin/env python3
"""Tests for tools/objcmp.py: what it leaves out, and that everything else still counts as a difference.

Run: tools/py -I tools/tests/test_objcmp.py
"""
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import objcmp  # noqa: E402

PCH = b"___@@_PchSym_@00@UaUb@objname"
ANON_NAME = b"?value@?A0x12345678@@3HA"


def objname_record(signature, path):
    body = struct.pack("<H", 0x1101) + signature + path + b"\0"
    return struct.pack("<H", len(body)) + body


def debug_section(signature, path):
    symbols = objname_record(signature, path)
    return b"\x04\0\0\0" + struct.pack("<II", 0xF1, len(symbols)) + symbols + b"\0" * (-len(symbols) % 4)


def coff(stamp=1, signature=b"AAAA", path=b"C:\\a\\x.obj", text=b"\x90\x90\xc3", drectve=b"/INCLUDE:" + PCH + b" ",
         anon=ANON_NAME, reloc_offset=4, chks=b"\1" * 8):
    """An object with .debug$S (one relocation), .text, .drectve, .chks64, and aux records for .debug$S's symbol."""
    raws = [(b".debug$S", debug_section(signature, path)), (b".text", text), (b".drectve", drectve), (b".chks64", chks)]
    table = 20 + 40 * len(raws)
    at = table
    heads = bytearray()
    blobs = bytearray()
    for i, (name, raw) in enumerate(raws):
        relocs = 1 if i == 0 else 0
        reloc_at = at + len(raw) if relocs else 0
        heads += name.ljust(8, b"\0") + struct.pack("<IIIIIIHHI", 0, 0, len(raw), at, reloc_at, 0, relocs, 0, 0)
        blobs += raw
        at += len(raw)
        if relocs:
            blobs += struct.pack("<IIH", reloc_offset + len(path), 2, 11)
            at += 10
    strings = PCH + b"\0" + anon + b"\0"
    pool = struct.pack("<I", 4 + len(strings)) + strings

    def symbol(offset, value, section, storage, aux):
        return struct.pack("<II", 0, offset) + struct.pack("<IhHBB", value, section, 0, storage, aux)

    symbols = (symbol(4, 0, 1, objcmp.IMAGE_SYM_CLASS_STATIC, 1)
               + struct.pack("<IHHIHHH", len(raws[0][1]), 1, 0, sum(path), 0, 0, 0)  # the section's aux record
               + symbol(4 + len(PCH) + 1, 8, 2, 2, 0))
    head = struct.pack("<HHIIIHH", 0x14C, len(raws), stamp, at, 3, 0, 0)
    return bytes(head + heads + blobs) + symbols + pool


class Objects(unittest.TestCase):
    def verdict(self, a=None, b=None, **options):
        return objcmp.compare(coff(**(a or {})), coff(**(b or {})), **options)

    def test_identical(self):
        self.assertEqual(self.verdict(), ("same", ""))

    def test_time_stamp_and_signature_are_time(self):
        self.assertEqual(self.verdict(b={"stamp": 2})[0], "time")
        self.assertEqual(self.verdict(b={"signature": b"BBBB"})[0], "time")

    def test_object_path_with_what_follows_from_it(self):
        self.assertEqual(self.verdict(b={"path": b"C:\\deeper\\path\\x.obj"})[0], "path")

    def test_pch_symbol(self):
        other = b"/INCLUDE:___@@_PchSym_@00@UaUbUlonger@objname "
        self.assertEqual(self.verdict(b={"drectve": other})[0], "pchsym")

    def test_chks64_follows_the_sections(self):
        self.assertEqual(self.verdict(b={"chks": b"\2" * 8})[0], "same")

    def test_code_is_a_difference(self):
        self.assertEqual(self.verdict(b={"text": b"\x90\x91\xc3"}), ("DIFF", "section .text: raw"))

    def test_other_directive_is_a_difference(self):
        self.assertEqual(self.verdict(b={"drectve": b"/INCLUDE:_other "})[0], "DIFF")

    def test_symbol_names_are_a_difference(self):
        self.assertEqual(self.verdict(b={"anon": ANON_NAME.replace(b"value", b"other")})[0], "DIFF")

    def test_anon_hash_only_on_request(self):
        other = {"anon": ANON_NAME.replace(b"12345678", b"9abcdef0")}
        self.assertEqual(self.verdict(b=other)[0], "DIFF")
        self.assertEqual(self.verdict(b=other, anon=True)[0], "anon")

    def test_anon_leaves_the_rest_in(self):
        other = {"anon": ANON_NAME.replace(b"12345678", b"9abcdef0"), "text": b"\x90\x91\xc3"}
        self.assertEqual(self.verdict(b=other, anon=True)[0], "DIFF")


def image(stamp=1, body=b"\0" * 64):
    pe = 0x40
    optional = 224
    data = bytearray(0x400)
    struct.pack_into("<I", data, 0x3C, pe)
    data[pe:pe + 4] = b"PE\0\0"
    struct.pack_into("<HHI", data, pe + 4, 0x14C, 1, stamp)
    struct.pack_into("<H", data, pe + 20, optional)
    struct.pack_into("<H", data, pe + 24, 0x10B)
    table = pe + 24 + optional
    data[table:table + 8] = b".text\0\0\0"
    struct.pack_into("<IIII", data, table + 8, 0x200, 0x1000, 0x200, 0x200)
    data[0x200:0x200 + len(body)] = body
    return bytes(data)


class Images(unittest.TestCase):
    def test_link_time_is_left_out(self):
        other, stamps = objcmp.compare_images(image(1), image(2))
        self.assertEqual((other, len(stamps)), ([], 1))

    def test_a_byte_of_code_is_not(self):
        other, _ = objcmp.compare_images(image(), image(body=b"\1" + b"\0" * 63))
        self.assertEqual(other, [0x200])

    def test_size_is_a_message(self):
        self.assertIsInstance(objcmp.compare_images(image(), image() + b"\0")[0], str)

    def test_anon_digits_only_on_request(self):
        a, b = (image(body=b"xx?A0x" + digits + b"@@") for digits in (b"12345678", b"9abcdef0"))
        self.assertEqual(len(objcmp.compare_images(a, b)[0]), 8)
        other, left_out = objcmp.compare_images(a, b, anon=True)
        self.assertEqual((other, len(left_out)), ([], 8))


class Trees(unittest.TestCase):
    def test_nothing_found_is_a_failure(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            done = subprocess.run([sys.executable, "-I", objcmp.__file__, a, b], capture_output=True, text=True)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)


if __name__ == "__main__":
    unittest.main()
