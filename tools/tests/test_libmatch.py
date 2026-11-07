#!/usr/bin/env python3
"""Tests for tools/libmatch.py on synthetic COFF objects and a synthetic image: exact, prefix, unlisted and inside
matches, the uniqueness rule for short non-exact matches, and the relocation checks (names_by_relocs, family,
calls, generic initialisers dropped, local labels).

Run: .venv/Scripts/python -I tools/tests/test_libmatch.py
"""
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import libmatch as L  # noqa: E402

BASE = 0x400000
TEXT = (0x1000, 0x3000)


def make_obj(funcs, externs=()):
    """A COFF object with one code section per function (as /Gy makes them). funcs: [(name, code bytes,
    [(offset, symbol name, type)])]; a relocation's symbol is a function of the object (local when it is the
    function itself: its own section) or one of externs. Returns the object's bytes."""
    n = len(funcs)
    syms = [(name, 0, i + 1, 0x20, 2) for i, (name, _, _) in enumerate(funcs)] + [(e, 0, 0, 0x20, 2) for e in externs]
    index = {s[0]: i for i, s in enumerate(syms)}
    data, headers = b"", b""
    pos = 20 + 40 * n
    for name, body, rels in funcs:
        relocs = b"".join(struct.pack("<IIH", off, index[sym], typ) for off, sym, typ in rels)
        headers += struct.pack("<8sIIIIIIHHI", b".text$mn", 0, 0, len(body), pos, pos + len(body), 0, len(rels), 0,
                               0x60000020)
        data += body + relocs
        pos += len(body) + len(relocs)
    strtab = b""
    symtab = b""
    for name, value, sec, typ, cls in syms:
        raw = name.encode()
        if len(raw) <= 8:
            field = raw.ljust(8, b"\0")
        else:
            field = struct.pack("<II", 0, 4 + len(strtab))
            strtab += raw + b"\0"
        symtab += field + struct.pack("<IHHBB", value, sec, typ, cls, 0)
    header = struct.pack("<HHIIIHH", 0x14C, n, 0, pos, len(syms), 0, 0)
    return header + headers + data + symtab + struct.pack("<I", 4 + len(strtab)) + strtab


def call(at_site, target):
    """e8 rel32 at at_site (rva of the e8) to target."""
    return b"\xe8" + struct.pack("<i", target - (at_site + 5))


# 40 distinct-looking bytes of straight code (no relocations), ending in ret.
BODY40 = bytes([0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x56, 0x57, 0x8B, 0x7D, 0x08, 0x33, 0xDB, 0x8B, 0xF3, 0x39, 0x5F,
                0x04, 0x76, 0x0C, 0x8B, 0x07, 0x03, 0x04, 0xB0, 0x46, 0x3B, 0x77, 0x04, 0x72, 0xF4, 0x5F, 0x5E, 0x5B, 0x8B,
                0xE5, 0x5D, 0xC2, 0xC3])


class Fixture:
    def __init__(self, objs, prefix="t"):
        self.dir = tempfile.TemporaryDirectory()
        self.lib = L.Library()
        for i, obj in enumerate(objs):
            p = Path(self.dir.name) / f"{prefix}{i}.obj"  # prefix "probe": a tools/stlprobe object
            p.write_bytes(obj)
            self.lib.add_path(p)
        self.image = bytearray(b"\xcc" * TEXT[1])
        self.funcs = []

    def put(self, rva, data, size=None):
        self.image[rva:rva + len(data)] = data
        if size is not None:
            self.funcs.append({"rva": hex(rva), "size": size})

    def run(self, known=None, crt=(), confirm_only=frozenset()):
        image = bytes(self.image)
        rows, conflicts = L.locate(self.lib, image, self.funcs, TEXT[0], TEXT[1], log=lambda *_: None)
        sizes = {int(f["rva"], 16): f["size"] for f in self.funcs}
        fs = sorted(sizes.items())
        lines = L.format_rows(self.lib, rows, sizes, sizes, fs)
        known_map = known_from(lines, crt)
        if known:
            for rva, names in known.items():
                known_map.setdefault(rva, (set(), set()))[0].update(names)
        L.analyse_relocs(rows, L.reloc_tables(self.lib, rows), image, BASE, TEXT, known_map, None, self.lib,
                         confirm_only)
        self.conflicts = conflicts
        return {m["rva"]: m for m in L.format_rows(self.lib, rows, sizes, sizes, fs)}

    def close(self):
        self.dir.cleanup()


def known_from(lines, crt):
    known = {}
    for m in lines:
        if m.get("kind") != "inside":
            known.setdefault(int(m["rva"], 16), (set(), set()))[0].update(m["names"])
    for rva, name in crt:
        known.setdefault(rva, (set(), set()))[1].add(name)
    return known


