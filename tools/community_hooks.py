"""Locates the places community mods hook or patch in the pinned exe, from their published patterns.

Currently: TS2-Extender's Legacy Collection lookups (refs/TS2-Extender/include_lc/Lookups.h, used
via ADDRESS(name, lookup) in src/Addresses.cpp). Writes db/community-hooks.jsonl, one line per
named address: {mod, name, matches: [rva...], function: rva of the containing function,
at_start: whether the match is the function's entry point, note: the descriptive comment}.
Only names and addresses are recorded, no code.

Usage: python -I tools/community_hooks.py
"""
import bisect
import json
import re
from pathlib import Path

import pefile

ROOT = Path(__file__).resolve().parent.parent
EXT = ROOT / "refs/TS2-Extender"


def parse_lookups(text):
    """Returns {lookupName: (bytes, mask, note)}."""
    out = {}
    pending_comment = []
    pos = 0
    for m in re.finditer(r"(//[^\n]*)|static\s+char\s+(\w+)\[\]\s*=\s*(\{[^}]*\}|\"[^\"]*\")\s*;", text):
        if m.group(1):
            pending_comment.append(m.group(1)[2:].strip())
            continue
        name, value = m.group(2), m.group(3)
        if value.startswith("{"):
            body = re.sub(r"//[^\n]*", "", value[1:-1])
            nums = [int(x, 0) & 0xFF for x in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", body)]
            note = "; ".join(c for c in pending_comment if not re.match(r"(GHIDRA|CE|MAC):", c))
            out[name] = [bytes(nums), None, note]
        elif name.endswith("Mask") and name[:-4] in out:
            out[name[:-4]][1] = value[1:-1]
        pending_comment = []
    return out


def main():
    lookups = parse_lookups((EXT / "include_lc/Lookups.h").read_text(encoding="utf-8", errors="replace"))
    pairs = re.findall(r"ADDRESS\((\w+),\s*(\w+)\)", (EXT / "src/Addresses.cpp").read_text(encoding="utf-8", errors="replace"))

    pe = pefile.PE(str(ROOT / "local/original/Sims2EP9.exe.unpacked.exe"), fast_load=True)
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    code, base_rva = text.get_data(), text.VirtualAddress

    funcs = sorted((int(f["rva"], 16), f["size"]) for f in map(json.loads, open(ROOT / "db/functions.jsonl")))
    starts = [s for s, _ in funcs]

    out = []
    for name, lookup in pairs:
        if lookup not in lookups or lookups[lookup][1] is None:
            continue
        pattern, mask, note = lookups[lookup]
        regex = re.compile(b"".join(re.escape(bytes([b])) if c == "x" else b"." for b, c in zip(pattern, mask)), re.S)
        matches = [base_rva + m.start() for m in regex.finditer(code)]
        func = None
        at_start = False
        if len(matches) == 1:
            i = bisect.bisect_right(starts, matches[0]) - 1
            if i >= 0 and starts[i] <= matches[0] < starts[i] + funcs[i][1]:
                func = starts[i]
                at_start = func == matches[0]
        out.append({
            "mod": "TS2-Extender",
            "name": name,
            "matches": ["0x%x" % m for m in matches],
            "function": "0x%x" % func if func is not None else None,
            "at_start": at_start,
            "note": note,
        })

    with open(ROOT / "db/community-hooks.jsonl", "w", newline="\n") as f:
        for row in out:
            f.write(json.dumps(row) + "\n")
    unique = sum(len(r["matches"]) == 1 for r in out)
    print(f"{len(out)} named addresses, {unique} with a unique match, "
          f"{sum(r['at_start'] for r in out)} at a function start, "
          f"{sum(not r['matches'] for r in out)} not found")


if __name__ == "__main__":
    main()
