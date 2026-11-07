"""Writes the function table difftest.exe uses to name a fault address inside the original image
(harness/monitor.h, --funcs): one line per function, "<rva hex> <size dec> <name>", sorted by RVA.

Name priority: a status entry's name (hand-written), an annotation's name (class::name for functions), db/names.jsonl,
then the Ghidra name from db/functions.jsonl. Facts from db/ only.

Usage: python -I tools/difftest_funcs.py <out file>   (skipped when <out> is newer than every db/ input)
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def _rows(path):
    if not path.exists():
        return
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    yield json.loads(line)
                except ValueError:
                    pass


def _rva(text):
    try:
        return int(str(text), 16)
    except ValueError:
        return None


def build():
    db = ROOT / "db"
    funcs = {}  # rva -> [size, name]
    for r in _rows(db / "functions.jsonl"):
        v = _rva(r.get("rva"))
        if v is not None:
            funcs[v] = [int(r.get("size") or 0), r.get("name") or ""]
    named = {}
    for r in _rows(db / "names.jsonl"):
        v = _rva(r.get("rva"))
        if v is not None and r.get("name"):
            named[v] = r["name"]
    for r in _rows(db / "annotations.jsonl"):
        if r.get("kind") in ("function", "name"):
            v = _rva(r.get("rva"))
            if v is not None and r.get("name"):
                named[v] = (r["class"] + "::" + r["name"]) if r.get("class") else r["name"]
    for path in sorted((db / "status").glob("*.jsonl")):
        for r in _rows(path):
            v = _rva(r.get("rva"))
            if v is not None and r.get("name"):
                named[v] = r["name"]
    for v, name in named.items():
        if v in funcs:
            funcs[v][1] = name
        else:
            funcs[v] = [0, name]
    return funcs


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    out = Path(argv[1])
    inputs = [p for p in (ROOT / "db").glob("*.jsonl") if p.name != "library-matches.jsonl"] + list((ROOT / "db/status").glob("*.jsonl"))
    if out.exists() and all(out.stat().st_mtime >= p.stat().st_mtime for p in inputs):
        return 0
    funcs = build()
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(".tmp")
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        for rva in sorted(funcs):
            size, name = funcs[rva]
            f.write("%x %d %s\n" % (rva, size, name.replace("\n", " ")))
    tmp.replace(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