class Matching(unittest.TestCase):
    def setUp(self):
        self.fx = None

    def tearDown(self):
        if self.fx:
            self.fx.close()

    def test_exact(self):
        self.fx = Fixture([make_obj([("_f40", BODY40, [])])])
        self.fx.put(0x1100, BODY40, 40)
        rows = self.fx.run()
        self.assertEqual(rows["0x1100"]["names"], ["_f40"])
        self.assertNotIn("kind", rows["0x1100"])

    def test_prefix_and_inside(self):
        # Ghidra sized the function 10 bytes and made a second function 24 bytes in: the body is one library function.
        self.fx = Fixture([make_obj([("_f40", BODY40, [])])])
        self.fx.put(0x1100, BODY40, 10)
        self.fx.funcs.append({"rva": hex(0x1100 + 24), "size": 16})
        rows = self.fx.run()
        self.assertEqual(rows["0x1100"]["kind"], "prefix")
        self.assertEqual((rows["0x1100"]["size"], rows["0x1100"]["ghidra_size"]), (40, 10))
        piece = rows[hex(0x1100 + 24)]
        self.assertEqual((piece["kind"], piece["in"], piece["offset"], piece["names"]), ("inside", "0x1100", 24, []))
        self.assertEqual(piece["piece_of"], ["_f40"])

    def test_prefix_needs_whole_body(self):
        self.fx = Fixture([make_obj([("_f40", BODY40, [])])])
        broken = bytearray(BODY40)
        broken[37] ^= 0xFF  # past the db's size: a prefix match must still cover the whole body
        self.fx.put(0x1100, bytes(broken), 10)
        self.assertEqual(self.fx.run(), {})

    def test_unlisted_body_before_a_start(self):
        # No function starts where the body does; one starts 20 bytes in (a funclet Ghidra found on its own).
        self.fx = Fixture([make_obj([("_f40", BODY40, [])])])
        self.fx.put(0x1200, BODY40)
        self.fx.funcs.append({"rva": hex(0x1200 + 20), "size": 20})
        self.fx.funcs.append({"rva": hex(0x1100), "size": 0x200})  # the record that holds 0x1200
        rows = self.fx.run()
        self.assertEqual(rows["0x1200"]["kind"], "unlisted")
        self.assertEqual(rows["0x1200"]["container"], "0x1100")
        self.assertEqual(rows[hex(0x1200 + 20)]["in"], "0x1200")

    def test_short_prefix_must_be_unique(self):
        body = BODY40[:20] + b"\xc3"  # 21 bytes: under SHORT_PREFIX
        self.fx = Fixture([make_obj([("_f21", body, [])])])
        self.fx.put(0x1100, body + b"\x90" * 8, 29)
        self.assertIn("0x1100", self.fx.run())
        self.fx.put(0x1200, body + b"\x90" * 8, 29)  # a second place: no longer unique, both dropped
        self.assertEqual(self.fx.run(), {})

    def test_short_exact_rule_unchanged(self):
        body = bytes([0x8B, 0x44, 0x24, 0x04, 0x8B, 0x40, 0x08, 0xC3, 0x8B, 0xFF, 0xC3])  # 11 bytes
        self.fx = Fixture([make_obj([("_g", body, [])])])
        self.fx.put(0x1100, body, 11)
        self.assertIn("0x1100", self.fx.run())
        self.fx.put(0x1200, body, 11)
        self.assertEqual(self.fx.run(), {})


BODY40B = bytes(b ^ 0x5A if i not in (38, 39) else b for i, b in enumerate(BODY40))  # another 40-byte body


class Gaps(unittest.TestCase):
    """Lane IN: library bodies in code no db function covers (kind "gap"), and the stray-gap filter."""

    def setUp(self):
        self.fx = None
        self.neighbour = L.GAP_NEIGHBOUR

    def tearDown(self):
        L.GAP_NEIGHBOUR = self.neighbour
        L.THIRDPARTY_OF.clear()
        if self.fx:
            self.fx.close()

    def test_body_in_a_gap(self):
        self.fx = Fixture([make_obj([("_f40", BODY40, [])])])
        self.fx.put(0x1100, b"\x90" * 16, 16)  # a db function before, then int3 padding, then the body
        self.fx.put(0x1123, BODY40)  # off a 16-byte boundary: found because an int3 precedes it
        rows = self.fx.run()
        self.assertEqual((rows["0x1123"]["kind"], rows["0x1123"]["size"]), ("gap", 40))

    def test_body_overlapping_a_function_is_no_gap(self):
        self.fx = Fixture([make_obj([("_f40", BODY40, [])])])
        self.fx.put(0x1120, BODY40)
        self.fx.funcs.append({"rva": hex(0x1100), "size": 0x30})  # covers the body's first 16 bytes, no start inside
        self.assertNotIn("0x1120", self.fx.run())

    def test_stray_third_party_gap_dropped(self):
        self.fx = Fixture([make_obj([("_f40", BODY40, []), ("_g40", BODY40B, [])])])
        L.THIRDPARTY_OF[str(self.fx.lib.paths[0])] = "zlibx"
        self.fx.put(0x1100, BODY40, 40)  # the library's block: an exact row
        self.fx.put(0x1200, BODY40B)  # a gap row of the same library, near it
        self.assertEqual(self.fx.run()["0x1200"]["kind"], "gap")
        L.GAP_NEIGHBOUR = 0x80  # now too far from the block: game code of the same shape
        self.assertNotIn("0x1200", self.fx.run())

    def test_short_toolchain_body_beats_an_embedded_twin(self):
        body = bytes([0x8B, 0x44, 0x24, 0x04, 0x8B, 0x40, 0x08, 0xC3, 0x8B, 0xFF, 0xC3])  # 11 bytes
        self.fx = Fixture([make_obj([("_crt", body, [])]), make_obj([("_twin", body, [])])])
        L.THIRDPARTY_OF[str(self.fx.lib.paths[1])] = "d3dxx"
        self.fx.put(0x1100, body, 11)
        self.assertEqual(self.fx.run()["0x1100"]["names"], ["_crt"])


