"""Are two build trees' objects, or two linked images, the same code? For a change to how they're made (a generator, a
compile cache).

Every object is parsed and compared section by section, relocation by relocation, symbol by symbol. Only three things
are left out, and each object says which one it needed:
  - time:   the COFF header's TimeDateStamp and S_OBJNAME's signature, a clock reading (two compiles of a file differ in it);
  - path:   the object's own path in .debug$S (S_OBJNAME), which names the checkout it was compiled in (with it go the
            record's length, the section's aux length and checksum, and the offsets of that section's relocations);
  - pchsym: the name of the PCH's link symbol (__@@_PchSym_@...), which cl derives from the /Fp path.
With --anon a fourth: the hash in anonymous-namespace names (?A0x12345678), which cl derives from the source's full path.
Without /d1trimfile it differs between two checkouts, so it's only left out on request, to show that nothing else does.
.chks64 is left out too: it holds a checksum of each section, and the sections themselves are compared.
Anything else is a difference: the object is listed as DIFF with the first thing that differs. An object in only one
tree is listed as ONLY. Exit 1 on any DIFF or ONLY, or when no object was found.

Usage: python -I tools/objcmp.py TREE_A TREE_B [--target ts2core,difftest] [--config Release] [--limit 20] [--anon]
TREE_* are build directories; objects are read from the object folders of the given targets (default every *.dir).

       python -I tools/objcmp.py --image A.exe B.exe [--anon]
compares two linked images byte by byte. Only time stamp fields are left out (the file header's, the debug, export and
resource directories', and the header checksum, which follows from them); exit 1 on any other byte. --anon also leaves
out the eight hash digits of each ?A0x name, and says how many bytes that took.
"""
import argparse
import re
import struct
import sys
from collections import Counter
from pathlib import Path

PCH_SYMBOL = re.compile(rb"_*@@_PchSym_@[^\0 ]*")
ANON = re.compile(rb"\?A0x[0-9a-f]{8}")
BIGOBJ_CLASS = b"\xc7\xa1\xba\xd1\xee\xba\xa9\x4b\xaf\x20\xfa\xf6\x6a\xa4\xdc\xb8"
S_OBJNAME = 0x1101
DEBUG_S_SYMBOLS = 0xF1
IMAGE_SYM_CLASS_STATIC = 3


def debug_s(raw):
    """A .debug$S section without its S_OBJNAME's fields: (canonical bytes, name, signature). Other shapes stay whole."""
    if len(raw) < 4 or raw[:4] != b"\x04\0\0\0":
        return raw, None, None
    out = bytearray(raw[:4])
    at = 4
    name = signature = None
    while at + 8 <= len(raw):
        kind, size = struct.unpack_from("<II", raw, at)
        body = raw[at + 8:at + 8 + size]
        if kind == DEBUG_S_SYMBOLS:
            rebuilt = bytearray()
            pos = 0
            while pos + 4 <= len(body):
                length, record = struct.unpack_from("<HH", body, pos)
                item = body[pos:pos + 2 + length]
                if record == S_OBJNAME:
                    name = item[8:].split(b"\0", 1)[0]
                    signature = item[4:8]
                    item = item[2:4]
                rebuilt += item
                pos += 2 + length
            rebuilt += body[pos:]
            body = bytes(rebuilt)
        out += struct.pack("<I", kind) + body
        at += 8 + ((size + 3) & ~3)
    out += raw[at:]
    return bytes(out), name, signature


