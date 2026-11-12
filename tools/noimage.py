"""What an image-free program (milestone N on: gmdc_decode) still needs from the game image.

Usage: noimage.py <linker map> <object directory> [...] [--log <build log>] [--by-function] [--json <out>]
                  [--against <earlier --json or saved run>] [--no-save] [--check [--delete <exe>]]
       noimage.py --gate <program> [--config Release]     (render_object, gmdc_decode: report only, exit 0)
       noimage.py --write-aliases <out.obj or rsp> <library or object directory or object> [...] [--trap <symbol>]

After the list it prints what changed against the previous run (lane QF): references removed and references exposed
(owning a vtable roots its slots, so /OPT:REF keeps code whose references then show up). The previous run is
build/noimage-<map stem>.last.jsonl of this checkout (each run saves its result there unless --no-save), or the file
--against names (an earlier --json, e.g. main's run copied over).

The program is linked against ts2core_noimage (CMakeLists.txt): our objects without tools/gamebase.asm and the
generated origsyms.asm, with /FORCE:UNRESOLVED (-DTS2_NOIMAGE_FORCE=ON) so the linker still writes the map. The map
says which of our functions were linked (after /OPT:REF) and from which object; the objects' relocations say which of
those functions refer to
  - _ts2_rva_0 (GameAddress<T>(rva), ImportFunction, the game vtables the hook-shaped classes store): the RVA is the
    relocation's addend, read from our own compiled bytes;
  - ??$OriginalEntry@...@@YAXXZ (OriginalFunction<Fn, 0xRVA>()): the RVA is in the name (tools/origsyms.py);
  - _ts2_rva_<hex> (an owned static, src/game/owned.h) that the program does not define itself: the RVA is the hex
    plus the addend. Once standalone/ defines it, it is no longer a reference to the image.
An image symbol an object aliases with /alternatename to a symbol the program defines (other than the trap, below) is
resolved by that definition and is no reference; one aliased to the trap still is.

The normal link (lane QQ): --write-aliases writes `/alternatename:<symbol>=<trap>` for every image symbol the inputs
(ts2core_noimage.lib, the program's own objects) leave undefined and do not alias themselves: as an object holding only
a .drectve section (a name ending .obj; the link takes it as an input; keep it out of the scanned directories, or its
aliases count as the program's own next time) or as a response file.
MSVC reports an unresolved symbol for every section of an object it loads, live or not, so the link fails even when
/OPT:REF would drop every reference; with the aliases it links, and the trap (one function in its own COMDAT,
standalone/noimage_trap.cpp) is kept only if live code refers to the image. --check is the gate after the link: it
fails (exit 1; --delete removes the exe) when the trap is in the map or any image reference is left in linked code,
so an exe that links is image-free. CMakeLists.txt runs both (gmdc_decode's PRE_LINK and POST_BUILD).
--gate <program> reads what the post-build gate reads (build/<config>/<program>.map, the object folders of ts2core and
<program>) and prints the same list plus one "noimage gate" line (the reference count, the RVAs,
whether the trap is linked), without saving or failing: while a program still needs the image, its build reports
"Build failed" (the gate deleted the exe) but writes the map, and this reads the count from it (lane FT).
Each RVA is named from db/ (names, functions with sizes, class vtables, status). `--log` adds the other unresolved
symbols of the build log (library pieces, declared-but-undefined members). Reads names, relocations and addends of our
own objects only; no game bytes.
"""
import argparse
import json
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import origsyms  # noqa: E402
import tsdb  # noqa: E402

IMAGE_REL_I386_DIR32 = 0x06
IMAGE_REL_I386_DIR32NB = 0x07
IMAGE_REL_I386_REL32 = 0x14
IMAGE_SCN_LNK_NRELOC_OVFL = 0x01000000
IMAGE_SCN_LNK_COMDAT = 0x1000
GAME_BASE_SYMBOL = "_ts2_rva_0"
OWNED_SYMBOL = re.compile(r"^_ts2_rva_([0-9a-fA-F]+)$")

MAP_LINE = re.compile(r"^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+[0-9a-f]{8}\s+(?:f\s+)?(?:i\s+)?(\S+)\s*$", re.M)
# error LNK2019: unresolved external symbol "<undecorated>" (<decorated>) referenced in function "<...>" (<decorated>)
# (a C symbol has no quoted form: unresolved external symbol _name referenced in function ...)
NAME = r"(?:\"[^\"]*\" \((\S+)\)|(\S+))"
UNRESOLVED = re.compile(r"error LNK20(?:01|19): unresolved external symbol " + NAME +
                        r"(?: referenced in function " + NAME + ")?")