def caller_body(n_pad=24):
    """push 1; call <reloc at +3>; add esp, 4; then padding to n bytes; ret."""
    b = b"\x6a\x01\xe8\0\0\0\0\x83\xc4\x04" + bytes(range(0x50, 0x50 + n_pad)) + b"\xc3"
    return b


class Relocations(unittest.TestCase):
    def setUp(self):
        self.fx = None

    def tearDown(self):
        if self.fx:
            self.fx.close()

    def test_folded_caller_narrowed_by_callee(self):
        # strtol and wcstol compile to the same bytes and call parse<char> / parse<wchar_t>. The game's callee is a
        # byte match of parse<wchar_t> only, so the caller is wcstol.
        parse_c = BODY40
        parse_w = BODY40[:30] + bytes([0x66, 0x8B, 0x01, 0x66, 0x85, 0xC0, 0x75, 0xF0, 0x5D]) + b"\xc3"
        obj_c = make_obj([("_strtol", caller_body(), [(3, "_parse_c", L.REL32)]), ("_parse_c", parse_c, [])])
        obj_w = make_obj([("_wcstol", caller_body(), [(3, "_parse_w", L.REL32)]), ("_parse_w", parse_w, [])])
        self.fx = Fixture([obj_c, obj_w])
        body = bytearray(caller_body())
        body[2:7] = call(0x1100 + 2, 0x1200)
        self.fx.put(0x1100, bytes(body), len(body))
        self.fx.put(0x1200, parse_w, len(parse_w))
        rows = self.fx.run()
        self.assertEqual(rows["0x1100"]["names"], ["_strtol", "_wcstol"])
        self.assertEqual(rows["0x1100"]["names_by_relocs"], ["_wcstol"])
        self.assertNotIn("family", rows["0x1100"])
        self.assertNotIn("calls", rows["0x1100"])  # the only site is confirmed

    def test_family_label_records_targets(self):
        # vector<Blob4<28>>::_Tidy calls the stand-in element destructor; the game's call goes to game code.
        name = "?_Tidy@?$vector@U?$Blob4@$0BM@@@@@QAEXXZ"
        dtor = "??1?$Blob4@$0BM@@@QAE@XZ"
        self.fx = Fixture([make_obj([(name, caller_body(), [(3, dtor, L.REL32)])], externs=[dtor])])
        body = bytearray(caller_body())
        body[2:7] = call(0x1100 + 2, 0x1800)
        self.fx.put(0x1100, bytes(body), len(body))
        row = self.fx.run()["0x1100"]
        self.assertTrue(row["family"])
        self.assertEqual(row["calls"], [{"off": "0x3", "target": "0x1800", "symbols": [dtor]}])

    def test_conflicting_callee_makes_a_family(self):
        # The only candidate calls _known, but the game's target is library code under another name.
        self.fx = Fixture([make_obj([("_f", caller_body(), [(3, "_known", L.REL32)])], externs=["_known"])])
        body = bytearray(caller_body())
        body[2:7] = call(0x1100 + 2, 0x1800)
        self.fx.put(0x1100, bytes(body), len(body))
        row = self.fx.run(known={0x1800: {"_other"}})["0x1100"]
        self.assertTrue(row["family"])
        self.assertEqual(row["calls"][0]["target"], "0x1800")

    def test_one_name_several_bodies(self):
        # libcmt's __scrt_common_main_seh exists in several objects that differ only in the entry point they call.
        objs = [make_obj([("_main_seh", caller_body(), [(3, entry, L.REL32)])], externs=[entry])
                for entry in ("_WinMain", "_wWinMain")]
        self.fx = Fixture(objs)
        body = bytearray(caller_body())
        body[2:7] = call(0x1100 + 2, 0x1800)
        self.fx.put(0x1100, bytes(body), len(body))
        row = self.fx.run()["0x1100"]
        self.assertEqual(row["calls"], [{"off": "0x3", "target": "0x1800", "symbols": ["_WinMain", "_wWinMain"]}])
        row = self.fx.run(known={0x1800: {"_wWinMain"}})["0x1100"]
        self.assertNotIn("calls", row)
        self.assertNotIn("family", row)

    def test_folded_callee_agrees_by_its_bytes(self):
        # speex_free is `jmp free`, folded in the game with operator delete (also `jmp free`): the target is known
        # under the other name, but the symbol's own body is there.
        obj = make_obj([("_f", caller_body(), [(3, "_speex_free", L.REL32)]),
                        ("_speex_free", b"\xe9\0\0\0\0", [(1, "_free", L.REL32)])], externs=["_free"])
        self.fx = Fixture([obj])
        body = bytearray(caller_body())
        body[2:7] = call(0x1100 + 2, 0x1800)
        self.fx.put(0x1100, bytes(body), len(body))
        self.fx.put(0x1800, b"\xe9" + struct.pack("<i", 0x40))
        row = self.fx.run(known={0x1800: {"??3@YAXPAX@Z"}})["0x1100"]
        self.assertNotIn("family", row)
        self.fx.put(0x1800, b"\xeb\x40\x90\x90\x90")  # something else under that name: a conflict
        self.assertTrue(self.fx.run(known={0x1800: {"??3@YAXPAX@Z"}})["0x1100"]["family"])

    def test_crt_name_agrees(self):
        self.fx = Fixture([make_obj([("_f", caller_body(), [(3, "_free", L.REL32)])], externs=["_free"])])
        body = bytearray(caller_body())
        body[2:7] = call(0x1100 + 2, 0x1800)
        self.fx.put(0x1100, bytes(body), len(body))
        row = self.fx.run(crt=[(0x1800, "free")])["0x1100"]
        self.assertNotIn("family", row)
        self.assertNotIn("calls", row)

    def test_same_symbol_two_targets_conflicts(self):
        b = b"\xe8\0\0\0\0\xe8\0\0\0\0" + bytes(range(0x60, 0x70)) + b"\xc3"
        self.fx = Fixture([make_obj([("_f", b, [(1, "_g", L.REL32), (6, "_g", L.REL32)])], externs=["_g"])])
        body = bytearray(b)
        body[0:5] = call(0x1100, 0x1800)
        body[5:10] = call(0x1105, 0x1900)
        self.fx.put(0x1100, bytes(body), len(body))
        self.assertTrue(self.fx.run()["0x1100"]["family"])

    def test_external_symbol_inside_the_body_conflicts(self):
        self.fx = Fixture([make_obj([("_f", caller_body(), [(3, "_g", L.REL32)])], externs=["_g"])])
        body = bytearray(caller_body())
        body[2:7] = call(0x1100 + 2, 0x1100 + 12)  # "calls _g" into its own middle: not what the library does
        self.fx.put(0x1100, bytes(body), len(body))
        self.assertTrue(self.fx.run()["0x1100"]["family"])

    def test_local_label_checked(self):
        # mov eax, [table + ...] style DIR32 to the function's own section: the game must point at the same offset.
        b = b"\xb8\x10\0\0\0" + bytes(range(0x60, 0x70)) + b"\xc3"
        self.fx = Fixture([make_obj([("_f", b, [(1, "_f", L.DIR32)])])])
        body = bytearray(b)
        body[1:5] = struct.pack("<I", BASE + 0x1100 + 0x10)
        self.fx.put(0x1100, bytes(body), len(body))
        self.assertNotIn("family", self.fx.run()["0x1100"])
        body[1:5] = struct.pack("<I", BASE + 0x1100 + 0x14)
        self.fx.put(0x1100, bytes(body), len(body))
        self.assertTrue(self.fx.run()["0x1100"]["family"])

    def test_generic_initialiser_dropped_unless_confirmed(self):
        init = "??__Einitlocks@@YAXXZ"
        self.fx = Fixture([make_obj([(init, caller_body(), [(3, "??0_Init_locks@std@@QAE@XZ", L.REL32)])],
                                    externs=["??0_Init_locks@std@@QAE@XZ"])])
        body = bytearray(caller_body())
        body[2:7] = call(0x1100 + 2, 0x1800)
        self.fx.put(0x1100, bytes(body), len(body))
        self.assertEqual(self.fx.run(), {})  # the game's constructor is not the library's: some other initialiser
        self.assertIn("0x1100", self.fx.run(known={0x1800: {"??0_Init_locks@std@@QAE@XZ"}}))

    def test_probe_deleting_destructor_needs_its_destructor(self):
        # push 1; call dtor; call operator delete; padding; ret. A probe's deleting destructor has every class's shape.
        dtor, delete = "??1?$Blob4@$07@@QAE@XZ", "??3@YAXPAX@Z"
        b = b"\x6a\x01\xe8\0\0\0\0\xe8\0\0\0\0" + bytes(range(0x50, 0x68)) + b"\xc3"
        self.fx = Fixture([make_obj([("??_G?$Blob4@$07@@QAEPAXI@Z", b, [(3, dtor, L.REL32), (8, delete, L.REL32)])],
                                    externs=[dtor, delete])], prefix="probe")
        body = bytearray(b)
        body[2:7] = call(0x1100 + 2, 0x1800)
        body[7:12] = call(0x1100 + 7, 0x1900)
        self.fx.put(0x1100, bytes(body), len(body))
        self.assertEqual(self.fx.run(known={0x1900: {delete}}), {})  # a game class's destructor: not library code
        self.assertIn("0x1100", self.fx.run(known={0x1800: {dtor}, 0x1900: {delete}}))
        # only operator delete to check (an empty destructor): nothing tells it from a game class's
        self.fx.close()
        b = b"\x6a\x01\x90\x90\x90\x90\x90\xe8\0\0\0\0" + b[12:]
        self.fx = Fixture([make_obj([("??_G_Facet_base@std@@UAEPAXI@Z", b, [(8, delete, L.REL32)])],
                                    externs=[delete])], prefix="probe")
        body = bytearray(b)
        body[7:12] = call(0x1100 + 7, 0x1900)
        self.fx.put(0x1100, bytes(body), len(body))
        self.assertEqual(self.fx.run(known={0x1900: {delete}}), {})

    def test_short_unconfirmed_probe_wrapper_dropped(self):
        # A probe family label made mostly of calls (8 bytes of its own) is any wrapper's shape; with 16 it stays.
        for pad, kept in ((2, False), (10, True)):
            b = b"\x6a\x01\xe8\0\0\0\0\xe8\0\0\0\0" + bytes(range(0x50, 0x50 + pad)) + b"\x5e\xc3"
            self.fx = Fixture([make_obj([("?f@?$vector@U?$Blob4@$07@@@std@@QAEXXZ", b,
                                          [(3, "_g", L.REL32), (8, "_h", L.REL32)])], externs=["_g", "_h"])],
                              prefix="probe")
            body = bytearray(b)
            body[2:7] = call(0x1100 + 2, 0x1800)
            body[7:12] = call(0x1100 + 7, 0x1900)
            self.fx.put(0x1100, bytes(body), len(body))
            rows = self.fx.run()
            self.assertEqual("0x1100" in rows, kept)
            if kept:
                self.assertTrue(rows["0x1100"]["family"])
            else:  # unless it calls library code (another instantiation's row)
                self.assertIn("0x1100", self.fx.run(known={0x1900: {"?h@?$vector@H@std@@QAEXXZ"}}))
            self.fx.close()
        self.fx = None

    def test_short_single_instantiation_calling_something_else_dropped(self):
        # 17 bytes of its own, one name, and the game calls something other than the body we have for its callee
        # (basic_ostringstream::str's shape on scene-graph virtuals): dropped. Two instantiations sharing the shape
        # (their callees other element types' code), or one calling a function template (_Emplace_reallocate<T>:
        # the game's T may differ), stay a likely family.
        b = b"\x6a\x01\xe8\0\0\0\0" + bytes(range(0x50, 0x5c)) + b"\x5e\xc3"
        for types, inner_ in ((["D"], "?g@?$basic_stringbuf@{}@std@@QBEXXZ"), (["H", "I"], "?g@?$basic_stringbuf@{}@std@@QBEXXZ"),
                              (["D"], "??$g@{}@?$basic_stringbuf@D@std@@QBEXXZ")):
            funcs = []
            for t in types:
                inner = inner_.format(t)
                funcs += [(f"?str@?$basic_ostringstream@{t}@std@@QBEXXZ", b, [(3, inner, L.REL32)]), (inner, BODY40, [])]
            self.fx = Fixture([make_obj(funcs)], prefix="probe")
            body = bytearray(b)
            body[2:7] = call(0x1100 + 2, 0x1800)
            self.fx.put(0x1100, bytes(body), len(body))
            rows = self.fx.run()
            if len(types) == 1 and not inner.startswith("??$"):
                self.assertNotIn("0x1100", rows)
            else:
                self.assertTrue(rows["0x1100"]["family_likely"])
            self.fx.close()
        self.fx = None

    def test_static_initialiser_needs_confirmation(self):
        self.fx = Fixture([make_obj([("_init", caller_body(), [(3, "_ctor", L.REL32)])], externs=["_ctor"])])
        body = bytearray(caller_body())
        body[2:7] = call(0x1100 + 2, 0x1800)
        self.fx.put(0x1100, bytes(body), len(body))
        self.assertIn("0x1100", self.fx.run())
        self.assertEqual(self.fx.run(confirm_only={0x1100}), {})
        self.assertIn("0x1100", self.fx.run(known={0x1800: {"_ctor"}}, confirm_only={0x1100}))

    def test_template_with_unmatched_template_callee_is_likely_a_family(self):
        # _Emplace_one_at_back<T> calls _Emplace_reallocate<T>; we have that body, the game's target isn't it.
        outer = "??$_Emplace_one_at_back@H@?$vector@HV?$allocator@H@std@@@std@@AAEAAHABH@Z"
        inner = "??$_Emplace_reallocate@H@?$vector@HV?$allocator@H@std@@@std@@AAEPAHQAHABH@Z"
        self.fx = Fixture([make_obj([(outer, caller_body(), [(3, inner, L.REL32)]), (inner, BODY40, [])])])
        body = bytearray(caller_body())
        body[2:7] = call(0x1100 + 2, 0x1800)
        self.fx.put(0x1100, bytes(body), len(body))
        row = self.fx.run()["0x1100"]
        self.assertTrue(row["family_likely"])
        self.assertNotIn("family", row)
        self.assertEqual(row["calls"], [{"off": "0x3", "target": "0x1800", "symbols": [inner], "unmatched": True}])
        # The same callee present in the game: confirmed, nothing recorded.
        self.fx.put(0x1800, BODY40, 40)
        row = self.fx.run()["0x1100"]
        self.assertNotIn("family_likely", row)
        self.assertNotIn("calls", row)

    def test_site_target(self):
        r = L.Reloc(1, L.REL32, "_g", 0, None)
        image = b"\x90" * 0x10 + call(0x10, 0x50) + b"\0" * 0x60
        self.assertEqual(L.site_target(image, 0x10, r, BASE), 0x50)
        image = b"\xb8" + struct.pack("<I", BASE + 0x1234)
        self.assertEqual(L.site_target(image, 0, r._replace(type=L.DIR32), BASE), 0x1234)


