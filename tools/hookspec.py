"""Prints hook-table entries (src/hook/hooks.h HookSpec) for functions, with the site CRC, and
refuses functions that must not be hooked.

Usage: python -I tools/hookspec.py 0x1c2c6e [0x1c3bec ...] [--replacement 'MemberAddress(&cRZFile::Read)']
       [--into src/hook/tables/<Group>.cpp [--include rz/cRZFile.h]]
--into appends the entries to that table's kHooks array (creating the file, with --include as its
header, if it doesn't exist) instead of printing them; RVAs already in the table are skipped.
The CRC covers the first min(size, 32) bytes of the unpacked exe (on-disk bytes; the installer undoes
ASLR relocations before comparing). Without --replacement a placeholder is printed to fill in.
Refused: under 5 bytes, shared_vf_* (folded with unrelated classes), library code, thunks.
Noted: TS2-Extender hooks/patches inside the function (db/community-hooks.jsonl; facts only, not a
reason to skip), byte-identical
copies elsewhere (db/families.jsonl: each copy needs its own entry if it should be replaced too).
"""
import argparse
import re
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rvas", nargs="+")
    ap.add_argument("--replacement", default="/* replacement */ nullptr")
    ap.add_argument("--into", help="hook table file to append the entries to")
    ap.add_argument("--include", help="header for a new table file, e.g. rz/cRZFile.h")
    args = ap.parse_args()
    entries = []

    import pefile
    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    funcs = {f["rva"]: f for f in tsdb.functions()}
    names = {n["rva"]: n for n in tsdb.jsonl("names.jsonl")}
    annotated = {}
    for a in tsdb.jsonl("annotations.jsonl"):
        if a.get("kind") in ("function", "name") and a.get("name") and "rva" in a:
            n = a["name"]
            annotated[hex(int(a["rva"], 16))] = f"{a['class']}::{n}" if a.get("class") and "::" not in n else n
    library = tsdb.library_rvas()
    community = tsdb.jsonl("community-hooks.jsonl")
    families = {m: fam for fam in tsdb.jsonl("families.jsonl") for m in fam["members"]}
    ok = True
    for rva in args.rvas:
        rva = hex(int(rva, 16))
        f = funcs.get(rva)
        name = names.get(rva, {}).get("name") or (f or {}).get("name", rva)
        if (name.startswith(("FUN_", "0x")) or re.fullmatch(r"(\w+::)?vf\d+", name)) and rva in annotated:
            name = annotated[rva]  # a name a lane recorded (annotate) before db/names.jsonl was regenerated
        problems = []
        if not f:
            problems.append("not a function start in db/functions.jsonl (check the address)")
        elif f["size"] < 5:
            problems.append(f"only {f['size']} bytes: a 5-byte jump would overwrite the next function")
        if name.startswith("shared_vf_"):
            problems.append("shared_vf_*: folded with unrelated classes")
        if rva in library or names.get(rva, {}).get("source") in ("library", "ghidra-fid"):
            problems.append("library code: not reimplemented")
        if f and f.get("thunk"):
            problems.append("thunk")
        if problems:
            ok = False
            print(f"// REFUSED {rva} {name}: " + "; ".join(problems), file=sys.stderr)
            continue
        start, size = int(rva, 16), f["size"]
        for c in community:
            for m in c["matches"]:
                if start <= int(m, 16) < start + size:
                    print(f"// WARNING {rva} {name}: {c['mod']} {c['name']} touches {m} ({c['note']})", file=sys.stderr)
        fam = families.get(rva)
        if fam:
            print(f"// note {rva} {name}: {len(fam['members']) - 1} byte-identical copies (db/families.jsonl {fam['key']})", file=sys.stderr)
        n = min(size, 32)
        crc = zlib.crc32(pe.get_data(start, n)) & 0xFFFFFFFF
        key = re.sub(r"[^A-Za-z0-9]+", "_", name.replace("::", "_")).strip("_")
        entries.append((rva, f'    {{"{key}", {rva}, {n}, 0x{crc:08x}, {args.replacement}}},'))
    if args.into:
        insert(Path(args.into), entries, args.include)
    else:
        for _, line in entries:
            print(line)
    sys.exit(0 if ok else 1)


def insert(path, entries, include):
    """Appends entries to the kHooks array in a table file, skipping RVAs it already has."""
    if path.exists():
        text = path.read_text(encoding="utf-8")
    else:
        text = "\n".join(['#include "hook/hooks.h"'] + ([f'#include "{include}"'] if include else []) +
                         ["", "namespace {", "", "const HookSpec kHooks[] = {", "};",
                          "REGISTER_HOOKS(kHooks);", "", "} // namespace", ""])
    start = text.find("kHooks[] = {")
    end = text.find("\n};", start)
    if start < 0 or end < 0:
        sys.exit(f"{path}: no 'kHooks[] = {{ ... }};' array found")
    present = {int(m, 16) for m in re.findall(r'\{\s*"[^"]*",\s*(0x[0-9a-fA-F]+)', text[start:end])}
    new = [line for rva, line in entries if int(rva, 16) not in present]
    skipped = len(entries) - len(new)
    if new:
        text = text[:end] + "\n" + "\n".join(new) + text[end:]
        path.write_text(text, encoding="utf-8", newline="\n")
    print(f"{path}: added {len(new)} entries" + (f", skipped {skipped} already present" if skipped else ""))


if __name__ == "__main__":
    main()
