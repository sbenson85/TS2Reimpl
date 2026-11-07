"""Generates the link-time symbols behind OriginalFunction<Fn, 0xRVA>() (src/game/original.h) and the owned statics
(TS2_OWNED_STATIC, src/game/owned.h).

Usage: origsyms.py --out <file.asm> [--root <repo>] [--exe-base 0x400000]
  Scans src/, tests/, harness/, host/, record/ and standalone/ for OriginalFunction<Fn, 0xRVA>() uses
  (tools/origcalls.py's parser) and writes a MASM file that defines one absolute symbol per RVA:
  the decorated name of `OriginalEntry<RVA>()` (a function template declared, never defined). A call
  through it is then a plain `call rel32` the linker resolves to the original function, as the original
  binary's own calls are.

The value is NOT the target address: for a rel32 reference to an absolute symbol the linker computes
S - (RVA of the field + 4), i.e. it treats S as image-relative, so the call lands at S + the executable's image
base. The symbol is therefore (game base + RVA) - exe base, with the exe linked at /BASE:<exe base> and /FIXED
(CMakeLists.txt). Consequence: the symbol is right for rel32 references only, and a reference that takes the
address (an `offset` operand) would be off by the exe base: src/game/original.h falls back to GameAddress in
the builds that don't fold the call (/Od), and tools/checkgamebase.py rejects any non-rel32 reference to it.

Owned statics: every `TS2_OWNED_STATIC(Type, <hex>)` in the scanned sources gets `_ts2_rva_<hex>`, an absolute data
symbol valued game base + RVA (no exe-base adjustment: a data reference (DIR32) to an absolute symbol is its value, as
for ts2_rva_0). ts2core_noimage drops origsyms.obj with them, so an image-free program defines those objects itself
(docs/standalone-shape.md, "Owned statics"). tools/checkgamebase.py checks the values in every linked map.

CMake runs this at build time (the file is not committed, so parallel lanes never conflict); the output is
rewritten only when it changes.

The decorated name of a function template with a non-type argument is
  ??$OriginalEntry@$0<value>@@@YAXXZ
where <value> is the unsigned argument in hex digits A..P (0..15), most significant first, ended by '@'
(zero is "A@"; 1..10 are the single digit '0'..'9' and are not used: RVAs are at least 0x1000).
tools/checkgamebase.py decodes the linked names again from the linker map and checks every address;
a name the compiler wants and this file lacks is an unresolved external at link time."""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import origcalls  # noqa: E402

SCAN_DIRS = ("src", "tests", "harness", "host", "record", "standalone")
BASE = None  # kGameBase, read from src/game/game.h
NAME = "??$OriginalEntry@$0{}@@@YAXXZ"
OWNED_RE = re.compile(r"\bTS2_OWNED_STATIC\((?:[^;()]|\([^;()]*\))*?,\s*([0-9a-fA-F]+)\s*\)")
NAME_RE = re.compile(r"^\?\?\$OriginalEntry@\$0([A-P]+)@@@YAXXZ$")


def encode(value):
    """The decorated name of OriginalEntry<value>()."""
    if value < 0x1000:
        raise ValueError(f"RVA {value:#x} is below 0x1000")
    digits = ""
    while value:
        digits = "ABCDEFGHIJKLMNOP"[value & 15] + digits
        value >>= 4
    return NAME.format(digits)


def decode(name):
    """The RVA of a decorated OriginalEntry<RVA>() name, or None when it isn't one."""
    m = NAME_RE.match(name)
    if not m:
        return None
    value = 0
    for c in m.group(1):
        value = value * 16 + (ord(c) - ord("A"))
    return value


def game_base(root):
    m = re.search(r"kGameBase\s*=\s*(0x[0-9a-fA-F]+)", (root / "src/game/game.h").read_text())
    return int(m.group(1), 16)


def sources(root):
    """[(path, text)] of every source under the scanned directories, read once: it runs before every build."""
    out = []
    for d in SCAN_DIRS:
        base = root / d
        if base.is_dir():
            out += [(p, p.read_text(encoding="utf-8", errors="replace"))
                    for p in sorted(base.rglob("*")) if p.suffix in (".cpp", ".h", ".c")]
    return out


def scan_rvas(root, files=None):
    """{rva: first 'file:line'} for every OriginalFunction use under the scanned directories."""
    found = {}
    for p, text in sources(root) if files is None else files:
        if "original.h" == p.name or "OriginalFunction" not in text:
            continue
        for line, rva in origcalls.uses(text):
            if rva is not None:
                found.setdefault(int(rva, 16), f"{p.relative_to(root).as_posix()}:{line}")
    return found


def scan_owned(root, files=None):
    """{hex spelling: first 'file:line'} for every TS2_OWNED_STATIC declaration or definition under the scanned
    directories (the macro's own header excluded). Two spellings of one RVA are an error: the C name is the spelling."""
    found, by_value = {}, {}
    for p, text in sources(root) if files is None else files:
        if (p.name == "owned.h" and p.parent.name == "game") or "TS2_OWNED_STATIC" not in text:
            continue
        for m in OWNED_RE.finditer(text):
            spelling = m.group(1)
            where = f"{p.relative_to(root).as_posix()}:{text.count(chr(10), 0, m.start()) + 1}"
            other = by_value.setdefault(int(spelling, 16), spelling)
            if other != spelling:
                raise ValueError(f"{where}: ts2_rva_{spelling} is also spelled ts2_rva_{other}")
            found.setdefault(spelling, where)
    return found


def render(rvas, base, exe_base=0x400000, owned=()):
    out = ["; Generated by tools/origsyms.py: one absolute symbol per original function our code calls",
           "; (OriginalFunction<Fn, 0xRVA>()), valued game base + RVA - exe base (see the tool). Do not edit.",
           ".386", ".model flat"]
    for rva in sorted(rvas):
        out.append(f"PUBLIC {encode(rva)}")
    for rva in sorted(rvas):
        out.append(f"{encode(rva)} EQU 0{(base + rva - exe_base) & 0xFFFFFFFF:08X}h")
    if owned:
        out.append("; Owned statics (TS2_OWNED_STATIC): the image's objects, game base + RVA.")
    for spelling in sorted(owned, key=lambda s: int(s, 16)):
        out.append(f"PUBLIC _ts2_rva_{spelling}")
        out.append(f"_ts2_rva_{spelling} EQU 0{(base + int(spelling, 16)) & 0xFFFFFFFF:08X}h")
    out.append("END")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    ap.add_argument("--exe-base", default="0x400000", help="the executables' /BASE (CMake: TS2_EXE_BASE)")
    args = ap.parse_args()
    root = Path(args.root)
    files = sources(root)
    text = render(scan_rvas(root, files), game_base(root), int(args.exe_base, 0), scan_owned(root, files))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    if not out.exists() or out.read_text() != text:
        out.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