class Coff(unittest.TestCase):
    def test_functions_past_section_32767(self):
        # Section numbers are unsigned: a big probe object (over 32767 COMDAT sections) keeps its upper functions.
        funcs = [(f"f{i}", b"\xc3", []) for i in range(32770)]
        names = [name for name, _, _ in L.coff_functions(make_obj(funcs))]
        self.assertEqual(len(names), 32770)
        self.assertIn("f32769", names)


class Exclusions(unittest.TestCase):
    def test_excluded_match_is_dropped_with_its_inside_rows(self):
        # An excluded rva loses its row and the "inside" rows under it; other rows, and rvas with no row, are untouched.
        rows = {
            0x10: {"kind": "exact", "entries": [], "length": 22},
            0x20: {"kind": "exact", "entries": [], "length": 22},
            0x14: {"kind": "inside", "in": 0x10, "offset": 4},
        }
        hit = L.apply_exclusions(rows, {0x10: "game code", 0x99: "no row there"})
        self.assertEqual(hit, [0x10])
        self.assertTrue(rows[0x10]["drop"])
        self.assertNotIn("drop", rows[0x20])
        self.assertNotIn("drop", rows[0x14])

    def test_format_rows_skips_dropped_rows_and_their_inside_rows(self):
        class FakeLib:
            paths = {"p": Path("a.lib")}
        rows = {
            0x10: {"kind": "exact", "entries": [("?f@@YAXXZ", "p", "o.obj")], "length": 22},
            0x20: {"kind": "exact", "entries": [("?g@@YAXXZ", "p", "o.obj")], "length": 22},
            0x14: {"kind": "inside", "in": 0x10, "offset": 4},
        }
        L.apply_exclusions(rows, {0x10: "game code"})
        out = L.format_rows(FakeLib(), rows, {}, {}, [(0x10, 22), (0x20, 22)])
        self.assertEqual([m["rva"] for m in out], ["0x20"])

    def test_format_rows_caps_the_inputs_listed(self):
        # A body in more inputs than LIB_CAP lists the first LIB_CAP (sorted: lib[0] is unchanged) and the count.
        n = L.LIB_CAP + 3
        class FakeLib:
            paths = {i: Path(f"probe_one{i:04d}.obj") for i in range(n)}
        rows = {0x10: {"kind": "exact", "entries": [("?f@@YAXXZ", i, "o.obj") for i in range(n)], "length": 22},
                0x20: {"kind": "exact", "entries": [("?g@@YAXXZ", 0, "o.obj")], "length": 22}}
        out = {m["rva"]: m for m in L.format_rows(FakeLib(), rows, {}, {}, [(0x10, 22), (0x20, 22)])}
        self.assertEqual(out["0x10"]["lib"], [f"probe_one{i:04d}.obj" for i in range(L.LIB_CAP)])
        self.assertEqual(out["0x10"]["lib_count"], n)
        self.assertEqual(out["0x20"]["lib"], ["probe_one0000.obj"])
        self.assertNotIn("lib_count", out["0x20"])

    def test_the_exclusion_file_is_well_formed(self):
        excluded = L.load_exclusions()
        for rva, reason in excluded.items():
            self.assertTrue(reason.strip(), hex(rva))


