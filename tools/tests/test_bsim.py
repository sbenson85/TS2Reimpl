#!/usr/bin/env python3
"""Tests for tools/bsim.py's candidate ranking and tools/sdkucrt.py's lib matching, on synthetic inputs
(no Ghidra, no network).

Run: tools/py -I tools/tests/test_bsim.py
"""
import json
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import bsim  # noqa: E402
import sdkucrt  # noqa: E402


def row(rva, size, *matches):
    return {"rva": rva, "size": size, "self": 30.0,
            "matches": [{"db": "x", "exe": e, "name": n, "sim": s, "sig": g} for e, n, s, g in matches]}


class LibraryOf(unittest.TestCase):
    def test_strips_versions_and_flags(self):
        self.assertEqual(bsim.library_of("libucrt-10.0.22621.3233/strtod.obj"), "libucrt")
        self.assertEqual(bsim.library_of("libcpmt-14.40/xlocale.obj"), "libcpmt")
        self.assertEqual(bsim.library_of("lua50-O2/lapi.obj"), "lua50")
        self.assertEqual(bsim.library_of("jpeg6a-O1OiGS/jdhuff.obj"), "jpeg6a")
        self.assertEqual(bsim.library_of("stlprobe/probe0.obj"), "stlprobe")


class Rank(unittest.TestCase):
    def test_skips_known_and_weak_and_orders_by_size(self):
        rows = [
            row("0x100", 40, ("libucrt-10.0.22621.3233/a.obj", "a", 0.99, 30.0)),
            row("0x200", 400, ("lua50-O2/b.obj", "b", 0.80, 50.0), ("lua50-O2/c.obj", "c", 0.96, 45.0)),
            row("0x300", 900, ("libcmt-14.40/d.obj", "d", 0.99, 90.0)),     # known: skipped
            row("0x400", 50, ("libcmt-14.40/e.obj", "e", 0.70, 9.0)),      # under min_sim: skipped
            row("0x500", 60),                                               # no matches
        ]
        got = bsim.rank(rows, known={0x300}, min_sim=0.9)
        self.assertEqual([c["rva"] for c in got], ["0x200", "0x100"])
        self.assertEqual(got[0]["name"], "c")           # best match by similarity, not first listed
        self.assertEqual(got[0]["lib"], "lua50")
        self.assertEqual(got[0]["confidence"], "high")

    def test_confidence_levels(self):
        self.assertEqual(bsim.confidence(0.99, 25.0, 0), "high")
        self.assertEqual(bsim.confidence(0.99, 12.0, 0), "medium")   # small function: not high
        self.assertEqual(bsim.confidence(0.90, 15.0, 0), "medium")
        self.assertEqual(bsim.confidence(0.99, 5.0, 0), "low")

    def test_by_library_sums(self):
        cands = [{"lib": "a", "size": 10}, {"lib": "a", "size": 5}, {"lib": "b", "size": 7}]
        self.assertEqual(bsim.by_library(cands), {"a": [2, 15], "b": [1, 7]})

    def test_known_rvas_reads_both_files(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "db").mkdir()
            (Path(d) / "db/library-matches.jsonl").write_text(json.dumps({"rva": "0x10"}) + "\n")
            (Path(d) / "db/crt-equivalents.jsonl").write_text(json.dumps({"rva": "0x20", "name": "x"}) + "\n")
            self.assertEqual(bsim.known_rvas(Path(d)), {0x10, 0x20})


def coff(code, relocs=(), name=b"_f"):
    """A one-section, one-function i386 COFF object: code in .text, DIR32 relocations at the given offsets."""
    nsect, symptr_after = 1, None
    hdr_size, sect_size = 20, 40
    rawptr = hdr_size + sect_size
    relptr = rawptr + len(code)
    symptr = relptr + 10 * len(relocs)
    obj = struct.pack("<HHIIIHH", 0x14C, nsect, 0, symptr, 1, 0, 0)
    obj += struct.pack("<8sIIIIIIHHI", b".text", 0, 0, len(code), rawptr, relptr, 0, len(relocs), 0, 0x60000020)
    obj += code
    for off in relocs:
        obj += struct.pack("<IIH", off, 0, 0x06)
    obj += struct.pack("<8sIhHBB", name.ljust(8, b"\0"), 0, 1, 0x20, 2, 0)
    obj += struct.pack("<I", 4)  # empty string table
    return obj


def archive(members):
    data = b"!<arch>\n"
    data += b"/".ljust(16) + b"0".ljust(12) + b"0".ljust(6) + b"0".ljust(6) + b"0".ljust(8) + b"0".ljust(10) + b"`\n"
    for name, body in members:
        data += (name + "/").encode().ljust(16) + b"0".ljust(32) + str(len(body)).encode().ljust(10) + b"`\n" + body
        if len(body) & 1:
            data += b"\n"
    return data


class SdkUcrt(unittest.TestCase):
    def test_pick_member(self):
        names = ["c/ucrt/arm64/libucrt.lib", "c/ucrt/x86/libucrt.lib", "c/ucrt/x86/libucrtd.lib", "c/um/x86/kernel32.lib"]
        self.assertEqual(sdkucrt.pick_member(names), "c/ucrt/x86/libucrt.lib")
        with self.assertRaises(ValueError):
            sdkucrt.pick_member(["c/ucrt/x64/libucrt.lib"])

    def test_match_lib_masks_relocations(self):
        body = b"\x55\x8b\xec\xa1\x00\x00\x00\x00\x03\x45\x08\x5d\xc3\x90\x90\x90\x90\x90\x90\x90"[:13]
        body = body + b"\x8b\xc0\x8b\xc0\xc3"  # 18 bytes: over MIN_SIZE, so no uniqueness rule
        lib = archive([("f.obj", coff(body, relocs=[4]))])
        game = bytearray(0x100)
        game[0x10:0x10 + len(body)] = body
        game[0x14:0x18] = b"\x78\x56\x34\x12"           # relocated operand differs: still a match
        game[0x40:0x40 + len(body)] = body
        game[0x40] = 0x56                                # an unmasked byte differs: no match
        funcs = [{"rva": "0x10", "size": len(body)}, {"rva": "0x40", "size": len(body)}]
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "t.lib"
            p.write_bytes(lib)
            got = sdkucrt.match_lib(p, bytes(game), funcs)
        self.assertEqual(got, {0x10: (len(body), ["_f"])})
        with tempfile.TemporaryDirectory() as d:     # a directory of .obj files works the same
            (Path(d) / "f.obj").write_bytes(coff(body, relocs=[4]))
            self.assertEqual(sdkucrt.match_lib(Path(d), bytes(game), funcs), {0x10: (len(body), ["_f"])})

    def test_extract_keeps_members_with_code(self):
        code = b"\x55\x8b\xec\x5d\xc3" * 4
        data_only = struct.pack("<HHIIIHH", 0x14C, 0, 0, 0, 0, 0, 0)
        lib = archive([("dir\\a.obj", coff(code)), ("b.obj", data_only), ("c.obj", coff(code, name=b"_g"))])
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "t.lib"
            p.write_bytes(lib)
            self.assertEqual(bsim.extract(p, Path(d) / "out"), 2)
            self.assertEqual(sorted(x.name for x in (Path(d) / "out").iterdir()), ["0000_a.obj", "0002_c.obj"])


if __name__ == "__main__":
    unittest.main()
