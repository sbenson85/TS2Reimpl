"""Extracts interface vtable slot names into db/interface-slots.jsonl.

Input: MSVC /d1reportAllClassLayout output for Las Marionetas's cIGZ*/cIRZ* interface headers
(local/lmlayout/layout.txt, produced by compiling each header; nothing from those headers enters
the repo except interface and method names in slot order, which are ABI facts the game shares).
Overloads keep their compiler slot order and get a #n suffix (#1 is the earliest slot).

Each line: {"interface", "slots": [method names], "source"}.
Usage: python -I tools/interface_slots.py [layout.txt]
"""
import json
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "local/lmlayout/layout.txt"
    lines = src.read_text(errors="replace").splitlines()
    tables = {}
    i = 0
    while i < len(lines):
        m = re.match(r"^(cI[GR]Z\w*)::\$vftable@:$", lines[i])  # primary vftable of an interface
        if m:
            name, slots = m.group(1), []
            i += 1
            while i < len(lines) and lines[i].strip():
                s = re.match(r"^\s*(\d+)\s*\|\s*&(\w+)::(.+?)\s*$", lines[i])
                if s:
                    slots.append(s.group(3))
                i += 1
            if slots and name not in tables:
                totals = Counter(slots)
                seen = Counter()
                labelled = []
                for s in slots:
                    seen[s] += 1
                    labelled.append(s if totals[s] == 1 else f"{s}#{seen[s]}")
                tables[name] = labelled
        i += 1

    with open(ROOT / "db/interface-slots.jsonl", "w", newline="\n") as f:
        for name in sorted(tables):
            f.write(json.dumps({"interface": name, "slots": tables[name],
                                "source": "LasMarionetas interface headers (MSVC layout)"}) + "\n")
    print(len(tables), "interfaces")


if __name__ == "__main__":
    main()