class StatusEntryList(unittest.TestCase):
    def test_lookalikes_are_not_listed(self):
        # A Concurrency Runtime name on game code of the same shape (COM director destructors, cHTMLLink) is not a
        # library match to settle; an STL family row, a named row and a row without a status entry are decided on.
        tdtor = "??1ThreadInternalContext@details@Concurrency@@UAE@XZ"
        lines = [
            {"rva": "0x10", "names": [tdtor], "family_likely": True},
            {"rva": "0x20", "names": ["??_GSegment@?$Mailbox@VInternalContextBase@details@Concurrency@@@details@Concurrency@@QAEPAXI@Z"],
             "family_likely": True},
            {"rva": "0x30", "names": ["?_Tidy@?$vector@HV?$allocator@H@std@@@std@@AAEXXZ"], "family": True},
            {"rva": "0x40", "names": ["?_Tidy@?$vector@HV?$allocator@H@std@@@std@@AAEXXZ"]},
            {"rva": "0x50", "names": ["?_Tidy@?$vector@HV?$allocator@H@std@@@std@@AAEXXZ"]},
        ]
        status = {r: {"status": "verified"} for r in ("0x10", "0x20", "0x30", "0x40")}
        self.assertEqual([m["rva"] for m in L.status_entry_matches(lines, status)], ["0x30", "0x40"])


