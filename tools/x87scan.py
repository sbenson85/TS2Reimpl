"""x87 arithmetic in our own compiled code (lane PJ; docs/lane/test.md, "x87 precision").

    tools/py -I tools/x87scan.py [stem ...] [--config Release]

/arch:SSE2 does not keep MSVC off the x87: it adds, subtracts, multiplies and divides on the x87 a value a call returns
in st(0) (float / double returns, std::hypot, vtable getters), and /O1 (*.size.cpp) does so for small helpers returning
double. Such an operation rounds at the thread's x87 precision (24-bit in game under D3D9, 53 in the harness), so where
the original does the same step in SSE2 ours depends on the precision and the original does not.

Lists, for every object file of ts2core (or those whose stem contains one of the arguments), each function with x87
arithmetic (fadd / fsub / fsubr / fmul / fdiv / fdivr and their p forms, fsqrt) and the instructions. Compare each with
the original's disassembly of the same function (tools/classinfo.py --disasm <rva>): a single float operation stored
to a float is harmless; a double result or a chain is not when the original uses SSE2 there.
"""
import argparse
import glob
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tsdb  # noqa: E402

OPS = re.compile(r"\s(f(?:add|sub|subr|mul|div|divr)p?|fsqrt)(?:\s|$)")
FUNCTION = re.compile(r"^(\S.*):$")


def dumpbin():
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    vs = subprocess.run([str(vswhere), "-products", "*", "-latest", "-property", "installationPath"],
                        capture_output=True, text=True).stdout.strip()
    found = sorted(glob.glob(os.path.join(vs, "VC/Tools/MSVC/*/bin/Hostx64/x86/dumpbin.exe")))
    if not found:
        sys.exit("dumpbin.exe not found (Visual Studio C++ tools)")
    return found[-1]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stems", nargs="*", help="object file stem substrings (default: all)")
    ap.add_argument("--config", default="Release")
    args = ap.parse_args()
    objdir = tsdb.object_dir(ROOT / "build", "ts2core", args.config)
    objs = sorted(objdir.rglob("*.obj"))
    if args.stems:
        objs = [o for o in objs if any(s in o.stem for s in args.stems)]
    if not objs:
        sys.exit(f"no object files in {objdir} (build first: tools/difftest.ps1 -BuildOnly)")
    tool = dumpbin()
    total = 0
    for obj in objs:
        out = subprocess.run([tool, "/nologo", "/disasm", str(obj)], capture_output=True, text=True).stdout
        current, hits = None, {}
        for line in out.splitlines():
            m = FUNCTION.match(line)
            if m:
                current = m.group(1)
                continue
            if current and OPS.search(line):
                hits.setdefault(current, []).append(line.strip())
        for function, lines in hits.items():
            total += 1
            print(f"{obj.stem}: {function}")
            for line in lines:
                print(f"    {line}")
    print(f"{total} function(s) with x87 arithmetic in {len(objs)} object file(s)")


if __name__ == "__main__":
    main()