def parse(data):
    """(canonical form of an object, its time stamp, its S_OBJNAME names, its PCH symbol names)."""
    bigobj = data[:4] == b"\0\0\xff\xff" and data[12:28] == BIGOBJ_CLASS
    if bigobj:
        machine = struct.unpack_from("<H", data, 6)[0]
        stamp, sections, symtab, symbols = struct.unpack_from("<IIII", data, 8)[0], *struct.unpack_from("<III", data, 44)
        table = 56
        entry = 20
        number = "<i"
    else:
        machine, sections, stamp, symtab, symbols = struct.unpack_from("<HHIII", data, 0)
        table = 20 + struct.unpack_from("<H", data, 16)[0]
        entry = 18
        number = "<h"
    strings_at = symtab + symbols * entry

    def text_at(offset):
        end = data.index(b"\0", strings_at + offset)
        return data[strings_at + offset:end]

    objnames = []
    signatures = []
    pch = []
    canon_sections = []
    follows_path = set()  # sections whose length and checksum depend on a left-out field
    for i in range(sections):
        head = data[table + i * 40:table + (i + 1) * 40]
        name = head[:8].rstrip(b"\0")
        vsize, vaddr, size, raw_at, reloc_at, line_at, relocs, lines, flags = struct.unpack_from("<IIIIIIHHI", head, 8)
        raw = data[raw_at:raw_at + size] if raw_at else b""
        relocations = data[reloc_at:reloc_at + relocs * 10] if reloc_at else b""
        if name == b".chks64":  # one checksum per section, of what's compared here anyway
            raw = b""
        if name == b".debug$S":
            raw, objname, signature = debug_s(raw)
            if objname is not None:
                objnames.append(objname)
                signatures.append(signature)
                follows_path.add(i + 1)
                relocations = b"".join(relocations[at + 4:at + 10] for at in range(0, len(relocations), 10))
                size = len(raw)
        elif name == b".drectve":
            found = PCH_SYMBOL.findall(raw)
            if found:
                raw = PCH_SYMBOL.sub(b"<pch symbol>", raw)
                pch += found
                follows_path.add(i + 1)
                size = len(raw)
        canon_sections.append((name, vsize, vaddr, size, relocs, lines, flags, raw, relocations,
                               data[line_at:line_at + lines * 6] if line_at else b""))
    canon_symbols = []
    at = 0
    while at < symbols:
        rec = data[symtab + at * entry:symtab + (at + 1) * entry]
        name = rec[:8].rstrip(b"\0") if rec[:4] != b"\0\0\0\0" else text_at(struct.unpack_from("<I", rec, 4)[0])
        if PCH_SYMBOL.fullmatch(name):
            pch.append(name)
            name = b"<pch symbol>"
        value = struct.unpack_from("<I", rec, 8)[0]
        section = struct.unpack_from(number, rec, 12)[0]
        kind, storage, aux = struct.unpack_from("<HBB", rec, entry - 4)
        extra = data[symtab + (at + 1) * entry:symtab + (at + 1 + aux) * entry]
        if storage == IMAGE_SYM_CLASS_STATIC and value == 0 and section in follows_path:
            extra = b""
        canon_symbols.append((name, value, section, kind, storage, extra))
        at += 1 + aux
    return (machine, bigobj, canon_sections, canon_symbols), (stamp, signatures), objnames, pch


def first_difference(a, b):
    if a[0] != b[0] or a[1] != b[1]:
        return "header"
    if len(a[2]) != len(b[2]):
        return f"{len(a[2])} against {len(b[2])} sections"
    for x, y in zip(a[2], b[2]):
        if x != y:
            part = next(n for n, p, q in zip(("name", "vsize", "vaddr", "size", "relocs", "lines", "flags", "raw",
                                              "relocations", "line numbers"), x, y) if p != q)
            return f"section {x[0].decode(errors='replace')}: {part}"
    if len(a[3]) != len(b[3]):
        return f"{len(a[3])} against {len(b[3])} symbols"
    for x, y in zip(a[3], b[3]):
        if x != y:
            return f"symbol {x[0].decode(errors='replace')[:80]}"
    return "?"


def compare(a_bytes, b_bytes, anon=False):
    """("same"|"time"|"path"|"pchsym"|"anon"..., or "DIFF", detail): what it took for the two objects to be equal."""
    if a_bytes == b_bytes:
        return "same", ""
    a, a_stamp, a_names, a_pch = parse(a_bytes)
    b, b_stamp, b_names, b_pch = parse(b_bytes)
    plain = a == b
    if not plain and anon:
        a, b = (parse(ANON.sub(b"?A0x00000000", x))[0] for x in (a_bytes, b_bytes))
    if a != b:
        return "DIFF", first_difference(a, b)
    needed = [label for label, differs in (("time", a_stamp != b_stamp), ("path", a_names != b_names),
                                           ("pchsym", a_pch != b_pch), ("anon", not plain)) if differs]
    return "+".join(needed) or "same", ""


