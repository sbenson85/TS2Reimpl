"""Shows a function's C++ exception-handling setup: the FuncInfo it registers, its flags (EHs:
catch(...) catches only C++ exceptions; without it, /EHa: SEH too), try blocks and catch types.

Usage: python -I tools/ehinfo.py 0x1cbd79 [...]
"""
import struct
import sys
from pathlib import Path

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

BASE = 0xA0000000


def main():
    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    funcs = {f["rva"]: f for f in tsdb.jsonl("functions.jsonl")}
    for arg in sys.argv[1:]:
        rva = int(arg, 16)
        size = max(funcs.get(hex(rva), {}).get("size", 64), 64)
        handler = None
        insns = list(md.disasm(pe.get_data(rva, size), BASE + rva))
        # The handler is the last `push 0xa...` before `mov fs:[0], esp` (the read of fs:[0] may come
        # before or after the pushes).
        last_push = None
        for ins in insns:
            if ins.mnemonic == "push" and ins.op_str.startswith("0xa"):
                last_push = int(ins.op_str, 16)
            if ins.mnemonic == "mov" and ins.op_str.startswith("dword ptr fs:[0], esp") and last_push is not None:
                handler = last_push
                break
        for a, b in zip(insns, insns[1:]):
            if handler is not None:
                break
            if a.mnemonic == "mov" and a.op_str.startswith("eax, 0xa") and b.mnemonic == "call" and "0x" in b.op_str:
                handler = None  # _EH_prolog: eax holds the handler
                h = int(a.op_str.split(", ")[1], 16)
                handler = h
                break
        if handler is None:
            print(f"{arg}: no EH frame found in the first {size} bytes")
            continue
        stub = list(md.disasm(pe.get_data(handler - BASE, 16), handler))
        fi = next((int(i.op_str.split(", ")[1], 16) for i in stub if i.mnemonic == "mov" and i.op_str.startswith("eax, 0x")), None)
        if fi is None:
            print(f"{arg}: handler {handler:#x} doesn't load a FuncInfo")
            continue
        magic, max_state, unwind, n_try, try_map, _, _, _, flags = struct.unpack("<9I", pe.get_data(fi - BASE, 36))
        mode = "EHs (catch(...) is C++ only)" if flags & 1 else "EHa (catch(...) also catches SEH)"
        if flags & 4:
            mode += "; noexcept (a C++ exception leaving this frame terminates: declare the replacement noexcept)"
        print(f"{arg}: FuncInfo {fi - BASE:#x} magic {magic:#x} states {max_state} try blocks {n_try} flags {flags} -> {mode}")
        for t in range(n_try):
            lo, hi, catch_hi, n_catch, handlers = struct.unpack("<5I", pe.get_data(try_map - BASE + 20 * t, 20))
            for c in range(n_catch):
                adj, ptype, disp, funclet = struct.unpack("<4I", pe.get_data(handlers - BASE + 16 * c, 16))
                what = "catch(...)" if adj & 0x40 or ptype == 0 else f"catch(type {ptype - BASE:#x})"
                print(f"    try states {lo}-{hi}: {what}, funclet {funclet - BASE:#x}")


if __name__ == "__main__":
    main()
