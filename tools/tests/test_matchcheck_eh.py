#!/usr/bin/env python3
"""Tests for tools/matchcheck.py's C++ EH frame comparison (v5: handler stub, FuncInfo, funclets).

Synthetic only: a hand-assembled object (a function with a try/catch, an unwind funclet, a handler stub and its
tables) and a hand-assembled "original" image laid out differently. Each mutation changes one thing the
comparison must catch. No game bytes involved.

Run: .venv/Scripts/python -I tools/tests/test_matchcheck_eh.py
"""
import os
import struct
import sys
import unittest
from collections import defaultdict
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import matchcheck as M  # noqa: E402

BASE = 0x10000000
CODE, DATA = 0x60501020, 0x40301040  # section characteristics (COMDAT code, data)
# original RVAs
F, WORK, DTOR, OTHER, CXXFH = 0x1000, 0x2000, 0x2100, 0x2180, 0x2200
FUNCLET, STUB, FUNCINFO, UNWIND, TRY, HANDLERS, TD, TI_VFT = 0x3000, 0x3010, 0x4000, 0x4030, 0x4040, 0x4060, 0x5000, 0x5100

# The function: push ebp; mov ebp, esp; push -1; push <stub>; mov eax, fs:[0]; push eax; mov fs:[0], esp; push ecx;
# mov [ebp-4], 0; call work; mov [ebp-4], -1; L25 is the continuation the catch block returns.


def parent_code(cont_field, wide=False):
    c = bytearray()
    c += bytes.fromhex("558bec68ffffffff" if wide else "558bec6aff")  # 00 push ebp; mov ebp, esp; push -1
    c += b"\x68" + b"\0" * 4                       # 05 push <EH handler stub>        (abs field +6)
    c += bytes.fromhex("64a100000000")             # 0a mov eax, fs:[0]
    c += bytes.fromhex("50")                       # 10 push eax
    c += bytes.fromhex("64892500000000")           # 11 mov fs:[0], esp
    c += bytes.fromhex("51")                       # 18 push ecx
    c += bytes.fromhex("c745fc00000000")           # 19 mov [ebp-4], 0
    c += b"\xe8" + b"\0" * 4                       # 20 call work                     (rel32 field +0x21)
    c += bytes.fromhex("c745fcffffffff")           # 25 mov [ebp-4], -1               (continuation)
    c += bytes.fromhex("8b4df4")                   # 2c mov ecx, [ebp-0xc]
    c += bytes.fromhex("64890d00000000")           # 2f mov fs:[0], ecx
    c += bytes.fromhex("8be55dc3")                 # 36 mov esp, ebp; pop ebp; ret
    assert len(c) == 0x3a + 3 * wide
    c += b"\xb8" + struct.pack("<I", cont_field) + b"\xc3"  # 3a catch block: mov eax, <continuation>; ret
    return c


