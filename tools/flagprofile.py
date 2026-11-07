"""Which compiler flag profile each original function was built with, from facts in the binary.

EA built the game with two profiles (survey 2026-10-16, notes/log.md):
  speed  /O2: functions start on 16-byte boundaries, padded with int3 between them
  size   /O1 /Oi: functions are packed with no padding, so most start unaligned
A function is "size" when it starts unaligned, otherwise "speed". (About 1 in 16 size-built functions starts aligned
by chance and is misread as speed; "aligned but packed against an unaligned neighbour" was tried as a size signal and
matched /O2 more often than /O1, 26 to 8.)

Our sources pick their profile by file name: `<name>.size.cpp` is built with the size flags (CMakeLists.txt and
matchcheck both apply them), every other file with the shipped speed flags. A file whose functions come from both
profiles is split in two. (`#pragma optimize("s"/"gsy", on)` per function does not reproduce /O1 code: tried on
cGZCOM::GetClassObject 2026-10-16.)

Usage: python -I tools/flagprofile.py [--rva RVA ...] [--class GROUP ...] [--sources] [--summary]
  --rva      the profile of these original functions
  --class    per status group: how many of its functions are each profile, and what its source files declare
  --sources  every source file with hooked functions: profile of its originals vs its name (mismatches only unless
             --all)
"""
import argparse
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PROFILE_FLAGS = {"speed": [], "size": ["/O1", "/Oi"]}  # appended to the shipped (speed) flags


def profiles():
    """{rva int: "speed" | "size"} for every known original function."""
    return {int(f["rva"], 16): "size" if int(f["rva"], 16) % 16 else "speed" for f in tsdb.functions() if f.get("size", 0) > 0}


def source_profile(path):
    """The profile a source file is built with, from its name."""
    return "size" if str(path).endswith(".size.cpp") else "speed"


def status_entries():
    for p in sorted((ROOT / "db/status").glob("*.jsonl")):
        for line in p.read_text(encoding="utf-8").splitlines():
            if line.strip():
                yield p.stem, json.loads(line)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rva", nargs="+")
    ap.add_argument("--class", dest="groups", nargs="+")
    ap.add_argument("--sources", action="store_true")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--summary", action="store_true")
    args = ap.parse_args()
    prof = profiles()
    if args.rva:
        for r in args.rva:
            print(r, prof.get(int(r, 16), "unknown"))
    if args.summary or not (args.rva or args.groups or args.sources):
        sizes = {int(f["rva"], 16): f.get("size", 0) for f in tsdb.functions()}
        c, b = collections.Counter(prof.values()), collections.Counter()
        for a, p in prof.items():
            b[p] += sizes.get(a, 0)
        print("functions:", dict(c), " bytes:", dict(b))
    if args.groups or args.sources:
        by_src = collections.defaultdict(collections.Counter)
        by_group = collections.defaultdict(collections.Counter)
        for g, e in status_entries():
            if e.get("status") not in ("reimplemented", "verified", "trace-verified"):
                continue
            p = prof.get(int(e["rva"], 16), "unknown")
            by_group[g][p] += 1
            for s in str(e.get("src", "")).split(","):
                if s.strip():
                    by_src[s.strip()][p] += 1
        if args.groups:
            for g in args.groups:
                print(f"{g}: {dict(by_group.get(g, {}))}")
        if args.sources:
            for s, c in sorted(by_src.items()):
                want = "size" if c["size"] > c["speed"] else "speed"
                have = source_profile(s)
                if args.all or want != have:
                    mixed = " (mixed)" if c["size"] and c["speed"] else ""
                    print(f"{s}: originals {dict(c)}{mixed}; built {have}" + ("" if want == have else f" -> {want}"))


if __name__ == "__main__":
    main()
