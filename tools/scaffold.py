#!/usr/bin/env python3
"""Generates the starting files for a class lane (review part 2, item 3): the boilerplate every lane
otherwise writes by hand. From db/ and the unpacked exe, for the class's own functions not yet done
(vtable slots named after the class, and other functions named <Class>::...):

  <out>/<Group>.h                     class skeleton: one declaration per function, with slot, RVA,
                                      size, and the calling convention read from its returns
  <out>/hook_<Group>.cpp              hook table (src/hook/tables/), entries with site CRCs; the
                                      replacement is a placeholder to fill in
  <out>/<Group>.jsonl                 db/status lines, status "mapped"
  <out>/test_<Group>.cpp              one DIFF_TEST skeleton per function

Files go to a scratch directory (default local/scaffold/<Group>/) for the lane to copy from and
fill in: nothing is written into src/, tests/ or db/. Shared slots (shared_vf_*), library code,
thunks and done functions are listed, not scaffolded. The bodies, layouts and tests are the lane's
work; this only saves the typing.

Usage: python -I tools/scaffold.py cTSBehaviorTree [--group Name] [--out DIR]
"""
import argparse
import json
import re
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402


def convention(img, md, start, size):
    """Calling convention from the function's returns and its use of ecx: ('__thiscall', stack bytes)."""
    rets = set()
    uses_ecx_first = None
    for ins in md.disasm(img[start:start + size], start):
        if uses_ecx_first is None:
            reads = ins.op_str.split(",")[1:] if "," in ins.op_str else [ins.op_str]
            if any("ecx" in r for r in reads) and not ins.op_str.startswith("ecx"):
                uses_ecx_first = True
            elif ins.op_str.startswith("ecx"):
                uses_ecx_first = False
        if ins.mnemonic == "ret":
            rets.add(int(ins.op_str, 16) if ins.op_str else 0)
    stack = max(rets) if rets else None
    if uses_ecx_first:
        return "__thiscall", stack
    return ("__stdcall" if stack else "__cdecl"), stack


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cls")
    ap.add_argument("--group")
    ap.add_argument("--out")
    args = ap.parse_args()

    import capstone
    import pefile
    classes = {c["name"]: c for c in tsdb.jsonl("classes.jsonl")}
    c = classes.get(args.cls)
    if not c:
        sys.exit(f"no class {args.cls}; similar: {[n for n in classes if args.cls.lower() in n.lower()][:10]}")
    group = args.group or re.sub(r"[^A-Za-z0-9]+", "", args.cls)
    out = Path(args.out) if args.out else tsdb.main_root() / "local" / "scaffold" / group
    out.mkdir(parents=True, exist_ok=True)

    funcs = {f["rva"]: f for f in tsdb.functions()}
    names = {n["rva"]: n["name"] for n in tsdb.jsonl("names.jsonl")}
    status = tsdb.load_status()
    library = tsdb.library_rvas()
    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    img = pe.get_memory_mapped_image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    prefix = args.cls + "::"
    picked, skipped = [], []
    seen = set()
    slots = [(vt["offset"], i, e) for vt in c["vftables"] for i, e in enumerate(vt["entries"])]
    others = sorted(r for r, n in names.items() if n.startswith(prefix))
    for sub, slot, rva in slots + [(None, None, r) for r in others]:
        if rva in seen:
            continue
        seen.add(rva)
        name = names.get(rva, funcs.get(rva, {}).get("name", rva))
        why = None
        if rva in status and status[rva]["status"] != "mapped":
            why = status[rva]["status"]
        elif rva in library:
            why = "library"
        elif name.startswith("shared_vf_"):
            why = "shared"
        elif not name.startswith(prefix):
            why = f"named {name}"
        elif rva not in funcs:
            why = "not a function start"
        elif funcs[rva].get("thunk"):
            why = "thunk"
        if why:
            skipped.append((sub, slot, rva, name, why))
            continue
        f = funcs[rva]
        start = int(rva, 16)
        cc, stack = convention(img, md, start, f["size"])
        if slot is not None:
            cc = "__thiscall"  # a virtual is a member function even when it ignores this
        n = min(f["size"], 32)
        crc = zlib.crc32(img[start:start + n]) & 0xFFFFFFFF
        method = name[len(prefix):]
        ident = re.sub(r"[^A-Za-z0-9]+", "_", method).strip("_") or "Fn"
        picked.append(dict(sub=sub, slot=slot, rva=rva, size=f["size"], name=name, ident=ident, cc=cc,
                           stack=stack, crc=crc, n=n, hookable=f["size"] >= 5))

    def where(p):
        return f"slot {p['slot']} (+{p['sub']})" if p["slot"] is not None else "non-virtual"

    def args_of(p):
        k = (p["stack"] or 0) // 4
        return ", ".join(f"uint32_t a{i}" for i in range(k))

    h = [f"// Scaffold for {args.cls} (tools/scaffold.py): declarations to fill in; check each convention.",
         "#pragma once", "#include <cstdint>", "", f"class {args.cls} {{", "public:"]
    for p in picked:
        ret = f"ret {p['stack']:#x}" if p["stack"] is not None else "no ret found"
        h.append(f"    // {p['rva']} {p['size']} bytes, {where(p)}, {p['cc']}, {ret}")
        conv = "" if p["cc"] == "__thiscall" else f"static {'' if p['cc'] == '__cdecl' else '__stdcall '}"
        h.append(f"    {conv}uint32_t {p['ident']}({args_of(p)});")
    h += ["};", ""]
    (out / f"{group}.h").write_text("\n".join(h), encoding="utf-8")

    hk = ['#include "hook/hooks.h"', f'#include "{group}.h"', "", "namespace {", "", "const HookSpec kHooks[] = {"]
    for p in picked:
        if not p["hookable"]:
            hk.append(f"    // {p['rva']} {p['ident']}: {p['size']} bytes, not hookable")
            continue
        repl = (f"MemberAddress(&{args.cls}::{p['ident']})" if p["cc"] == "__thiscall"
                else f"reinterpret_cast<void *>(&{args.cls}::{p['ident']})")
        hk.append(f'    {{"{args.cls}_{p["ident"]}", {p["rva"]}, {p["n"]}, 0x{p["crc"]:08x}, {repl}}},')
    hk += ["};", "REGISTER_HOOKS(kHooks);", "", "} // namespace", ""]
    (out / f"hook_{group}.cpp").write_text("\n".join(hk), encoding="utf-8")

    st = [json.dumps({"rva": p["rva"], "name": p["name"], "status": "mapped", "src": f"src/.../{group}.cpp",
                      "test": f"tests/{group}.cpp", "notes": ""}) for p in picked]
    (out / f"{group}.jsonl").write_text("\n".join(st) + "\n", encoding="utf-8")

    t = ['#include "difftest.h"', f'#include "{group}.h"', ""]
    for p in picked:
        t += [f"// {p['rva']} {p['name']}: {p['cc']}, {(p['stack'] or 0) // 4} stack args",
              f"DIFF_TEST({args.cls}_{p['ident']}, {p['rva']})", "{",
              "    // Build inputs (objects with recording vtable copies for the slots it calls), run",
              f"    // ctx.Original<...>({p['rva']}) and ours on the same inputs, compare results and side effects.",
              "}", ""]
    (out / f"test_{group}.cpp").write_text("\n".join(t), encoding="utf-8")

    print(f"{len(picked)} functions scaffolded into {out}")
    for p in picked:
        print(f"  {p['rva']:>9} {p['size']:6} {where(p):18} {p['cc']:10} ret {p['stack']}  {p['name']}")
    if skipped:
        print(f"{len(skipped)} skipped:")
        for sub, slot, rva, name, why in skipped:
            print(f"  {rva:>9} slot {slot} {why:14} {name[:60]}")


if __name__ == "__main__":
    sys.exit(main())
