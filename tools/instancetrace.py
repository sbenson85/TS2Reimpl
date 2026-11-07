"""Compare the file traces of game instances run side by side (parallel-instance diagnosis, lane FV).

Reads host logs written with TS2HOST_TRACEFILES=2 (host/instance.cpp: the image's GetProcAddress hands out traced
CreateFileW / FindFirstFileW / FindNextFileW / folder writes, which is how the game's file layer reaches them) and prints:

  1. failed opens through the game's file layer (CreateFileW(proc)), by error, with sharing violations (32) first;
  2. opens that ask for write access to a file inside the game folder (outside EP9/TSBin): through hardlinks every
     instance shares those files, and the game opens files with FILE_SHARE_READ only, so a write handle held by one
     instance makes the other's opens fail;
  3. files of the game folder each log opened and the other did not (paths made relative to the instance's root);
  4. folder listings whose entry count differs between the logs.

    tools/py -I tools/instancetrace.py local/traces/<batch>/lot-simtrace-1.host.log local/traces/<batch>/lot-simtrace-2.host.log
    tools/py -I tools/instancetrace.py --max 40 A.host.log B.host.log
"""
import argparse
import collections
import re

OPEN = re.compile(r'file: CreateFileW\(proc\) "([^"]*)" access (0x[0-9a-f]+) share (\d+) disposition (\d+) -> (\S+), error (\d+)')
FIRST = re.compile(r'find: FindFirstFileW "([^"]*)" -> (\S+) "([^"]*)"')
NEXT = re.compile(r'find: next (\S+) -> (\d)')
ROOT = re.compile(r'^.*?/local/game(?:-\d+)?/', re.I)
GENERIC_WRITE, FILE_WRITE_DATA = 0x40000000, 0x2


def rel(path):
    p = path.replace("\\", "/").lower()
    m = ROOT.match(p)
    return ("<game>/" + p[m.end():]) if m else p


def in_game(r):
    return r.startswith("<game>/") and not r.startswith("<game>/ep9/tsbin/")


def read(path):
    fails, writes, opened, listings = [], [], set(), collections.Counter()
    handles = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        m = OPEN.search(line)
        if m:
            p, access, share, disp, h, err = m.group(1), int(m.group(2), 16), int(m.group(3)), int(m.group(4)), m.group(5), int(m.group(6))
            r = rel(p)
            if err:
                fails.append((err, r, access, share, disp, line[:12]))
            else:
                opened.add(r)
                if access & (GENERIC_WRITE | FILE_WRITE_DATA) and in_game(r):
                    writes.append((r, access, share, line[:12]))
            continue
        m = FIRST.search(line)
        if m:
            folder = rel(m.group(1))
            if m.group(2) not in ("FFFFFFFF", "ffffffff"):
                handles[m.group(2)] = folder
                listings[folder] += 1
            else:
                listings[folder] += 0
            continue
        m = NEXT.search(line)
        if m and m.group(2) == "1" and m.group(1) in handles:
            listings[handles[m.group(1)]] += 1
    return fails, writes, opened, listings


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--max", type=int, default=25, help="lines per section and log (default 25)")
    a = ap.parse_args()
    data = {log: read(log) for log in a.logs}
    for log, (fails, writes, opened, listings) in data.items():
        print(f"== {log}")
        print(f"  {len(opened)} files opened through the game's file layer, {len(listings)} folders listed")
        if not opened and not listings:
            print("  no CreateFileW(proc) / FindFirstFileW lines: run with TS2HOST_TRACEFILES=2 and a host built with lane FV's tracing")
        by_err = collections.Counter(f[0] for f in fails)
        print(f"  failed opens: {dict(by_err) or 'none'}")
        for err, r, access, share, disp, t in sorted(fails, key=lambda f: (f[0] != 32, f[5]))[:a.max]:
            print(f"    {t} error {err} access 0x{access:x} share {share} disp {disp} {r}")
        print(f"  write-access opens of shared game files: {len(writes)}")
        for r, access, share, t in writes[:a.max]:
            print(f"    {t} access 0x{access:x} share {share} {r}")
    if len(a.logs) == 2:
        (_, _, oa, la), (_, _, ob, lb) = data[a.logs[0]], data[a.logs[1]]
        for name, x, y in ((a.logs[0], oa, ob), (a.logs[1], ob, oa)):
            only = sorted(r for r in x - y if in_game(r))
            print(f"== game files only {name} opened: {len(only)}")
            for r in only[:a.max]:
                print(f"    {r}")
        diff = sorted(f for f in set(la) | set(lb) if la.get(f, 0) != lb.get(f, 0))
        print(f"== folder listings with different entry counts: {len(diff)}")
        for f in diff[:a.max]:
            print(f"    {la.get(f, 0):5} {lb.get(f, 0):5} {f}")


if __name__ == "__main__":
    main()