class NearMisses(unittest.TestCase):
    """Lane WB: --near looks at every body whose own copy is unplaced (one member has several shapes, one per unit
    context), and --rvas limits it to listed db functions."""

    def setUp(self):
        shape2 = bytearray(BODY40)
        shape2[10] ^= 0x11  # the same function from another unit: a few bytes differ
        shape2[11] ^= 0x22
        self.shape2 = bytes(shape2)
        self.fx = Fixture([make_obj([("_f40", BODY40, [])]), make_obj([("_f40", self.shape2, [])])])
        self.fx.put(0x1100, BODY40, 40)  # the first shape, placed exactly
        game = bytearray(self.shape2)
        game[20] ^= 0x01  # the second shape, one byte off
        self.fx.put(0x1200, bytes(game), 40)
        self.fx.put(0x1300, BODY40B, 40)

    def tearDown(self):
        self.fx.close()

    def search(self, only=None):
        return L.near_misses(self.fx.lib, bytes(self.fx.image), self.fx.funcs, TEXT, 0, 1 << 32, only)

    def test_second_shape_of_a_placed_name(self):
        out = self.search()
        self.assertEqual([(rva, name, n, size) for rva, name, n, size, _, _ in out], [(0x1200, "_f40", 40, 40)])
        self.assertAlmostEqual(out[0][4], 39 / 40)
        self.assertEqual(out[0][5].name, "t1.obj")  # the input holding that shape, for --diff

    def test_rvas_limits_the_candidates(self):
        self.assertEqual(self.search(only={0x1300}), [])
        self.assertEqual([o[0] for o in self.search(only={0x1200, 0x1300})], [0x1200])


