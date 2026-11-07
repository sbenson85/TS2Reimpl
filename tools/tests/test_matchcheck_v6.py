#!/usr/bin/env python3
"""Tests for tools/matchcheck.py v6: the three false-acceptance routes closed by lane BV.

 - constant data: compiler literals compare as whole objects from their start, internal relocations by rule 2;
   named data needs a ts2_rva symbol;
 - exact hook targets: the replacement is the symbol the kHooks entry relocates to (not a name lookup), every
   definition of it must match, ambiguous tables are refused;
 - build provenance: BuildLog says when a build object is not current.

Synthetic only (hand-assembled objects and a fake "original" image); no game bytes.

Run: .venv/Scripts/python -I tools/tests/test_matchcheck_v6.py
"""
import os
import struct
import sys
import tempfile
import time
import unittest
from collections import defaultdict
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import matchcheck as M  # noqa: E402

BASE = 0x10000000
CODE, RDATA, DATA = 0x60500020, 0x40300040, 0xC0300040  # without the COMDAT bit (0x1000)
COMDAT = M.IMAGE_SCN_LNK_COMDAT


def original(blobs, relocs=()):
    """A fake original image: {rva: bytes}, absolute pointer fields at the rvas in relocs (stored as BASE + value)."""
    img = bytearray(0x2000)
    for rva, b in blobs.items():
        img[rva:rva + len(b)] = b
    return SimpleNamespace(base=BASE, relocs=set(relocs), cookie=None,
                           read=lambda rva, n: bytes(img[rva:rva + n]),
                           dword=lambda rva: struct.unpack_from("<I", img, rva)[0])


def checker(orig, funcs=None):
    ck = M.Checker.__new__(M.Checker)
    ck.orig = orig
    ck.funcs = funcs or {}
    ck.status, ck.impl_rvas, ck.thunks = {}, defaultdict(set), {}
    ck.use_equiv, ck.use_callee_proof, ck._entry = False, False, {}
    ck._proofs, ck._proving, ck._used, ck._cod, ck._why, ck.proven = {}, [], [], {}, {}, {}
    ck._eh, ck._relmaps = [], {}
    ck.library = defaultdict(set)
    return ck


def objfile(sections, symbols, name="synthetic.obj"):
    """sections: [(name, raw, chars, relocs)]; symbols: [(name, value, section index or None, class, type)]."""
    obj = SimpleNamespace(path=Path(name), symbols=dict(enumerate(symbols)),
                          sections=[dict(name=n, raw=bytes(r), chars=c, relocs=list(rl)) for n, r, c, rl in sections])
    obj.functions = lambda: M.Obj.functions(obj)
    return obj


def ptr(v):
    return struct.pack("<I", BASE + v)


