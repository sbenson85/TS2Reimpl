#!/usr/bin/env python3
"""Which trapping imports block the most code from differential testing (review part 2, item 6).

The harness (harness/image.cpp) traps every import that isn't passed through or sandboxed, so an
original function that can reach a trapping import through its direct calls can't run in a test.
For each game function not yet done (not verified, replaced or library code), this collects the
trapping imports reachable through direct calls (from db/functions.jsonl; virtual calls aren't followed, so it is a lower
bound) and ranks imports by the bytes of functions that only that import blocks: making it pass
through (or sandboxing it) would unblock them, as far as direct calls show.

Usage: python -I tools/importblockers.py [--top 30] [--show IMPORT]
"""
import argparse
import collections
import pathlib
import re
import sys


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import tsdb  # noqa: E402


# Reached only on fatal errors (CRT abort, terminate, report-and-exit paths and their console output):
# a test that reaches them has failed anyway, so they don't block testing. --fatal counts them.
FATAL = {"kernel32.dll!terminateprocess", "kernel32.dll!exitprocess", "kernel32.dll!setunhandledexceptionfilter",
         "kernel32.dll!unhandledexceptionfilter", "kernel32.dll!getconsolemode", "kernel32.dll!getconsoleoutputcp",
         "kernel32.dll!writeconsolew", "kernel32.dll!readconsolew", "kernel32.dll!setstdhandle"}


def harness_allowed():
    """Imports the harness doesn't trap: the passthrough list and the sandboxed functions."""
    root = tsdb.main_root()
    image = (root / "harness" / "image.cpp").read_text(encoding="utf-8")
    sandbox = (root / "harness" / "sandbox.cpp").read_text(encoding="utf-8")
    allowed = {m.lower() for m in re.findall(r'"([A-Za-z0-9_]+\.dll![A-Za-z0-9_]+)"', image, re.I)}
    sandboxed = {m.lower() for m in re.findall(r"\bE\(([A-Za-z0-9_]+)\)", sandbox)}
    return allowed, sandboxed


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--fatal", action="store_true", help="count the fatal-error-only imports as blockers too")
    ap.add_argument("--show", help="list the largest functions blocked only by this import")
    args = ap.parse_args()

    allowed, sandboxed = harness_allowed()

    def traps(name):
        if not args.fatal and name.lower() in FATAL:
            return False
        return name.lower() not in allowed and name.split("!")[1].lower() not in sandboxed

    funcs = {int(f["rva"], 16): f for f in tsdb.functions()}
    # db/functions.jsonl lists imports among the callees as "DLL!name".
    direct = {rva: {c for c in f["callees"] if "!" in c and traps(c)} for rva, f in funcs.items()}
    callees = {rva: [int(c, 16) for c in f["callees"] if "!" not in c and int(c, 16) in funcs]
               for rva, f in funcs.items()}

    # Fixed point: reachable trapping imports through direct calls.
    reach = {rva: set(h) for rva, h in direct.items()}
    changed = True
    while changed:
        changed = False
        for rva, cs in callees.items():
            r = reach[rva]
            before = len(r)
            for c in cs:
                r |= reach[c]
            if len(r) != before:
                changed = True

    done = {r for r, e in tsdb.load_status().items() if e["status"] in ("verified", "replaced")}
    done = {int(r, 16) if isinstance(r, str) else r for r in done}
    library = tsdb.library_rvas()
    library = {int(r, 16) if isinstance(r, str) else r for r in library}
    todo = [r for r in funcs if r not in done and r not in library]

    only = collections.Counter()
    only_list = collections.defaultdict(list)
    among = collections.Counter()
    free = 0
    for r in todo:
        size = funcs[r]["size"]
        if not reach[r]:
            free += size
            continue
        for n in reach[r]:
            among[n] += size
        if len(reach[r]) == 1:
            n = next(iter(reach[r]))
            only[n] += size
            only_list[n].append(r)
    total = sum(funcs[r]["size"] for r in todo)
    print(f"{len(todo)} functions not done ({total:,} bytes); {free:,} bytes reach no trapping import "
          f"through direct calls")
    print(f"{'import':48} {'only blocker (bytes)':>22} {'among blockers (bytes)':>24}")
    for n in sorted(among, key=lambda n: (-only[n], -among[n]))[:args.top]:
        print(f"{n:48} {only[n]:22,} {among[n]:24,}")
    if args.show:
        key = next((n for n in only_list if n.lower().endswith(args.show.lower())), None)
        for r in sorted(only_list.get(key, []), key=lambda r: -funcs[r]["size"])[:40]:
            print(f"  0x{r:x} {funcs[r]['size']:6} {funcs[r]['name']}")


if __name__ == "__main__":
    sys.exit(main())