class AddObjects(unittest.TestCase):
    """--add-objects: only what the new objects match changes, placed as a full run would place it."""

    SIZES = {0x1000: 40, 0x2000: 40, 0x3000: 100}

    def merge(self, old, new, excluded=()):
        lines, report = L.merge_added(old, new, self.SIZES, excluded)
        return {m["rva"]: m for m in lines}, report

    def test_appends_and_leaves_the_rest_alone(self):
        keep = {"rva": "0x1000", "names": ["a"], "lib": ["l"], "objects": ["o"], "name": "a", "note": "by hand"}
        out, report = self.merge([keep], [{"rva": "0x2000", "names": ["b"], "lib": ["p.obj"], "objects": ["p.obj"]}])
        self.assertEqual(out["0x1000"], keep)
        self.assertEqual(report["added"], ["0x2000"])
        self.assertEqual(list(out), ["0x1000", "0x2000"])  # by address

    def test_extends_a_row_of_the_same_body(self):
        old = {"rva": "0x1000", "names": ["a"], "lib": ["z.obj"], "objects": ["z"], "note": "by hand", "name": "a"}
        out, report = self.merge([old], [{"rva": "0x1000", "names": ["b"], "lib": ["a.obj"], "objects": ["y"]}])
        self.assertEqual(report["extended"], ["0x1000"])
        m = out["0x1000"]
        self.assertEqual((m["names"], m["lib"], m["objects"]), (["a", "b"], ["a.obj", "z.obj"], ["y", "z"]))
        self.assertEqual((m["name"], m["note"]), ("a", "by hand"))
        self.assertNotIn("names_by_relocs", m)

    def test_lib_list_stays_capped_and_counted(self):
        libs = [f"p{i:03}.obj" for i in range(L.LIB_CAP)]
        old = {"rva": "0x1000", "names": ["a"], "lib": libs, "objects": ["o"], "lib_count": 500}
        out, _ = self.merge([old], [{"rva": "0x1000", "names": ["a"], "lib": ["q.obj"], "objects": ["o"]}])
        self.assertEqual(len(out["0x1000"]["lib"]), L.LIB_CAP)
        self.assertEqual(out["0x1000"]["lib_count"], 501)

    def test_confirmed_names_decide_a_family_row(self):
        old = {"rva": "0x1000", "names": ["Blob"], "lib": ["a"], "objects": ["o"], "family": True,
               "calls": [{"off": "0x1", "target": "0x9", "symbols": ["x"]}]}
        out, _ = self.merge([old], [{"rva": "0x1000", "names": ["real"], "lib": ["b"], "objects": ["o"]}])
        m = out["0x1000"]
        self.assertEqual((m["names"], m["names_by_relocs"]), (["Blob", "real"], ["real"]))
        self.assertNotIn("family", m)
        self.assertNotIn("calls", m)

    def test_a_family_match_does_not_unconfirm_a_row(self):
        old = {"rva": "0x1000", "names": ["real"], "lib": ["a"], "objects": ["o"]}
        out, _ = self.merge([old], [{"rva": "0x1000", "names": ["Blob"], "lib": ["b"], "objects": ["o"], "family": True}])
        m = out["0x1000"]
        self.assertEqual(m["names_by_relocs"], ["real"])
        self.assertNotIn("family", m)

    def test_exact_beats_non_exact(self):
        exact = {"rva": "0x1000", "names": ["e"], "lib": ["a"], "objects": ["o"]}
        prefix = {"rva": "0x1000", "names": ["p"], "lib": ["b"], "objects": ["o"], "kind": "prefix", "size": 30}
        out, report = self.merge([exact], [prefix])
        self.assertEqual(out["0x1000"], exact)
        self.assertEqual(report["skipped"], ["0x1000"])
        piece = {"rva": "0x1010", "names": [], "kind": "inside", "in": "0x1000", "offset": 16}
        out, report = self.merge([prefix, piece], [exact])
        self.assertEqual(out["0x1000"]["names"], ["e"])
        self.assertNotIn("0x1010", out)  # the piece of the row that went
        self.assertEqual(report["replaced"], ["0x1000"])

    def test_longer_non_exact_wins(self):
        short = {"rva": "0x1000", "names": ["s"], "lib": ["a"], "objects": ["o"], "kind": "gap", "size": 20}
        long = {"rva": "0x1000", "names": ["l"], "lib": ["b"], "objects": ["o"], "kind": "gap", "size": 60}
        self.assertEqual(self.merge([short], [long])[0]["0x1000"]["names"], ["l"])
        self.assertEqual(self.merge([long], [short])[0]["0x1000"]["names"], ["l"])

    def test_match_inside_an_accepted_one_is_dropped_with_its_piece(self):
        outer = {"rva": "0x3000", "names": ["o"], "lib": ["a"], "objects": ["o"]}  # db size 100
        inner = {"rva": "0x3010", "names": ["i"], "lib": ["b"], "objects": ["o"], "kind": "gap", "size": 32}
        piece = {"rva": "0x3018", "names": [], "kind": "inside", "in": "0x3010", "offset": 8, "piece_of": ["i"], "lib": ["b"]}
        out, report = self.merge([outer], [inner, piece])
        self.assertEqual(list(out), ["0x3000"])
        self.assertEqual(report["skipped"], ["0x3010"])

    def test_match_around_a_gap_row_replaces_it(self):
        inner = {"rva": "0x4010", "names": ["i"], "lib": ["b"], "objects": ["o"], "kind": "gap", "size": 16}
        outer = {"rva": "0x4000", "names": ["o"], "lib": ["a"], "objects": ["o"], "kind": "gap", "size": 64}
        out, report = self.merge([inner], [outer])
        self.assertEqual(list(out), ["0x4000"])
        self.assertIn("0x4010", report["replaced"])

    def test_exact_match_inside_a_longer_one_keeps_both(self):
        outer = {"rva": "0x1f00", "names": ["o"], "lib": ["a"], "objects": ["o"], "kind": "prefix", "size": 512}
        piece = {"rva": "0x2000", "names": [], "kind": "inside", "in": "0x1f00", "offset": 256}
        exact = {"rva": "0x2000", "names": ["e"], "lib": ["b"], "objects": ["o"]}
        out, _ = self.merge([outer, piece], [exact])
        self.assertEqual(out["0x2000"], exact)
        self.assertEqual(out["0x1f00"], outer)

    def test_pieces_follow_their_rows_names(self):
        outer = {"rva": "0x1000", "names": ["a"], "lib": ["l"], "objects": ["o"], "kind": "prefix", "size": 64}
        piece = {"rva": "0x1010", "names": [], "kind": "inside", "in": "0x1000", "offset": 16, "piece_of": ["a"], "lib": ["l"]}
        new = {"rva": "0x1000", "names": ["b"], "lib": ["m"], "objects": ["o"], "kind": "prefix", "size": 64}
        out, _ = self.merge([outer, piece], [new])
        self.assertEqual(out["0x1010"]["piece_of"], ["a", "b"])

    def test_exclusion_removes_a_row_and_its_pieces(self):
        row = {"rva": "0x1000", "names": ["a"], "lib": ["l"], "objects": ["o"], "kind": "prefix", "size": 64}
        piece = {"rva": "0x1010", "names": [], "kind": "inside", "in": "0x1000", "offset": 16}
        out, report = self.merge([row, piece], [], {0x1000: "game code"})
        self.assertEqual(out, {})
        self.assertEqual(report["excluded"], ["0x1000"])