class ConstantData(unittest.TestCase):
    A = bytes(range(32))

    def lit32(self, orig_bytes):
        """Our 32-byte COMDAT literal, referenced at +0, against the original's bytes at 0x100."""
        obj = objfile([(".rdata", self.A, RDATA | COMDAT, [])], [("__xmm@a", 0, 0, 2, 0)])
        return checker(original({0x100: orig_bytes})).data_ref(obj, 0, 0, 0x100)

    def test_whole_literal_compared(self):
        self.assertIsNone(self.lit32(self.A))
        # equal in the first 4 bytes (all v5 compared of a constant with >16 bytes left), different after
        self.assertIn("contents differ", self.lit32(self.A[:4] + b"\xff" * 28))
        self.assertIn("contents differ", self.lit32(self.A[:31] + b"\xff"))

    def internal(self, orig_target, orig_reloc=True, our_target=("__real@b", RDATA | COMDAT)):
        """Our 8-byte literal {0, &other} with a relocation at +4; the original's at 0x200 points at orig_target."""
        name, chars = our_target
        b = bytes(range(100, 108))
        obj = objfile([(".rdata", b"\x11" * 4 + b"\0" * 4, RDATA | COMDAT, [(4, 1, M.DIR32)]),
                       (".rdata", b, chars, [])],
                      [("__xmm@a", 0, 0, 2, 0), (name, 0, 1, 2, 0)])
        orig = original({0x200: b"\x11" * 4 + ptr(orig_target), 0x300: b, 0x400: b"\xee" * 8},
                        relocs={0x204} if orig_reloc else ())
        return checker(orig).data_ref(obj, 0, 0, 0x200)

    def test_internal_relocation(self):
        self.assertIsNone(self.internal(0x300))
        self.assertIn("contents differ", self.internal(0x400))                    # points at other contents
        self.assertIn("relocated fields differ", self.internal(0x300, orig_reloc=False))
        self.assertIn("named data", self.internal(0x300, our_target=("?g@@3HA", DATA)))  # a pointer to a global

    def test_reference_into_the_middle(self):
        x, y = bytes(range(16)), bytes(range(50, 66))
        sec = (".rdata", x + y, RDATA, [])  # not COMDAT: two objects, each up to the next symbol
        obj = objfile([sec], [("__real@x", 0, 0, 3, 0), ("__real@y", 16, 0, 3, 0)])
        # ours: x + 8; the original's 0x508 has x's last 8 bytes, but what precedes them isn't x's first 8
        orig = original({0x500: b"\xaa" * 8 + x[8:], 0x600: x})
        self.assertIn("contents differ", checker(orig).data_ref(obj, 0, 8, 0x508))
        self.assertIsNone(checker(orig).data_ref(obj, 0, 8, 0x608))
        self.assertIn("outside the object", checker(orig).data_ref(obj, 0, 24, 0x618))
        # the last symbol of a section: no following symbol, so no extent
        self.assertIn("extent unknown", checker(original({0x600: y})).data_ref(obj, 1, 0, 0x600))
        # a symbol in the middle of a COMDAT section
        obj2 = objfile([(".rdata", x + y, RDATA | COMDAT, [])], [("__real@y", 16, 0, 3, 0)])
        self.assertIn("not at the start", checker(original({0x600: y})).data_ref(obj2, 0, 0, 0x600))

    def test_named_data_needs_an_rva_symbol(self):
        x = bytes(range(16))
        obj = objfile([(".rdata", x + x, RDATA, [])],
                      [("??_7Foo@@6B@", 0, 0, 2, 0), ("?kTable@@3QBHB", 16, 0, 2, 0), ("_ts2_rva_700", 0, None, 2, 0)])
        orig = original({0x700: x})
        # v7: a vtable is compared as one (test_matchcheck_v7.py); this one has no locator, so it is refused
        self.assertIn("locator", checker(orig).data_ref(obj, 0, 0, 0x700))
        self.assertIn("named data", checker(orig).data_ref(obj, 1, 0, 0x700))
        self.assertIsNone(checker(orig).data_ref(obj, 2, 0, 0x700))
        self.assertIn("vs original", checker(orig).data_ref(obj, 2, 4, 0x700))


F = 0x1000
ORIG_F = bytes.fromhex("8b442404c3")  # mov eax, [esp+4]; ret


def table(entries, name="table.obj"):
    """A compiled hook table: kHooks in .data, entries (rva, target symbol or None)."""
    raw = bytearray()
    relocs = []
    syms = [("?kHooks@?A0x1@@3QBUHookSpec@@B", 0, 0, 3, 0), ("??_C@_01hook@", 0, 1, 2, 0)]
    for i, (rva, target) in enumerate(entries):
        raw += struct.pack("<IIIII", 0, rva, 32, 0, 0)
        relocs.append((20 * i, 1, M.DIR32))
        if target:
            syms.append((target, 0, None, 2, 0x20))
            relocs.append((20 * i + 16, len(syms) - 1, M.DIR32))
    return objfile([(".data", raw, DATA, relocs), (".rdata", b"h\0", RDATA | COMDAT, [])], syms, name)


def tu(defs, name="tu.obj"):
    """An object defining functions {decorated name: code}, one COMDAT section each."""
    secs, syms = [], []
    for i, (sym, code) in enumerate(defs.items()):
        secs.append((".text$mn", code, CODE | COMDAT, []))
        syms.append((sym, 0, i, 2, 0x20))
    return objfile(secs, syms, name)


