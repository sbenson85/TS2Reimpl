"""Groups game functions whose code is byte-identical apart from relocated (absolute) addresses:
typically one template instantiated for many types (smart pointers, containers), or copies of a
helper. Writes db/families.jsonl: {"key", "size", "members": [rva...], "names": [...], "relocs": n}.

Careful: members can differ in what their relocated operands point at (e.g. which vtable a
constructor writes, or which exception-handler table it registers). `relocs` says how many such
operands the code has; a family with relocs == 0 is identical code, while others need a look at
what the relocated values are before one reimplementation can serve them all.

Usage: python -I tools/families.py [--min-size 12] [--show N]
"""
import argparse
import bisect
import hashlib
import json
from collections import defaultdict
from pathlib import Path

import pefile

ROOT = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--min-size", type=int, default=12)
    ap.add_argument("--show", type=int, default=0)
    args = ap.parse_args()

    pe = pefile.PE(str(ROOT / "local/original/Sims2EP9.exe.unpacked.exe"), fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_BASERELOC"]])
    image = pe.get_memory_mapped_image()
    relocs = sorted(r.rva for block in pe.DIRECTORY_ENTRY_BASERELOC for r in block.entries if r.type == 3)

    funcs = [json.loads(l) for l in open(ROOT / "db/functions.jsonl")]
    names = {n["rva"]: n["name"] for n in map(json.loads, open(ROOT / "db/names.jsonl"))}
    groups = defaultdict(list)
    reloc_count = {}
    for f in funcs:
        if f["size"] < args.min_size or f["thunk"]:
            continue
        start = int(f["rva"], 16)
        body = bytearray(image[start:start + f["size"]])
        n = 0
        for r in relocs[bisect.bisect_left(relocs, start - 3):bisect.bisect_left(relocs, start + f["size"])]:
            for i in range(max(r, start), min(r + 4, start + f["size"])):
                body[i - start] = 0
            n += 1
        key = hashlib.sha1(bytes(body)).hexdigest()[:16] + f"-{f['size']}"
        groups[key].append(f["rva"])
        reloc_count[key] = n

    families = [(k, v) for k, v in groups.items() if len(v) > 1]
    families.sort(key=lambda kv: -len(kv[1]))
    with open(ROOT / "db/families.jsonl", "w", newline="\n") as out:
        for key, members in families:
            out.write(json.dumps({"key": key, "size": int(key.split("-")[1]), "members": members,
                                  "names": sorted({names.get(m, "") for m in members if names.get(m)})[:5],
                                  "relocs": reloc_count[key]}) + "\n")
    covered = sum(len(v) for _, v in families)
    print(f"{len(families)} families covering {covered} functions (min size {args.min_size})")
    for key, members in families[: args.show]:
        print(f"  {len(members):4}x {key}  relocs {reloc_count[key]}  e.g. {members[0]} {names.get(members[0], '')[:50]}")


if __name__ == "__main__":
    main()