def objects(build, targets, config):
    build = Path(build)
    top = build / "CMakeFiles" if (build / "build.ninja").exists() else build
    out = {}
    for folder in sorted(top.glob("*.dir")):
        if targets and folder.stem not in targets:
            continue
        base = folder / config
        for path in base.rglob("*.obj"):
            out[f"{folder.name}/{path.relative_to(base).as_posix()}"] = path
    return out


def timestamp_ranges(data):
    """The byte ranges of a PE image that hold a link time or the checksum over it."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    sections, optional = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    plus = struct.unpack_from("<H", data, opt)[0] == 0x20B
    directories = opt + (112 if plus else 96)
    table = opt + optional
    spans = [struct.unpack_from("<IIII", data, table + i * 40 + 8) for i in range(sections)]

    def file_offset(rva):
        for vsize, vaddr, size, raw in spans:
            if vaddr <= rva < vaddr + max(vsize, size):
                return raw + rva - vaddr
        raise ValueError(f"RVA {rva:#x} is in no section")

    def directory(index):
        return struct.unpack_from("<II", data, directories + index * 8)

    out = [(pe + 8, 4), (opt + 64, 4)]
    rva, size = directory(0)
    if size:
        out.append((file_offset(rva) + 4, 4))
    rva, size = directory(2)
    if size:
        base = file_offset(rva)
        pending = [base]
        while pending:
            table_at = pending.pop()
            out.append((table_at + 4, 4))
            named, numbered = struct.unpack_from("<HH", data, table_at + 12)
            for i in range(named + numbered):
                target = struct.unpack_from("<I", data, table_at + 16 + i * 8 + 4)[0]
                if target & 0x80000000:
                    pending.append(base + (target & 0x7FFFFFFF))
    rva, size = directory(6)
    if size:
        at = file_offset(rva)
        for i in range(size // 28):
            out.append((at + i * 28 + 4, 4))
    return out


def compare_images(a, b, anon=False):
    """(byte offsets outside the left-out fields that differ, offsets inside them that differ), or a size message."""
    if len(a) != len(b):
        return f"sizes differ: {len(a)} against {len(b)}", []
    if a == b:
        return [], []
    ignored = set()
    for start, length in timestamp_ranges(a):
        ignored.update(range(start, start + length))
    if anon:
        for m in ANON.finditer(a):
            ignored.update(range(m.start() + 4, m.end()))
    different = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
    return [i for i in different if i not in ignored], [i for i in different if i in ignored]


def main_image(a_path, b_path, anon=False):
    other, left_out = compare_images(Path(a_path).read_bytes(), Path(b_path).read_bytes(), anon)
    if isinstance(other, str):
        print(other)
        return 1
    print(f"{len(other)} bytes differ outside the left-out fields ({len(left_out)} inside)")
    if other:
        print("first at file offsets " + ", ".join(f"{i:#x}" for i in other[:8]))
    return 1 if other else 0


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "--image":
        sys.exit(main_image(sys.argv[2], sys.argv[3], "--anon" in sys.argv[4:]))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--target", default="", help="comma list of CMake targets (default all)")
    ap.add_argument("--config", default="Release")
    ap.add_argument("--limit", type=int, default=20, help="DIFF and ONLY lines printed")
    ap.add_argument("--anon", action="store_true", help="leave out the hash in ?A0x names")
    args = ap.parse_args()
    targets = set(filter(None, args.target.split(",")))
    a = objects(args.a, targets, args.config)
    b = objects(args.b, targets, args.config)
    tally = Counter()
    shown = 0

    def report(line):
        nonlocal shown
        if shown < args.limit:
            print(line)
        shown += 1

    for key in sorted(a.keys() | b.keys()):
        if key not in a or key not in b:
            tally["ONLY"] += 1
            report(f"ONLY {'a' if key in a else 'b'} {key}")
            continue
        verdict, detail = compare(a[key].read_bytes(), b[key].read_bytes(), args.anon)
        tally[verdict] += 1
        if verdict == "DIFF":
            report(f"DIFF {key}: {detail}")
    print(f"{sum(tally.values())} objects: " + ", ".join(f"{n} {k}" for k, n in sorted(tally.items())))
    sys.exit(1 if tally["DIFF"] or tally["ONLY"] or not tally else 0)  # nothing compared proves nothing


if __name__ == "__main__":
    main()
