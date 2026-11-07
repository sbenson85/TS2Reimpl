"""Ranks functions to reimplement next, bottom-up through the call graph.

A function is "ready" when everything it calls is already verified, is library code
(db/library-matches.jsonl) or is a Windows import the harness passes through, so it can get a
differential test right away. Excluded: library functions (byte-matched, or named by Ghidra FID),
functions already in db/status/,
thunks, folded trivial functions (shared_vf_*) and functions under 5 bytes (can't be hooked).
Caveat: only direct calls are known; virtual/indirect calls can reach anything, which the harness
reveals at run time (an import trap or a crash).
Ready functions are ordered by caller count (how much of the game they affect), named ones first.

Usage: python -I tools/candidates.py [--class PREFIX] [--all] [-n 40] [--originals [--by-size]]
  --class      only functions whose name starts with PREFIX (e.g. cRZ, cTSPerson::)
  --all        also list not-yet-ready functions, with what blocks them
  --originals  callees that aren't done don't block: the replacement calls them through
               OriginalFunction (CLAUDE.md "Original callees"), so only imports block. Shows how many
               original callees each would need. The harness still decides: a callee that reaches an
               import or uninitialised state fails the test, and then it has to be replaced first.
  --by-size    rank ready functions by size (largest first) instead of caller count
"""
import argparse
import json
from collections import Counter, defaultdict
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PASSTHROUGH_IMPORTS = {"HeapAlloc", "HeapFree", "HeapReAlloc", "HeapSize", "GetProcessHeap"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--class", dest="prefix", default="")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("-n", type=int, default=40)
    ap.add_argument("--originals", action="store_true")
    ap.add_argument("--by-size", action="store_true")
    ap.add_argument("--blockers", action="store_true",
                    help="rank unfinished functions by how many others they're the last blocker of")
    args = ap.parse_args()

    funcs = {f["rva"]: f for f in tsdb.functions()}
    # Tail calls (jumps to another function) are callees too: Ghidra's call references leave them out.
    tail = ROOT / "db/tail-calls.jsonl"
    if tail.exists():
        for e in map(json.loads, open(tail)):
            if e["rva"] in funcs:
                funcs[e["rva"]] = dict(funcs[e["rva"]], callees=list(funcs[e["rva"]]["callees"]) + e["targets"])
    names = {n["rva"]: n for n in map(json.loads, open(ROOT / "db/names.jsonl"))}
    library = tsdb.library_rvas()
    status = tsdb.load_status()
    listed = set(status)  # anything with a status is someone's work already
    # Callees count as done when verified or library code, or a jump stub to one.
    done = tsdb.resolve_done({rva for rva, e in status.items() if e["status"] == "verified"} | library, funcs)
    callers = Counter(c for f in funcs.values() for c in set(f["callees"]))

    rows = []
    for rva, f in funcs.items():
        name = names.get(rva, {}).get("name", f["name"])
        if rva in library or rva in listed or rva in done or f["thunk"] or f["size"] < 5 or name.startswith("shared_vf_"):
            continue
        if args.prefix and not name.startswith(args.prefix):
            continue
        blockers, originals = [], []
        for c in f["callees"]:
            if "!" in c:
                if c.split("!")[1] not in PASSTHROUGH_IMPORTS:
                    blockers.append(c)
            elif c not in library and c not in done and c != rva:
                (originals if args.originals else blockers).append(
                    c if args.blockers else names.get(c, {}).get("name", c))
        named = not name.startswith(("FUN_", "thunk_"))
        rows.append((len(blockers) == 0, named, callers[rva], rva, name, f["size"], blockers, originals))

    if args.blockers:
        # A function blocked only by X becomes ready once X is done.
        sole = Counter(r[6][0] for r in rows if len(r[6]) == 1 and "!" not in r[6][0])
        two = Counter(b for r in rows if len(r[6]) == 2 for b in r[6] if "!" not in b)
        print("unfinished functions ranked by how many functions they alone block (+ one of two)")
        for b, n in sorted(sole.items(), key=lambda kv: (-kv[1], -two[kv[0]]))[: args.n]:
            f = funcs.get(b, {})
            name = names.get(b, {}).get("name", f.get("name", b))
            print(f"  {b:>9}  {f.get('size', 0):5}B  unlocks {n:4} (+{two[b]:3})  {callers[b]:5} callers  {name[:70]}")
        return

    order = (lambda r: -r[5]) if args.by_size else (lambda r: (not r[1], -r[2]))
    ready = sorted((r for r in rows if r[0]), key=order)
    print(f"{len(ready)} ready of {len(rows)} remaining" + (f" matching '{args.prefix}'" if args.prefix else "")
          + (" (original callees allowed)" if args.originals else ""))
    for _, _, ncall, rva, name, size, _, originals in ready[: args.n]:
        extra = f"  {len(originals)} original callees" if args.originals else ""
        print(f"  {rva:>9}  {size:5}B  {ncall:5} callers  {name[:90]}{extra}")
    if args.all:
        blocked = sorted((r for r in rows if not r[0]), key=lambda r: (len(r[6]), -r[2]))
        print(f"\n{len(blocked)} blocked (fewest blockers first):")
        for _, _, ncall, rva, name, size, blockers, _ in blocked[: args.n]:
            print(f"  {rva:>9}  {size:5}B  {ncall:5} callers  {name[:60]}  <- {', '.join(b[:40] for b in blockers[:3])}")


if __name__ == "__main__":
    main()
