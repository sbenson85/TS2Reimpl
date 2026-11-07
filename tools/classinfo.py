"""Shows what's known about a class: bases (with subobject offsets), every vtable slot (function,
size, best name, status, callees) and non-virtual functions named after it.

Our side of it (headers, src, tests, fakes, docs): tools/whereis.py.

Usage: python -I tools/classinfo.py cRZCriticalSection [--todo]   (unqualified names work if only one class has it)
       python -I tools/classinfo.py [cRZCriticalSection] --disasm SLOT|RVA [SLOT|RVA ...]
  --todo    list only functions not done yet (done: verified, trace-verified, replaced, library)
  --disasm  disassemble slots (by number, primary vtable: needs the class) or functions (by RVA),
            without the class listing
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DONE = ("verified", "trace-verified", "replaced", "library")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cls", nargs="?")
    ap.add_argument("--disasm", nargs="+")
    ap.add_argument("--todo", action="store_true")
    args = ap.parse_args()
    if not args.cls and not args.disasm:
        ap.error("give a class, or --disasm with RVAs")

    classes = {c["name"]: c for c in map(json.loads, open(ROOT / "db/classes.jsonl"))}
    funcs = {f["rva"]: f for f in tsdb.functions()}
    names = {n["rva"]: n for n in map(json.loads, open(ROOT / "db/names.jsonl"))}
    status = {r: s["status"] for r, s in tsdb.load_status().items()}
    library = tsdb.library_rvas()

    def state(rva):
        return status.get(rva) or ("library" if rva in library else "")

    def describe(rva):
        f = funcs.get(rva, {})
        n = names.get(rva, {}).get("name", f.get("name", "?"))
        callees = [names.get(c, {}).get("name", c) if "!" not in c else c for c in f.get("callees", [])]
        return f"{rva:>9} {f.get('size', 0):5}B {state(rva):9} {n[:60]:60} -> {', '.join(x[:30] for x in callees[:4])}"

    c = None
    if args.cls:
        # An unqualified name (`cViewer_LEGACY`, a template's outer name) is enough when only one class has it.
        name, similar = tsdb.resolve_class(args.cls, classes)
        if not name:
            print("no single class; candidates:", similar[:20])
            return
        c = classes[name]

    if args.disasm:
        disasm(args.disasm, c, funcs, names, state)
        return

    prefix = c["name"] + "::"
    others = sorted(r for r, n in names.items() if n["name"].startswith(prefix)
                    and all(r not in vt["entries"] for vt in c["vftables"]))
    own = {e for vt in c["vftables"] for e in vt["entries"]} | set(others)
    counts = {}
    for r in own:
        counts[state(r) or "unmapped"] = counts.get(state(r) or "unmapped", 0) + 1
    todo = sum(v for k, v in counts.items() if k not in DONE)
    print(f"{c['name']}  (type descriptor {c['td']}): {len(own)} distinct functions, {todo} to do; "
          + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    for b, off in zip(c["bases"], c.get("base_offsets", [])):
        print(f"  base {b} at +{off}")
    for vt in c["vftables"]:
        print(f"vftable {vt['rva']} for subobject +{vt['offset']}: {len(vt['entries'])} slots")
        for i, e in enumerate(vt["entries"]):
            if not args.todo or state(e) not in DONE:
                print(f"  [{i:3}] {describe(e)}")
                # Folded slots (shared with other classes): the shape implements every one, so show the code inline.
                if names.get(e, {}).get("name", "").startswith("shared_vf_") and state(e) != "library":
                    print(f"          = {one_line(e, funcs)}")
    others = [r for r in others if not args.todo or state(r) not in DONE]
    if others:
        print("other functions named after this class:")
        for r in others:
            print("  " + describe(r))


_pe = []


def one_line(rva, funcs, limit=10):
    """A short function's disassembly on one line (`mov al, 1; ret`), up to `limit` instructions."""
    from capstone import CS_ARCH_X86, CS_MODE_32, Cs
    if not _pe:
        import pefile
        _pe.append(pefile.PE(str(tsdb.unpacked_exe()), fast_load=True))
    start, size = int(rva, 16), funcs.get(rva, {}).get("size", 64)
    out = []
    for ins in Cs(CS_ARCH_X86, CS_MODE_32).disasm(_pe[0].get_data(start, size), start):
        out.append(f"{ins.mnemonic} {ins.op_str}".strip())
        if len(out) == limit:
            return "; ".join(out) + f"; ... ({size} B: --disasm {rva})"
    return "; ".join(out)


def disasm(targets, c, funcs, names, state):
    import pefile
    from capstone import CS_ARCH_X86, CS_MODE_32, Cs
    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    status_names = {r: e["name"] for r, e in tsdb.load_status().items()}
    for t in targets:
        if t.startswith("0x"):
            rva = hex(int(t, 16))
        elif c:
            rva = c["vftables"][0]["entries"][int(t)]
        else:
            sys.exit(f"slot {t}: give the class to disassemble slots by number")
        start = int(rva, 16)
        size = funcs.get(rva, {}).get("size", 64)
        name = status_names.get(rva) or names.get(rva, {}).get("name", funcs.get(rva, {}).get("name", "?"))
        print(f"\ndisassembly of {rva} {name} ({size} bytes, {state(rva) or 'not done'}):")
        # Ghidra's size is sometimes short: past it, keep going until a ret or unconditional jmp.
        for ins in md.disasm(pe.get_data(start, size + 4096), start):
            end = ins.address + ins.size - start
            if ins.address - start == size:
                print("  ; --- past Ghidra's function size ---")
            target = ""
            if ins.mnemonic in ("call", "jmp") or ins.mnemonic.startswith("j"):
                if ins.op_str.startswith("0x"):  # addresses here are RVAs (disassembled at the RVA)
                    a = hex(int(ins.op_str, 16))
                    target = "   ; " + (status_names.get(a) or names.get(a, {}).get("name", ""))[:80]
            print(f"  {ins.address:#x}: {ins.mnemonic} {ins.op_str}{target}")
            if end >= size and ins.mnemonic in ("ret", "jmp", "int3"):
                break


if __name__ == "__main__":
    main()