TRAP_SYMBOL = "_ts2_noimage_trap"  # standalone/noimage_trap.cpp (extern "C", __cdecl)
TRAP_OBJECT = "noimage_trap.obj"  # the object that defines it
ALIAS = re.compile(rb"[/-]alternatename:([^ \"\x00=]+)=([^ \"\x00]+)", re.I)


def is_image_symbol(name):
    """Whether a symbol is one the game image provides (tools/gamebase.asm, the generated origsyms.asm)."""
    return name == GAME_BASE_SYMBOL or bool(OWNED_SYMBOL.match(name)) or origsyms.decode(name) is not None


def aliases_in(data):
    """The /alternatename directives in an object's bytes (its .drectve), {symbol: target}."""
    return {m.group(1).decode("latin-1"): m.group(2).decode("latin-1") for m in ALIAS.finditer(data)}


def coff_members(path):
    """(name, bytes) of each COFF object in an archive (.lib), or of the object itself."""
    data = Path(path).read_bytes()
    if not data.startswith(b"!<arch>\n"):
        yield Path(path).name, data
        return
    pos = 8
    longnames = b""
    while pos + 60 <= len(data):
        header = data[pos:pos + 60]
        name = header[:16].decode("latin-1").strip()
        size = int(header[48:58].decode("latin-1").strip())
        body = data[pos + 60:pos + 60 + size]
        pos += 60 + size + (size & 1)
        if name == "//":
            longnames = body
            continue
        if name in ("/", ""):
            continue  # the linker members
        if name.startswith("/") and name[1:].isdigit():
            off = int(name[1:])
            end = longnames.find(b"\0", off)
            name = longnames[off:end if end >= 0 else None].decode("latin-1")
        if body[:4] == b"\0\0\xff\xff" and struct.unpack_from("<H", body, 4)[0] < 2:
            continue  # an import object
        yield name.rstrip("/"), body


def read_coff(path):
    """(sections, symbols) of a COFF object: sections as dicts with relocations, symbols as (name, value, section, type,
    storage class) by table index."""
    return read_coff_data(Path(path).read_bytes())


def read_coff_data(data):
    """read_coff over an object's bytes. Each section dict also carries "chars" (its characteristics), "raw" (its raw
    bytes, b"" when it has none) and, for a COMDAT section, "comdat": (selection, associated section number) from its
    section symbol's auxiliary record (tools/imagefree.py reads those; this tool doesn't)."""
    big = len(data) >= 8 and struct.unpack_from("<HH", data, 0) == (0, 0xFFFF)
    if big:
        nsec, symptr, nsym = struct.unpack_from("<III", data, 44)
        sechdr = 56
        symsize = 20
    else:
        nsec = struct.unpack_from("<H", data, 2)[0]
        symptr, nsym = struct.unpack_from("<II", data, 8)
        opt = struct.unpack_from("<H", data, 16)[0]
        sechdr = 20 + opt
        symsize = 18
    strtab = symptr + nsym * symsize

    def name_at(raw):
        if raw[:4] == b"\0\0\0\0":
            off = struct.unpack_from("<I", raw, 4)[0]
            end = data.index(b"\0", strtab + off)
            return data[strtab + off:end].decode("latin-1")
        return raw.rstrip(b"\0").decode("latin-1")

    sections = []
    for i in range(nsec):
        h = sechdr + i * 40
        raw_name = data[h:h + 8]
        _, _, rawsize, rawptr, relptr, _, nrel, _, chars = struct.unpack_from("<IIIIIIHHI", data, h + 8)
        relocs = []
        first = 0
        if chars & IMAGE_SCN_LNK_NRELOC_OVFL:
            nrel = struct.unpack_from("<I", data, relptr)[0]
            first = 1
        for r in range(first, nrel):
            va, sym, typ = struct.unpack_from("<IIH", data, relptr + r * 10)
            addend = struct.unpack_from("<I", data, rawptr + va)[0] if rawptr and va + 4 <= rawsize else 0
            relocs.append((va, sym, typ, addend))
        sections.append({"name": raw_name.rstrip(b"\0").decode("latin-1"), "relocs": relocs, "chars": chars,
                         "raw": data[rawptr:rawptr + rawsize] if rawptr else b""})

    symbols = {}
    i = 0
    while i < nsym:
        s = symptr + i * symsize
        if big:
            value, secnum, typ, cls, naux = struct.unpack_from("<IiHBB", data, s + 8)
        else:
            value, secnum, typ, cls, naux = struct.unpack_from("<IhHBB", data, s + 8)
        symbols[i] = (name_at(data[s:s + 8]), value, secnum, typ, cls)
        if cls == 3 and value == 0 and naux and 0 < secnum <= nsec and \
                sections[secnum - 1]["chars"] & IMAGE_SCN_LNK_COMDAT:
            # the section definition's auxiliary record: length, relocations, line numbers, checksum, number, selection
            number, selection = struct.unpack_from("<HB", data, s + symsize + 12)
            if big:
                number |= struct.unpack_from("<H", data, s + symsize + 16)[0] << 16
            sections[secnum - 1]["comdat"] = (selection, number)
        i += 1 + naux
    return sections, symbols