class HookTargets(unittest.TestCase):
    F_INT, F_DOUBLE = "?f@@YAXH@Z", "?f@@YAXN@Z"  # void f(int), void f(double): both undecorate to `f`

    def test_entries_resolve_by_relocation(self):
        targets, ambiguous, where = M.resolve_hooks({"t.cpp": table([(F, self.F_INT), (0x2000, self.F_DOUBLE)])})
        self.assertEqual(targets, {hex(F): self.F_INT, hex(0x2000): self.F_DOUBLE})
        self.assertEqual(ambiguous, {})
        self.assertEqual(M.hook_entries(table([(F, self.F_INT)]))[0], ("h", hex(F), self.F_INT))

    def test_ambiguous_tables_refused(self):
        _, amb, _ = M.resolve_hooks({"t.cpp": table([(F, None)])})
        self.assertIn("dynamic init", amb[hex(F)])
        _, amb, _ = M.resolve_hooks({"a.cpp": table([(F, self.F_INT)]), "b.cpp": table([(F, self.F_DOUBLE)])})
        self.assertIn("different replacements", amb[hex(F)])

    def test_overload_cannot_stand_in(self):
        # The hooked f(int) differs from the original; the overload f(double) has its bytes.
        obj = tu({self.F_INT: bytes.fromhex("8b442408c3"), self.F_DOUBLE: ORIG_F})
        targets, _, _ = M.resolve_hooks({"t.cpp": table([(F, self.F_INT)])})
        target = targets[hex(F)]
        cands = [(obj, None, s) for s in obj.functions() if s == target]
        ck = checker(original({F: ORIG_F}), {hex(F): dict(rva=hex(F), size=5, name="f")})
        r = ck.check(hex(F), cands)
        self.assertEqual(r["sym"], self.F_INT)
        self.assertFalse(r["match"])
        # control: the overload does match, so a name-based choice would have accepted it
        self.assertTrue(ck.check(hex(F), [(obj, None, self.F_DOUBLE)])["match"])

    def test_every_definition_must_match(self):
        a, b = tu({self.F_INT: ORIG_F}, "a.obj"), tu({self.F_INT: bytes.fromhex("8b442408c3")}, "b.obj")
        ck = checker(original({F: ORIG_F}), {hex(F): dict(rva=hex(F), size=5, name="f")})
        self.assertTrue(ck.check(hex(F), [(a, None, self.F_INT)])["match"])
        self.assertFalse(ck.check(hex(F), [(a, None, self.F_INT), (b, None, self.F_INT)])["match"])

    def test_calls_resolve_by_decorated_name(self):
        ck = checker(original({}), {})
        ck.set_hooks({hex(F): self.F_INT})
        self.assertTrue(ck.resolve_call(self.F_INT, F)[0])
        self.assertFalse(ck.resolve_call(self.F_DOUBLE, F)[0])