class Probes(unittest.TestCase):
    def test_a_partial_directory_reads_the_rest_from_its_base(self):
        with tempfile.TemporaryDirectory() as tmp:
            base, own = Path(tmp) / "base", Path(tmp) / "own"
            base.mkdir()
            own.mkdir()
            for p in (base / "probe0.obj", base / "probe1.obj", own / "probe1.obj", own / "probe_new.obj"):
                p.write_bytes(b"")
            self.assertEqual(L.stlprobe_objects(own), [own / "probe1.obj", own / "probe_new.obj"])
            (own / "base.txt").write_text(f"{base}\n")
            self.assertEqual(L.stlprobe_objects(own), [base / "probe0.obj", own / "probe1.obj", own / "probe_new.obj"])

    def test_progress_line_per_thousand_objects(self):
        import contextlib
        import io
        lib, out = L.Library(), io.StringIO()
        with contextlib.redirect_stdout(out):
            for i in range(L.PROGRESS * 2 + 1):
                lib.add_object(0, "x.obj", make_obj([]))
        self.assertEqual(out.getvalue().count("indexed"), 2)


class AlignedBodies(unittest.TestCase):
    def test_body_before_a_start_whose_bytes_are_relocated(self):
        # The db has a start 2 bytes into the body (a Binary Ninja start), and the 8 bytes there hold a relocated
        # operand, so the window search can't see it. 0x86c10, 0xc4b600.
        body = bytes(range(0x20, 0x20 + 64))
        relocs = [(4, "_ext", L.REL32)]
        game = bytearray(body)
        game[4:8] = b"\x11\x22\x33\x44"
        fx = Fixture([make_obj([("_f", body, relocs)], externs=["_ext"])])
        fx.put(0x1200, bytes(game))
        fx.put(0x1202, b"", 62)
        rows = fx.run()
        fx.close()
        self.assertEqual(rows["0x1200"]["names"], ["_f"])
        self.assertEqual(rows["0x1200"]["kind"], "unlisted")
        self.assertEqual(rows["0x1202"]["kind"], "inside")


if __name__ == "__main__":
    unittest.main()