def owner_symbols(symbols):
    """Per section number, the named symbols that can own code or data in it, by value."""
    owners = defaultdict(list)
    for name, value, secnum, typ, cls in symbols.values():
        if secnum <= 0 or cls not in (2, 3) or name.startswith((".", "$", "??_C@")):
            continue
        owners[secnum].append((value, name))
    for v in owners.values():
        v.sort()
    return owners


def owner_of(owners, secnum, offset):
    best = None
    for value, name in owners.get(secnum, ()):
        if value <= offset:
            best = name
        else:
            break
    return best


def load_db():
    db = ROOT / "db"
    names, sizes, status, vtables = {}, {}, {}, {}
    for line in (db / "functions.jsonl").read_text().splitlines():
        if line.strip():
            f = json.loads(line)
            r = int(f["rva"], 16)
            sizes[r] = f.get("size") or 0
            names.setdefault(r, f.get("name"))
    for line in (db / "names.jsonl").read_text().splitlines():
        if line.strip():
            n = json.loads(line)
            names[int(n["rva"], 16)] = n["name"]
    for path in sorted((db / "status").glob("*.jsonl")):
        for line in path.read_text().splitlines():
            if line.strip():
                s = json.loads(line)
                if "rva" in s:
                    status[int(s["rva"], 16)] = (s.get("status"), path.stem)
    for line in (db / "classes.jsonl").read_text().splitlines():
        if line.strip():
            c = json.loads(line)
            for vt in c.get("vftables", []):
                vtables[int(vt["rva"], 16)] = c["name"]
    return names, sizes, status, vtables


def describe(rva, db):
    names, sizes, status, vtables = db
    if rva in vtables:
        return "vtable", f"vtable of {vtables[rva]}", 0, None
    if rva in sizes:
        st = status.get(rva)
        return "function", names.get(rva) or "", sizes[rva], st
    return "data", names.get(rva) or "", 0, status.get(rva)


def directive_object(directives):
    """A COFF object (i386) whose only section is a .drectve holding `directives`: linker options the link reads from an
    input object, as #pragma comment(linker) puts them (an MSBuild link cannot take a nested @response file)."""
    raw = directives.encode("ascii")
    header = struct.pack("<HHIIIHH", 0x14c, 1, 0, 0, 0, 0, 0)  # machine, sections, time, symbols at, count, opt, flags
    # .drectve: no address, its bytes after the headers, IMAGE_SCN_LNK_INFO | IMAGE_SCN_LNK_REMOVE | 1-byte alignment
    section = struct.pack("<8sIIIIIIHHI", b".drectve", 0, 0, len(raw), 20 + 40, 0, 0, 0, 0, 0x00100a00)
    return header + section + raw