def build(**mut):
    """(checker, our object, our parent symbol index) with the original image built from the same description,
    changed by the mutations in mut (original side unless named ours_*)."""
    g = lambda k, d: mut.get(k, d)
    img = bytearray(0x6000)
    relocs = set()

    def put(rva, b):
        img[rva:rva + len(b)] = b

    def absptr(rva, target):
        if target is None:
            put(rva, b"\0\0\0\0")
            return
        put(rva, struct.pack("<I", BASE + target))
        relocs.add(rva)

    def rel(rva_field, target):
        put(rva_field, struct.pack("<i", target - (rva_field + 4)))

    # original: the function and its catch block right after it
    d = 3 if mut.get("wide") else 0  # the original encodes push -1 with an imm32: later offsets shift
    put(F, parent_code(0, bool(d)))
    absptr(F + 6 + d, g("stub", STUB))
    rel(F + 0x21 + d, WORK)
    absptr(F + 0x3b + d, F + g("cont", 0x25) + d)
    if "catch_body" in mut:
        put(F + 0x3f + d, mut["catch_body"])
    # unwind funclet: lea ecx, [ebp - 0x10]; jmp dtor
    put(FUNCLET, bytes([0x8d, 0x4d, g("funclet_disp", 0xf0), 0xe9]))
    rel(FUNCLET + 4, g("dtor", DTOR))
    # the handler stub: nop; nop; mov eax, <FuncInfo>; jmp __CxxFrameHandler3
    put(STUB, bytes.fromhex("9090b8"))
    absptr(STUB + 3, FUNCINFO)
    put(STUB + 7, b"\xe9")
    rel(STUB + 8, g("handler", CXXFH))
    # FuncInfo {magic, maxState, pUnwindMap, nTryBlocks, pTryBlockMap, nIPMapEntries, pIPtoStateMap, pESTypeList,
    # EHFlags}
    put(FUNCINFO, struct.pack("<II", g("magic", 0x19930522), g("max_state", 2)))
    absptr(FUNCINFO + 8, UNWIND)
    put(FUNCINFO + 12, struct.pack("<I", 1))
    absptr(FUNCINFO + 16, TRY)
    put(FUNCINFO + 20, struct.pack("<III", 0, 0, 0))
    put(FUNCINFO + 32, struct.pack("<I", g("flags", 1)))
    # unwind map: (-1, funclet), (to_state_1, null)
    put(UNWIND, struct.pack("<i", -1))
    absptr(UNWIND + 4, g("action", FUNCLET))
    put(UNWIND + 8, struct.pack("<i", g("to_state_1", -1)))
    absptr(UNWIND + 12, None)
    # try block {tryLow, tryHigh, catchHigh, nCatches, pHandlerArray}
    put(TRY, struct.pack("<4i", 0, g("try_high", 0), 1, 1))
    absptr(TRY + 16, HANDLERS)
    # handler {adjectives, pType, dispCatchObj, addressOfHandler}
    put(HANDLERS, struct.pack("<i", g("adjectives", 0)))
    absptr(HANDLERS + 4, g("type", TD))
    put(HANDLERS + 8, struct.pack("<i", 0))
    absptr(HANDLERS + 12, F + 0x3a + d)
    # TypeDescriptor {pVFTable, spare, name}
    absptr(TD, TI_VFT)
    put(TD + 8, g("type_name", b".H") + b"\0")
    orig = SimpleNamespace(base=BASE, relocs=relocs, cookie=None,
                           read=lambda rva, n: bytes(img[rva:rva + n]),
                           dword=lambda rva: struct.unpack_from("<I", img, rva)[0])

    # ours: sections and symbols as cl writes them
    syms = {}

    def sym(name, value, sec, cls=3, typ=0x20):
        syms[len(syms)] = (name, value, sec, cls, typ)
        return len(syms) - 1

    s_parent = sym("_F", 0, 0, 2)
    s_catch = sym("__catch$_F$0", 0x3a, 0)
    s_label = sym("$LN5", mut.get("ours_cont", 0x25), 0, 6, 0)
    s_funclet = sym("__unwindfunclet$_F$0", 0, 1)
    s_stub = sym("__ehhandler$_F", 8, 1)
    s_handlers = sym("__catchsym$_F$2", 0, 2, 3, 0)
    s_unwind = sym("__unwindtable$_F", 0x10, 2, 3, 0)
    s_try = sym("__tryblocktable$_F", 0x20, 2, 3, 0)
    s_funcinfo = sym("__ehfuncinfo$_F", 0x34, 2, 3, 0)
    s_td = sym("??_R0H@8", 0, 3, 2, 0)
    s_tivft = sym("??_7type_info@@6B@", 0, None, 2, 0)
    s_work = sym("_work", 0, None, 2)
    s_dtor = sym("_dtor", 0, None, 2)
    s_fh = sym("___CxxFrameHandler3", 0, None, 2)

    text = bytearray(parent_code(0))
    funclet = bytearray(bytes([0x8d, 0x4d, 0xf0, 0xe9, 0, 0, 0, 0]) + bytes.fromhex("9090b800000000e900000000"))
    xdata = bytearray(0x58)
    struct.pack_into("<i", xdata, 0x0, mut.get("ours_adjectives", 0))
    struct.pack_into("<4i", xdata, 0x10, -1, 0, -1, 0)
    struct.pack_into("<4i", xdata, 0x20, 0, 0, 1, 1)
    struct.pack_into("<II", xdata, 0x34, 0x19930522, 2)
    struct.pack_into("<I", xdata, 0x34 + 12, 1)
    struct.pack_into("<I", xdata, 0x34 + 32, 1)
    rdata = bytearray(8) + b".H\0"
    sections = [
        dict(name=".text$mn", raw=bytes(text), chars=CODE,
             relocs=[(6, s_stub, M.DIR32), (0x21, s_work, M.REL32), (0x3b, s_label, M.DIR32)]),
        dict(name=".text$x", raw=bytes(funclet), chars=CODE,
             relocs=[(4, s_dtor, M.REL32), (0xb, s_funcinfo, M.DIR32), (0x10, s_fh, M.REL32)]),
        dict(name=".xdata$x", raw=bytes(xdata), chars=DATA,
             relocs=[(0x4, s_td, M.DIR32), (0xc, s_catch, M.DIR32), (0x14, s_funclet, M.DIR32),
                     (0x34 + 8, s_unwind, M.DIR32), (0x34 + 16, s_try, M.DIR32), (0x30, s_handlers, M.DIR32)]),
        dict(name=".data$r", raw=bytes(rdata), chars=DATA, relocs=[(0, s_tivft, M.DIR32)]),
    ]
    obj = SimpleNamespace(path=Path("synthetic.obj"), sections=sections, symbols=syms)
    obj.functions = lambda: M.Obj.functions(obj)

    ck = M.Checker.__new__(M.Checker)
    ck.orig = orig
    ck.funcs = {hex(F): dict(rva=hex(F), size=0x3a + d, name="F")}
    ck.status, ck.hooks, ck.impl_rvas, ck.thunks = {}, [], defaultdict(set), {}
    ck.use_equiv, ck.use_callee_proof, ck._entry = True, False, {}
    ck._proofs, ck._proving, ck._used, ck._cod, ck._why, ck.proven = {}, [], [], {}, {}, {}
    ck._eh, ck._relmaps = [], {}
    ck.library = defaultdict(set, {hex(WORK): {"_work"}, hex(DTOR): {"_dtor"}, hex(CXXFH): {"___CxxFrameHandler3"},
                                   hex(OTHER): {"_other"}})
    return ck, obj


