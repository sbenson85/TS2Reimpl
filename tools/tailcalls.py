"""Finds tail calls: jumps (jmp / jcc with an immediate target) from a function to the start of another
function. db/functions.jsonl's callees come from Ghidra's call references and leave these out, so a
function that tail-jumps to unfinished code looked ready to tools/candidates.py.

    .venv/Scripts/python -I tools/tailcalls.py     # writes db/tail-calls.jsonl ({rva, targets})

Facts only (call edges), like the rest of db/.
"""
import json
import sys
from pathlib import Path

import pefile
from capstone import CS_ARCH_X86, CS_GRP_JUMP, CS_MODE_32, Cs

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

BASE = 0xA0000000


def main():
    funcs = tsdb.jsonl("functions.jsonl")
    starts = {int(f["rva"], 16) for f in funcs}
    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    out = []
    for f in funcs:
        rva, size = int(f["rva"], 16), f["size"]
        if size <= 0:
            continue
        targets = set()
        for ins in md.disasm(pe.get_data(rva, size), BASE + rva):
            if not ins.group(CS_GRP_JUMP) or not ins.operands or ins.operands[0].type != 2:  # immediate only
                continue
            t = ins.operands[0].imm - BASE
            if (t < rva or t >= rva + size) and t in starts:
                targets.add(t)
        if targets:
            out.append({"rva": f["rva"], "targets": sorted(hex(t) for t in targets)})
    with open(tsdb.ROOT / "db/tail-calls.jsonl", "w", encoding="utf-8", newline="\n") as fh:
        for e in out:
            fh.write(json.dumps(e) + "\n")
    print(f"{len(out)} functions with tail calls")


if __name__ == "__main__":
    main()