class NinjaProvenance(unittest.TestCase):
    """The same questions of a Ninja tree: the build file, the deps log and CMakeCache in place of MSBuild's logs."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.build = self.root / "build"
        (self.build / "CMakeFiles/ts2core.dir/Release/src").mkdir(parents=True)
        (self.root / "src").mkdir()
        self.src, self.hdr = self.root / "src/a.cpp", self.root / "src/a.h"
        self.obj = self.build / "CMakeFiles/ts2core.dir/Release/src/a.obj"
        escaped = str(self.src).replace(":", "$:").replace(" ", "$ ")
        project = self.build / "CMakeFiles/impl-Release.ninja"
        project.write_text(
            "build CMakeFiles\\ts2core.dir\\Release\\src\\a.obj: CXX_COMPILER__ts2core_unscanned_Release "
            f"{escaped} || cmake_object_order_depends_target_ts2core_RELEASE\n"
            '  DEFINES = -DNAME="<a.h>" -D_MBCS -DCMAKE_INTDIR=\\"Release\\"\n'
            "  FLAGS = /DWIN32 /EHsc /O2 -MT /MP /Oy /GS-\n"
            "  INCLUDES = -IC:\\x\\src\n\n")
        (self.build / "build.ninja").write_text("")
        (self.build / "CMakeCache.txt").write_text(
            "CMAKE_CXX_COMPILER:FILEPATH=C:/VS/VC/Tools/MSVC/14.40.33807/bin/Hostx64/x86/cl.exe\n"
            "CMAKE_MAKE_PROGRAM:FILEPATH=C:/VS/ninja.exe\n")
        files = (self.root / "CMakeLists.txt", project, self.src, self.hdr)
        for p in files[0::2] + (self.hdr,):
            p.write_text("x")
        past = time.time() - 100
        for p in files:
            os.utime(p, (past, past))
        self.state = self.build / ".ninja_log"
        self.state.write_text("x")
        os.utime(self.state, (past + 50, past + 50))
        self.obj.write_bytes(b"obj")
        self.deps = {"CMAKEFILES/TS2CORE.DIR/RELEASE/SRC/A.OBJ": [str(self.hdr)]}

    def tearDown(self):
        self.tmp.cleanup()

    def log(self):
        with mock.patch.object(M, "ninja_deps", return_value=self.deps):
            return M.BuildLog([self.root], here=self.root)

    def test_flags_from_the_build_file(self):
        self.assertEqual(self.log().unit("src/a.cpp")["flags"], [
            "/nologo", "/TP", "-DNAME=<a.h>", "-D_MBCS", "/DWIN32", "/EHsc", "/O2", "-MT", "/Oy", "/GS-"])

    def test_current(self):
        self.assertIsNone(self.log().stale("src/a.cpp", "14.40.33807"))

    def test_object_path_and_toolset(self):
        u = self.log().unit("src/a.cpp")
        self.assertEqual((u["obj"], u["toolset"], u["target"]), (self.build / "CMakeFiles\\ts2core.dir\\Release\\src\\a.obj",
                                                                 "14.40.33807", "ts2core"))

    def test_other_toolset(self):
        self.assertIn("toolset", self.log().stale("src/a.cpp", "14.44.1"))

    def test_header_newer_than_the_object(self):
        later = time.time() + 100
        os.utime(self.hdr, (later, later))
        self.assertIn("a.h", self.log().stale("src/a.cpp", "14.40.33807"))

    def test_configured_after_the_last_build(self):
        os.utime(self.state, (time.time() - 90, time.time() - 90))
        os.utime(self.root / "CMakeLists.txt")
        self.assertIn("rebuild", self.log().stale("src/a.cpp", "14.40.33807"))

    def test_no_deps_entry_is_not_current(self):
        self.deps = {}
        self.assertIn("read log", self.log().stale("src/a.cpp", "14.40.33807"))

    def test_only_system_headers_is_current(self):
        self.deps = {"CMAKEFILES/TS2CORE.DIR/RELEASE/SRC/A.OBJ": []}
        self.assertIsNone(self.log().stale("src/a.cpp", "14.40.33807"))

    def test_deps_output(self):
        run = mock.Mock(stdout="CMakeFiles/ts2core.dir/Release/src/a.obj: #deps 2, deps mtime 1 (VALID)\n"
                               "    ../src/a.h\n    generated/b.h\n\n")
        with mock.patch.object(M.subprocess, "run", return_value=run):
            self.assertEqual(M.ninja_deps(self.build, "ninja"), {"CMAKEFILES/TS2CORE.DIR/RELEASE/SRC/A.OBJ": [
                os.path.normpath(self.build / "../src/a.h"), os.path.normpath(self.build / "generated/b.h")]})


class Provenance(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        d = self.root / "build/ts2core.dir/Release"
        tl = d / "ts2core.tlog"
        tl.mkdir(parents=True)
        (self.root / "build/ts2hooktables.dir/Release/ts2hooktables.tlog").mkdir(parents=True)
        (self.root / "src").mkdir()
        (self.root / "inc").mkdir()
        self.src, self.hdr, self.obj = self.root / "src/a.cpp", self.root / "inc/a.h", d / "a.obj"
        S = str(self.src).upper()

        def log(name, lines):
            (tl / name).write_bytes("\r\n".join(lines).encode("utf-16"))
        log("CL.command.1.tlog", [f"^{S}", f"/c /I{self.root}\\SRC /nologo /O2 /Oy /GS- /D \"CMAKE_INTDIR=\\\"Release\\\"\" "
                                            f"/Fo\"X\\\\\" /EHsc {S}"])
        log("CL.read.1.tlog", [f"^{S}", str(self.hdr).upper()])
        log("CL.write.1.tlog", [f"^{S}", str(self.obj).upper()])
        (tl / "ts2core.lastbuildstate").write_text("PlatformToolSet=v143:VCToolsVersion=14.40.33807:\n")
        for p in (self.root / "CMakeLists.txt", self.root / "build/ts2core.vcxproj", self.src, self.hdr):
            p.write_text("x")
        past = time.time() - 100
        for p in (self.root / "CMakeLists.txt", self.root / "build/ts2core.vcxproj", self.src, self.hdr):
            os.utime(p, (past, past))
        self.obj.write_bytes(b"obj")
        os.utime(tl / "ts2core.lastbuildstate", (past + 50, past + 50))
        self.log = M.BuildLog([self.root], here=self.root)

    def tearDown(self):
        self.tmp.cleanup()

    def test_flags_from_the_command_log(self):
        self.assertEqual(self.log.unit("src/a.cpp")["flags"], ["/nologo", "/O2", "/Oy", "/GS-", "/EHsc"])

    def test_current(self):
        self.assertIsNone(self.log.stale("src/a.cpp", "14.40.33807"))

    def test_worktree_copy_differs(self):
        with tempfile.TemporaryDirectory() as other:
            (Path(other) / "src").mkdir()
            (Path(other) / "src/a.cpp").write_text("y")  # this checkout's source isn't what the build compiled
            self.assertIn("differs", M.BuildLog([self.root], here=Path(other)).stale("src/a.cpp", "14.40.33807"))
            (Path(other) / "src/a.cpp").write_text("x")
            self.assertIsNone(M.BuildLog([self.root], here=Path(other)).stale("src/a.cpp", "14.40.33807"))

    def test_stale(self):
        self.assertIn("toolset", self.log.stale("src/a.cpp", "14.41.0"))
        self.assertIn("not in the build", self.log.stale("src/b.cpp", "14.40.33807"))
        os.utime(self.hdr)  # a header changed after the object was built
        self.assertIn("A.H", self.log.stale("src/a.cpp", "14.40.33807").upper())
        os.utime(self.hdr, (time.time() - 100,) * 2)
        os.utime(self.root / "CMakeLists.txt")  # flags may have changed, not built since
        self.assertIn("CMakeLists", self.log.stale("src/a.cpp", "14.40.33807"))


if __name__ == "__main__":
    unittest.main()