def check(**mut):
    ck, obj = build(**mut)
    return ck.compare(hex(F), obj, Path("missing.cod"), "_F")


class EHFrame(unittest.TestCase):
    def test_equal_frame_matches(self):
        r = check()
        self.assertTrue(r["match"], r["reasons"])
        self.assertEqual(r["equiv"], ["eh-frame"])

    def assertRejected(self, text, **mut):
        r = check(**mut)
        self.assertFalse(r["match"], mut)
        self.assertIn(text, " ".join(r["reasons"]))

    def test_funcinfo_fields(self):
        self.assertRejected("magic", magic=0x19930521)
        self.assertRejected("maxState", max_state=3)
        self.assertRejected("EHFlags", flags=5)

    def test_unwind_map(self):
        self.assertRejected("toState", to_state_1=0)
        self.assertRejected("one side null", action=None)
        self.assertRejected("bytes differ", funclet_disp=0xec)        # destroys another local
        self.assertRejected("_dtor", dtor=OTHER)                      # calls another destructor

    def test_try_block_map(self):
        self.assertRejected("try block 0", try_high=1)
        self.assertRejected("adjectives", adjectives=8)
        self.assertRejected("catch (...)", type=None)
        self.assertRejected("caught type", type_name=b".M")

    def test_catch_block(self):
        self.assertRejected("address in the function differs", cont=0x2c)   # continues elsewhere
        self.assertRejected("address in the function differs", ours_cont=0x2c)
        self.assertRejected("bytes differ", catch_body=b"\xc2")

    def test_continuation_through_the_encoding_equivalence(self):
        # The original's push -1 is 3 bytes longer, so the function matches by instruction (encoding) and the
        # catch block's continuation must map through the instruction index, not the byte offset.
        r = check(wide=True)
        self.assertTrue(r["match"], r["reasons"])
        self.assertEqual(r["equiv"], ["eh-frame", "encoding"])
        self.assertRejected("address in the function differs", wide=True, cont=0x2c)
        self.assertRejected("address in the function differs", wide=True, cont=0x22)  # same byte offset as ours

    def test_stub(self):
        self.assertRejected("___CxxFrameHandler3", handler=OTHER)
        self.assertRejected("EH frame", stub=FUNCLET)                  # pushes something else as the handler


if __name__ == "__main__":
    unittest.main()
