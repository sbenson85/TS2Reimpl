"""Checks every GMDC the hooked game decoded (TS2HOST_GMDC=1 host log) against the standalone decoder.

Milestone N-f (docs/subsystems/scenegraph.md, "Milestone N", N-f). For each (instance, group, resource) in the host log's
`gmdc-expected - ...` lines, finds the packages that hold that GMDC by reading their DBPF indexes (local/game, without
UserData and NeighborhoodTemplate, plus the save folder), runs build/Release/gmdc_decode.exe on the key in each, and counts
the key as matching when the game's summary equals the standalone decoder's for at least one of them (an expansion
package may override a key Base also holds). A key the game reports under a runtime group (0x6f00xxxx) is looked up as
the package's local group 0xffffffff.

Usage: tools/py -I tools/gmdc_crosscheck.py HOST_LOG [--step N]   (--step samples every Nth key)
Exit 0 when every key is indexed and matches.
"""
import argparse
import glob
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "Release", "gmdc_decode.exe")
GMDC = 0xAC4F8687
LOCAL_GROUP = "ffffffff"


def host_keys(log):
    host = {}
    with open(log, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.search(r"gmdc-expected - (\w{8}) (\w{8}) (\w{8}) (.*)$", line.rstrip())
            if m:
                host.setdefault(m.group(1, 2, 3), set()).add(m.group(4))
    return host


def packages():
    game = os.path.join(ROOT, "local", "game")
    out = [p for p in glob.glob(os.path.join(game, "**", "*.package"), recursive=True)
           if "UserData" not in p and "NeighborhoodTemplate" not in p]
    saves = os.path.expanduser(os.path.join("~", "Documents", "EA Games", "The Sims 2 Legacy"))
    return out + glob.glob(os.path.join(saves, "**", "*.package"), recursive=True)


def index(pkgs):
    """{(instance, group, resource) hex: [package]} of every GMDC in the packages' DBPF indexes."""
    where = {}
    for p in pkgs:
        with open(p, "rb") as f:
            h = f.read(0x60)
            if len(h) < 0x60 or h[:4] != b"DBPF":
                continue
            count, off, size = struct.unpack_from("<III", h, 0x24)
            if count == 0 or size // count not in (20, 24):
                continue
            es = size // count
            f.seek(off)
            data = f.read(count * es)
        for i in range(min(count, len(data) // es)):
            t, g, inst = struct.unpack_from("<III", data, i * es)
            if t != GMDC:
                continue
            res = struct.unpack_from("<I", data, i * es + 12)[0] if es == 24 else 0
            where.setdefault((f"{inst:08x}", f"{g:08x}", f"{res:08x}"), []).append(p)
    return where


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--step", type=int, default=1)
    a = ap.parse_args()
    host = host_keys(a.log)
    pkgs = packages()
    where = index(pkgs)
    keys = sorted(host)[::a.step]
    ok = bad = missing = local = 0
    for k in keys:
        look = k if k in where else (k[0], LOCAL_GROUP, k[2])
        if look not in where:
            missing += 1
            print("not indexed", *k)
            continue
        local += look != k
        sums = {}
        for p in where[look]:
            r = subprocess.run([EXE, p, *look], capture_output=True, text=True, timeout=60)
            s = [l[8:] for l in r.stdout.splitlines() if l.startswith("summary ")]
            sums[p] = s[0] if s else None
        if any(s is not None and {s} == host[k] for s in sums.values()):
            ok += 1
        else:
            bad += 1
            print("DIFF", *k, "host", sorted(host[k]))
            for p, s in sums.items():
                print("   ", os.path.relpath(p, ROOT), s)
    print(f"packages {len(pkgs)}, host keys {len(host)}, checked {len(keys)}: match {ok} ({local} by local group), "
          f"differ {bad}, not indexed {missing}")
    return 0 if bad == 0 and missing == 0 and ok > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