def write_aliases(out, inputs, trap):
    """The response file of /alternatename:<image symbol>=<trap> lines (module doc). Returns the count."""
    undefined, defined, aliased = set(), set(), set()
    for item in inputs:
        item = Path(item)
        files = sorted(item.rglob("*.obj")) if item.is_dir() else [item]
        for f in files:
            for _, data in coff_members(f):
                aliased.update(aliases_in(data))
                _, symbols = read_coff_data(data)
                for name, value, secnum, typ, cls in symbols.values():
                    if cls not in (2, 105):
                        continue
                    if secnum > 0:
                        defined.add(name)
                    elif secnum == 0 and is_image_symbol(name):
                        undefined.add(name)
    names = sorted(undefined - defined - aliased)
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    if str(out).lower().endswith(".obj"):
        data = directive_object("".join(f" /alternatename:{n}={trap}" for n in names))
    else:
        data = "".join(f"/alternatename:{n}={trap}\n" for n in names).encode()
    if not Path(out).exists() or Path(out).read_bytes() != data:  # unchanged: leave it as it is
        Path(out).write_bytes(data)
    print(f"noimage: {len(names)} image symbol(s) aliased to {trap} ({out})")
    return len(names)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("map", nargs="?")
    ap.add_argument("objdirs", nargs="*")
    ap.add_argument("--write-aliases", metavar="RSP", help="write the alias response file over the inputs (map, objdirs)")
    ap.add_argument("--trap", default=TRAP_SYMBOL, help=f"the trap symbol (default {TRAP_SYMBOL})")
    ap.add_argument("--check", action="store_true", help="exit 1 when the trap is linked or any image reference is left")
    ap.add_argument("--delete", metavar="EXE", help="with --check: delete this file when the check fails")
    ap.add_argument("--log")
    ap.add_argument("--by-function", action="store_true", help="list each linked function's image references")
    ap.add_argument("--json", help="write the references as JSON lines here")
    ap.add_argument("--against", help="compare with this earlier result (a --json file) instead of the saved last run")
    ap.add_argument("--no-save", action="store_true", help="don't save this run as the last one (build/noimage-*.last.jsonl)")
    ap.add_argument("--gate", metavar="PROGRAM", help="report the post-build gate's count for this program from its map"
                    " (build/<config>/PROGRAM.map and its object directories); never fails, never saves")
    ap.add_argument("--config", default="Release", help="with --gate: the build configuration (default Release)")
    args = ap.parse_args()
    if args.write_aliases:
        write_aliases(args.write_aliases, [a for a in [args.map, *args.objdirs] if a], args.trap)
        return 0
    if args.gate:
        build = ROOT / "build"
        args.map = str(build / args.config / f"{args.gate}.map")
        args.objdirs = [str(tsdb.object_dir(build, t, args.config)) for t in ("ts2core", args.gate)]
        args.no_save, args.check, args.delete = True, False, None
        if not Path(args.map).exists():
            print(f"noimage gate {args.gate}: no map at {args.map} (build it: Invoke-Build {args.gate})")
            return 1
    if not args.map or not args.objdirs:
        ap.error("give a map and object directories (or --write-aliases)")

    text = Path(args.map).read_text(errors="replace")
    linked = defaultdict(set)  # object file name -> linked public names
    defined = set()
    for name, obj in MAP_LINE.findall(text):
        objname = obj.split(":")[-1].lower()
        linked[objname].add(name)
        # The map lists every trap-aliased image symbol as a public of the trap's object: those are not definitions
        # (lane QX, 2026-10-30: counting them hid every original function from a forced link's report).
        if objname != TRAP_OBJECT or name == args.trap:
            defined.add(name)

    db = load_db()
    refs = []  # (object, owner, kind, rva)
    objects = {}
    for d in args.objdirs:
        for p in Path(d).rglob("*.obj"):
            objects.setdefault(p.name.lower(), p)
    # Aliases the linked objects declare: an image symbol aliased to a definition the program links (not the trap) is
    # resolved by it.
    alias_to = {}
    for objname in linked:
        path = objects.get(objname)
        if path is not None:
            alias_to.update(aliases_in(path.read_bytes()))

    def resolved(name):
        target = alias_to.get(name)
        return name in defined or (target is not None and target != args.trap and target in defined)
    for objname, names_in in sorted(linked.items()):
        path = objects.get(objname)
        if path is None:
            continue
        sections, symbols = read_coff(path)
        owners = owner_symbols(symbols)
        for secidx, sec in enumerate(sections, start=1):
            for va, symidx, typ, addend in sec["relocs"]:
                target = symbols.get(symidx)
                if target is None:
                    continue
                owner = owner_of(owners, secidx, va)
                if owner is not None and owner not in names_in:
                    # a static (non-public) owner is not in the map's publics: keep it when its section has a
                    # public that was linked, else it was dropped by /OPT:REF
                    if not any(n in names_in for _, n in owners.get(secidx, ())):
                        continue
                tname = target[0]
                if tname == GAME_BASE_SYMBOL and typ in (IMAGE_REL_I386_DIR32, IMAGE_REL_I386_DIR32NB):
                    refs.append((objname, owner, "address", addend))
                elif tname == GAME_BASE_SYMBOL:
                    refs.append((objname, owner, f"address(reloc {typ:#x})", addend))
                elif OWNED_SYMBOL.match(tname) and tname != GAME_BASE_SYMBOL:
                    if not resolved(tname):  # an owned static the program has not defined yet
                        refs.append((objname, owner, "address", int(OWNED_SYMBOL.match(tname).group(1), 16) + addend))
                else:
                    rva = origsyms.decode(tname)
                    if rva is not None and not resolved(tname):
                        refs.append((objname, owner, "original", rva))

    by_rva = defaultdict(set)
    for obj, owner, kind, rva in refs:
        by_rva[(kind.split("(")[0], rva)].add(f"{obj}:{owner}")

    rows = []
    for (kind, rva), users in sorted(by_rva.items(), key=lambda kv: kv[0][1]):
        what, name, size, st = describe(rva, db)
        rows.append(json.dumps({"rva": hex(rva), "ref": kind, "what": what, "name": name, "size": size,
                                "status": st[0] if st else None, "users": sorted(users)}) + "\n")
    if args.json:
        with open(args.json, "w") as out:
            out.writelines(rows)
    last = ROOT / "build" / f"noimage-{Path(args.map).stem}.last.jsonl"
    previous = Path(args.against) if args.against else last
    previous_rows = ([json.loads(l) for l in previous.read_text().splitlines() if l.strip()]
                     if previous.exists() else None)

    if args.by_function:
        per = defaultdict(list)
        for obj, owner, kind, rva in refs:
            per[(obj, owner)].append((kind, rva))
        for (obj, owner), items in sorted(per.items()):
            print(f"{obj} {owner}")
            for kind, rva in sorted(set(items), key=lambda x: x[1]):
                what, name, size, st = describe(rva, db)
                print(f"    {kind:8} {rva:#09x} {what:8} {size:6} {name}" + (f" [{st[0]}]" if st else ""))

    totals = defaultdict(lambda: [0, 0])
    print(f"{'ref':8} {'rva':>9} {'what':8} {'size':>6} {'status':14} users  name")
    for (kind, rva), users in sorted(by_rva.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        what, name, size, st = describe(rva, db)
        totals[(kind, what)][0] += 1
        totals[(kind, what)][1] += size
        print(f"{kind:8} {rva:#09x} {what:8} {size:6} {(st[0] if st else '-'):14} {len(users):5}  {name}")
    print()
    for (kind, what), (n, size) in sorted(totals.items()):
        print(f"{kind:8} {what:8} {n:5} distinct, {size} bytes")

    if args.log:
        other = defaultdict(set)
        for m in UNRESOLVED.finditer(Path(args.log).read_text(errors="replace")):
            sym = m.group(1) or m.group(2)
            if OWNED_SYMBOL.match(sym) or origsyms.decode(sym) is not None:
                continue
            other[sym].add(m.group(3) or m.group(4) or "")
        if other:
            print("\nother unresolved symbols:")
            for sym, users in sorted(other.items()):
                print(f"  {sym}  <- {', '.join(sorted(users))}")

    print()
    if previous_rows is None:
        print(f"against the previous run: none at {previous} (this run is the baseline)")
    else:
        before = {(r["ref"], int(r["rva"], 16)): r for r in previous_rows}
        now = {(r["ref"], int(r["rva"], 16)): r for r in map(json.loads, rows)}
        removed = sorted(before.keys() - now.keys(), key=lambda k: k[1])
        exposed = sorted(now.keys() - before.keys(), key=lambda k: k[1])
        print(f"against the previous run ({previous}): {len(before)} -> {len(now)} distinct references, "
              f"{len(removed)} removed, {len(exposed)} exposed")
        for label, keys, src in (("removed", removed, before), ("exposed", exposed, now)):
            for k in keys:
                r = src[k]
                users = r.get("users") or []
                print(f"  {label:8} {k[0]:8} {k[1]:#09x} {r.get('what') or '':8} {r.get('size') or 0:6} {r.get('name')}"
                      + (f"  <- {users[0]}{f' (+{len(users) - 1})' if len(users) > 1 else ''}" if label == "exposed"
                         and users else ""))
    if args.gate:
        rvas = sorted({int(json.loads(r)["rva"], 16) for r in rows})
        print(f"noimage gate {args.gate}: {len(rows)} image reference(s)"
              + (f" at {', '.join(hex(r) for r in rvas)}" if rvas else "")
              + f"; the trap {'is' if args.trap in defined else 'is not'} linked")
    if not args.no_save:
        last.parent.mkdir(exist_ok=True)
        last.write_text("".join(rows))
    if args.check:
        problems = []
        if args.trap in defined:
            problems.append(f"the trap {args.trap} is linked: live code refers to the image")
        if rows:
            problems.append(f"{len(rows)} image reference(s) left in linked code (listed above)")
        if problems:
            for problem in problems:
                print(f"NOIMAGE CHECK FAILED: {problem}")
            if args.delete and Path(args.delete).exists():
                Path(args.delete).unlink()
                print(f"NOIMAGE CHECK: deleted {args.delete}")
            return 1
        print("NOIMAGE CHECK OK: no image reference in linked code, the trap not linked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
