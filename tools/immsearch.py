"""Immediate-value search: the instructions whose operands contain a constant (an immediate, or a memory
displacement such as a field offset or an absolute address), each with its containing function, name, class
and status. Typical use: who uses a resource type id, a class id, a message id or a magic size.

    tools/py -I tools/immsearch.py 0x6c589723 [0x... ...] [--class PREFIX] [--imm-only | --disp-only] [--max N]

Values are matched as 32-bit numbers (negative ones too: -8 is 0xfffffff8). Addresses in the code are VAs (the
unpacked exe's image base 0xa0000000 + RVA); --rva adds the image base to every value, so `--rva 0x10cc328` finds the
code using the global at RVA 0x10cc328 (tools/xrefs.py lists data references too).
--class PREFIX keeps hits whose function name starts with PREFIX (as tools/candidates.py: cRZ, cTSPerson::).
Branch and call targets are not reported. A value that fits in a byte or a word is found by decoding every function,
which takes a minute or two; a wider value is found by a byte search first and decodes only the functions holding it.
Hits in code outside every known function (db/functions.jsonl and the merges of tools/tsdb.py) are listed as such,
undecoded.
"""
import argparse
import bisect
import collections
import struct
import sys
from pathlib import Path

import capstone
from capstone import x86
import pefile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402


def parse_value(text):
    v = int(text, 0)
    if not -0x80000000 <= v <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError(f"{text}: not a 32-bit value")
    return v & 0xFFFFFFFF


def fits_small(v):
    s = v - (1 << 32) if v & 0x80000000 else v
    return -0x8000 <= s <= 0xFFFF


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 epilog=__doc__.split("\n\n", 2)[2], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("values", nargs="+", type=parse_value, help="constants (0x hex or decimal, may be negative)")
    ap.add_argument("--class", dest="prefix", default="", help="only functions whose name starts with PREFIX")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--imm-only", action="store_true", help="immediate operands only")
    g.add_argument("--disp-only", action="store_true", help="memory displacements only")
    ap.add_argument("--max", type=int, default=200, help="hits printed per value (default 200)")
    ap.add_argument("--rva", action="store_true", help="the values are RVAs: search for image base + value")
    a = ap.parse_args()

    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    if a.rva:
        a.values = [(v + base) & 0xFFFFFFFF for v in a.values]
    code = [(s.VirtualAddress, s.get_data()) for s in pe.sections if s.Characteristics & 0x20000000]  # IMAGE_SCN_MEM_EXECUTE

    funcs = sorted(tsdb.functions(), key=lambda f: int(f["rva"], 16))
    starts = [int(f["rva"], 16) for f in funcs]
    names = {n["rva"]: n.get("name", "") for n in tsdb.jsonl("names.jsonl")}
    status = tsdb.load_status()

    def name_of(f):
        return status.get(f["rva"], {}).get("name") or names.get(f["rva"]) or f["name"]

    def containing(rva):
        i = bisect.bisect_right(starts, rva) - 1
        for j in range(i, max(i - 8, -1), -1):  # the nearest start, or an earlier record that overlaps it
            if starts[j] <= rva < starts[j] + max(funcs[j]["size"], 1):
                return funcs[j]
        return None

    def read(rva, size):
        for va, data in code:
            if va <= rva and rva + size <= va + len(data):
                return data[rva - va:rva - va + size]
        return b""

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    decoded = {}

    def instructions(f):
        if f["rva"] not in decoded:
            rva = int(f["rva"], 16)
            decoded[f["rva"]] = list(md.disasm(read(rva, f["size"]), base + rva))
        return decoded[f["rva"]]

    def matches(ins, v):
        if ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL):
            return []
        kinds = []
        for op in ins.operands:
            if op.type == x86.X86_OP_IMM and not a.disp_only and op.imm & 0xFFFFFFFF == v:
                kinds.append("imm")
            elif op.type == x86.X86_OP_MEM and not a.imm_only and op.mem.disp & 0xFFFFFFFF == v:
                kinds.append("disp")
        return kinds

    wanted = [f for f in funcs if name_of(f).startswith(a.prefix)] if a.prefix else funcs
    for v in a.values:
        hits, outside = [], set()
        if fits_small(v):
            candidates = wanted  # an imm8/imm16 encoding has no distinctive bytes: decode everything
        else:
            needle = struct.pack("<I", v)
            holders = {}
            for va, data in code:
                at = data.find(needle)
                while at >= 0:
                    f = containing(va + at)
                    if f is None:
                        outside.add(va + at)
                    else:
                        holders[f["rva"]] = f
                    at = data.find(needle, at + 1)
            candidates = [f for f in holders.values() if not a.prefix or name_of(f).startswith(a.prefix)]
        for f in candidates:
            for ins in instructions(f):
                kinds = matches(ins, v)
                if kinds:
                    hits.append((ins.address - base, f, ins, "+".join(kinds)))
        hits.sort(key=lambda h: h[0])
        per_fn = collections.Counter(h[1]["rva"] for h in hits)
        print(f"{v:#x}: {len(hits)} instruction(s) in {len(per_fn)} function(s)"
              + (f" matching '{a.prefix}'" if a.prefix else ""))
        for rva, f, ins, kind in hits[:a.max]:
            nm = name_of(f)
            cls = nm.split("::")[0] if "::" in nm else "-"
            st = status.get(f["rva"], {}).get("status", "")
            print(f"  {rva:#08x} {kind:<8} {ins.mnemonic} {ins.op_str:<40} in {f['rva']} [{cls}] {nm[:60]}"
                  + (f" [{st}]" if st else ""))
        if len(hits) > a.max:
            print(f"  ... {len(hits) - a.max} more (--max)")
        if outside and not a.prefix:
            print(f"  raw bytes outside known functions (undecoded): {', '.join(hex(r) for r in sorted(outside)[:20])}"
                  + (" ..." if len(outside) > 20 else ""))


if __name__ == "__main__":
    main()
