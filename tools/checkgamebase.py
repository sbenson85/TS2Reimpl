"""Post-build check of the game-base symbols and the fixed image.

Usage: checkgamebase.py <map file | exe | object directory (<build>/*.dir/<config>)> [...]
(exit 1, with the offending symbols, on any mismatch)

For every linker map:
  - every `ts2_rva_<hex>` symbol resolves to game base + <hex> (and at least one is linked at all);
  - every `OriginalEntry<RVA>` symbol (the targets of OriginalFunction<Fn, 0xRVA>(), src/game/original.h,
    named by tools/origsyms.py) is absolute and equals game base + RVA - the executable's preferred load
    address: a rel32 reference to an absolute symbol lands at the symbol's value plus the image base (the linker
    treats it as image-relative), so that is the value that makes the call reach game base + RVA.
For every exe: the image is fixed (relocations stripped, no ASLR), because calls to original code are
`call rel32` to absolute addresses that a load at another base would break.
For every object directory: no object references an OriginalEntry symbol other than by rel32 (a call or jump),
since any other reference (taking the address) would get the image-relative value, off by the image base;
src/game/original.h is built so that doesn't happen, and this fails the build if it does.
The game base is kGameBase in src/game/game.h. Reads names, addresses, PE flags and object relocations only; no
game bytes."""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import origsyms  # noqa: E402

BASE = int(re.search(r"kGameBase\s*=\s*(0x[0-9a-fA-F]+)", (ROOT / "src/game/game.h").read_text()).group(1), 16)
LINE = re.compile(r"^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+_ts2_rva_([0-9a-fA-F]+)\s+([0-9a-f]{8})\s+<absolute>", re.M)
ENTRY = re.compile(r"^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\?\?\$OriginalEntry@\S*)\s+([0-9a-f]{8})\s+(\S+)", re.M)
PREFERRED = re.compile(r"Preferred load address is ([0-9a-fA-F]+)")
IMAGE_REL_I386_REL32 = 0x14
IMAGE_SCN_LNK_NRELOC_OVFL = 0x01000000
IMAGE_FILE_RELOCS_STRIPPED = 0x0001
IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE = 0x0040


def check_map(path):
    text = Path(path).read_text(errors="replace")
    bad = 0
    m = PREFERRED.search(text)
    if not m:
        print(f"checkgamebase: {path}: no preferred load address in the map", file=sys.stderr)
        return 1
    exe_base = int(m.group(1), 16)
    found = LINE.findall(text)
    if not found:
        print(f"checkgamebase: {path}: no ts2_rva symbol linked", file=sys.stderr)
        bad += 1
    for rva, value in found:
        if int(value, 16) != BASE + int(rva, 16):
            print(f"checkgamebase: {path}: ts2_rva_{rva} = {value}, expected {BASE + int(rva, 16):08x}", file=sys.stderr)
            bad += 1
    entries = ENTRY.findall(text)
    for name, value, kind in entries:
        rva = origsyms.decode(name)
        want = None if rva is None else (BASE + rva - exe_base) & 0xFFFFFFFF
        if rva is None:
            print(f"checkgamebase: {path}: {name}: not a name tools/origsyms.py generates", file=sys.stderr)
            bad += 1
        elif kind != "<absolute>" or int(value, 16) != want:
            print(f"checkgamebase: {path}: {name} = {value} {kind}, expected absolute {want:08x} "
                  f"(RVA {rva:#x}, image base {exe_base:#x})", file=sys.stderr)
            bad += 1
    print(f"checkgamebase: {path}: {len(found)} ts2_rva symbol(s), {len(entries)} original-function symbol(s) checked")
    return bad


def check_exe(path):
    data = Path(path).read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        print(f"checkgamebase: {path}: not a PE file", file=sys.stderr)
        return 1
    characteristics = struct.unpack_from("<H", data, pe + 22)[0]
    opt = pe + 24
    dll_characteristics = struct.unpack_from("<H", data, opt + 70)[0]
    bad = 0
    if not characteristics & IMAGE_FILE_RELOCS_STRIPPED:
        print(f"checkgamebase: {path}: relocations are not stripped (link with /FIXED)", file=sys.stderr)
        bad += 1
    if dll_characteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE:
        print(f"checkgamebase: {path}: DYNAMICBASE is set (link with /DYNAMICBASE:NO)", file=sys.stderr)
        bad += 1
    return bad


def non_rel32_refs(data):
    """[(section name, relocation type)] for every relocation in the COFF object `data` that targets an
    OriginalEntry symbol and isn't REL32."""
    if b"OriginalEntry" not in data:
        return []
    nsec = struct.unpack_from("<H", data, 2)[0]
    symptr, nsyms = struct.unpack_from("<II", data, 8)
    optsz = struct.unpack_from("<H", data, 16)[0]
    strings = symptr + nsyms * 18
    wanted, i = set(), 0
    while i < nsyms:
        rec = symptr + i * 18
        if data[rec:rec + 4] == b"\0\0\0\0":
            off = struct.unpack_from("<I", data, rec + 4)[0]
            name = data[strings + off:data.index(b"\0", strings + off)]
        else:
            name = data[rec:rec + 8].rstrip(b"\0")
        if name.startswith(b"??$OriginalEntry@"):
            wanted.add(i)
        i += 1 + data[rec + 17]
    out = []
    for s in range(nsec):
        sh = 20 + optsz + 40 * s
        sname = data[sh:sh + 8].rstrip(b"\0").decode(errors="replace")
        relptr = struct.unpack_from("<I", data, sh + 24)[0]
        nreloc = struct.unpack_from("<H", data, sh + 32)[0]
        chars = struct.unpack_from("<I", data, sh + 36)[0]
        first = 0
        if nreloc == 0xFFFF and chars & IMAGE_SCN_LNK_NRELOC_OVFL:  # the real count is in the first entry
            nreloc = struct.unpack_from("<I", data, relptr)[0]
            first = 1
        for r in range(first, nreloc):
            _, symidx, typ = struct.unpack_from("<IIH", data, relptr + 10 * r)
            if symidx in wanted and typ != IMAGE_REL_I386_REL32:
                out.append((sname, typ))
    return out


def check_objects(directory):
    bad = n = 0
    for obj in sorted(Path(directory).rglob("*.obj")):
        n += 1
        for sec, typ in non_rel32_refs(obj.read_bytes()):
            print(f"checkgamebase: {obj}: section {sec}: relocation type {typ:#x} against an OriginalEntry symbol "
                  f"(only a call or jump, rel32, reaches the right address)", file=sys.stderr)
            bad += 1
    print(f"checkgamebase: {directory}: {n} object(s) checked for non-rel32 references to original functions")
    return bad


def main(paths):
    bad = 0
    for p in paths:
        if Path(p).is_dir():
            bad += check_objects(p)
        elif p.lower().endswith(".map"):
            bad += check_map(p)
        else:
            bad += check_exe(p)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
