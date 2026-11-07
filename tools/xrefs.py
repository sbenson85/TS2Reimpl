"""Lists references to addresses in the game image: direct calls and jumps (from db/functions.jsonl
and db/tail-calls.jsonl) and absolute references (relocated dwords: immediates and memory operands
in code, pointers in data). Typical uses: a vtable's references are its constructors and
destructors; a function's data references are the vtables that hold it; a static object's
references show who builds and uses it.

    .venv/Scripts/python -I tools/xrefs.py 0xd9d0c4 [0x15b840 ...]

Each reference is printed with the containing function (name and status) or the section it's in.
"""
import bisect
import json
import sys
from collections import defaultdict
from pathlib import Path

import pefile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402


def main():
    if len(sys.argv) < 2 or any(a in ("-h", "--help") for a in sys.argv[1:]):
        print(__doc__)
        return
    try:
        targets = [int(a, 16) for a in sys.argv[1:]]
    except ValueError as e:
        sys.exit(f"xrefs: give hex addresses (RVAs): {e}\n\n{__doc__}")
    funcs = tsdb.functions()
    names = {n["rva"]: n.get("name", "") for n in tsdb.jsonl("names.jsonl")}
    status = tsdb.load_status()
    starts = sorted(int(f["rva"], 16) for f in funcs)
    by_rva = {int(f["rva"], 16): f for f in funcs}

    def describe(rva):
        i = bisect.bisect_right(starts, rva) - 1
        if i >= 0:
            f = by_rva[starts[i]]
            if rva < starts[i] + max(f["size"], 1):
                key = f["rva"]
                st = status.get(key, {}).get("status", "")
                name = status.get(key, {}).get("name") or names.get(key) or f["name"]
                return f"in {key} {name[:70]}" + (f" [{st}]" if st else "")
        for s in pe.sections:
            if s.VirtualAddress <= rva < s.VirtualAddress + s.Misc_VirtualSize:
                section = s.Name.rstrip(b"\0").decode()
                return f"in {section} (data)"
        return "outside sections"

    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_BASERELOC"]])
    base = pe.OPTIONAL_HEADER.ImageBase
    wanted = {base + t: t for t in targets}
    absolute = defaultdict(list)
    for block in pe.DIRECTORY_ENTRY_BASERELOC:
        for e in block.entries:
            if e.type != 3:  # HIGHLOW
                continue
            value = pe.get_dword_at_rva(e.rva)
            if value in wanted:
                absolute[wanted[value]].append(e.rva)

    callers = defaultdict(set)
    for f in funcs:
        for c in f["callees"]:
            if not c.startswith("0x"):
                continue
            callers[int(c, 16)].add(f["rva"])
    for e in tsdb.jsonl("tail-calls.jsonl"):
        for t in e["targets"]:
            callers[int(t, 16)].add(e["rva"] + " (tail)")

    for t in targets:
        print(f"{t:#x}: {describe(t)}")
        for c in sorted(callers.get(t, ())):
            print(f"  called from {c:>16}  {describe(int(c.split()[0], 16))}")
        for r in sorted(absolute.get(t, ())):
            print(f"  referenced at {r:#x}  {describe(r)}")
        if not callers.get(t) and not absolute.get(t):
            print("  no references found")


if __name__ == "__main__":
    main()
