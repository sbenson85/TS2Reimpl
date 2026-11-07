"""Identifies statically linked library functions (CRT, vcruntime, STL) in the game exe by matching
them byte-for-byte against the functions in the local MSVC 14.40 / Windows SDK .lib files.

The game was built with the same toolset, so library code is identical apart from relocated operands.
For each COFF function, bytes covered by its relocations are masked; a game function matches if every
unmasked byte is equal. Matches go to db/library-matches.jsonl: {"rva", "names": [decorated names],
"lib", "objects"}; several names mean identical code (folded or identical instantiations).
Only names and addresses are recorded.

Kinds of match (the row's "kind"; absent means exact):
  exact     the library body (or the body cut before its jump table) is as long as the function's size in the db.
  prefix    the whole library body matches at a function start, but the db sizes the function differently (Ghidra
            stopped early, e.g. before a __finally funclet or after a call it thinks never returns, or ran on into the
            next function). "size" is the library body's length, "ghidra_size" db/functions.jsonl's.
  unlisted  the whole library body matches at an address that is no function start in the db; found because a
            function start lies inside it (a funclet or tail Ghidra split off). "container": the db function holding it.
  gap       the whole library body matches in code no db function covers (a function Ghidra never made, reached only
            through a pointer table); "size" is the body's length. --boundaries lists it as a function to create.
  inside    a db function start inside a prefix/unlisted/exact match: a piece of library code (funclet, split tail).
            "in": the match's rva, "offset": the distance; "names" is empty, "piece_of" holds the match's names.
Non-exact matches must be MIN_SIZE bytes or longer, and under SHORT_PREFIX bytes unique both ways; the masking
rules are the same for all kinds (only relocation fields are wildcards).

Relocations (the strict matching above never looks at them; this step only reads them): every relocation of a
matched library function names its target, so the game's operand at the same offset says where that target is in the
game. A candidate name of the row is consistent when each relocation lands where it should: a label in its own
section lands at the same offset in the game function, one symbol lands at one address, and a target that is itself
a library match (or a db/crt-equivalents row) carries the symbol's name. Recorded per row:
  names_by_relocs  the candidate names that are consistent, when relocations rule some (not all) of them out
                   (e.g. 0x836de0 matches strtol and wcstol byte-for-byte; the callee decides).
  family: true     the bytes match but the name is a label for a family of instantiations: no candidate is consistent,
                   or the only names use tools/stlprobe's stand-in element types (Blob4<N>). Its "calls" say what
                   this instantiation really calls.
  calls            [{"off", "target", "symbols"}]: code targets the relocations name but no library match confirms
                   (game code such as an element destructor, or library code under another name). These name the
                   targets (`--relocation-names`), and for a family label they are the per-instantiation targets.

Functions under MIN_SIZE bytes match unrelated code by accident (generic getters), so they're kept
only when unique both ways: one game function has those bytes and one library name produces them.
Inputs can be .lib archives or single .obj files (e.g. a probe instantiating STL templates).

STL templates: libcpmt holds only the non-template parts, so tools/stlprobe/stlprobe.ps1 compiles probe sources that
instantiate the templates the game uses (named element types, N-byte stand-ins, iostreams, the game's allocator-service
allocator's stand-in) under each flag set EA's translation units used; see its header. Names using a stand-in are
family labels (STAND_IN). A probe's deleting destructors (??_G/??_E) are kept only when their relocations confirm them.
The default inputs take the probe objects from the main checkout's local/stlprobe; libmatch warns when they were not
built from tools/stlprobe as it is now (sources.sha256): rebuild them before writing rows.

Usage: python -I tools/libmatch.py [--dry-run] [.lib/.obj files or directories of .obj...]   (default: the CRT/STL libs below)
         --dry-run reports counts without writing db/library-matches.jsonl (--out FILE: the rows go there instead).
         --stlprobe DIR (any mode): the probe objects from DIR (a lane's stlprobe.ps1 -Out DIR) instead of local/stlprobe;
                                    a DIR built with -Only or -Missing lists its base directory in base.txt, whose other
                                    objects are read too, so a lane copies nothing.
       python -I tools/libmatch.py --add-objects DIR|OBJ... [--dry-run] [--out FILE]   index only these objects, and append or
                                    extend db/library-matches.jsonl with what they match; every other row stays as it is
                                    (add_objects() says how it differs from a full run). The lane's way to record rows.
       python -I tools/libmatch.py [--stlprobe DIR] --write-stamp   records the probe sources' hash (stlprobe.ps1 runs it)
       python -I tools/libmatch.py --callees 0xRVA        relocation sites of the match at RVA: offset, symbol, game target
       python -I tools/libmatch.py --relocation-names [--json|--all] targets named by the relocations of matched callers
       python -I tools/libmatch.py --boundaries [--json]  function-boundary fixes for Ghidra the matches imply
       python -I tools/libmatch.py --score INPUT... [--range LO HI] [--list|--json]   what these inputs alone match
       python -I tools/libmatch.py --cover LO HI [--rows FILE] [--limit N]  db functions in a range no match covers
       python -I tools/libmatch.py --near INPUT... (--range LO HI | --rvas FILE) [--min F] [--full]  closest db function per
                                                                        unplaced library body (for --diff)
       python -I tools/libmatch.py --diff INPUT SYMBOL RVA [--lines N]  library body next to the game's, from the first
                                                                        difference outside the relocation mask
"""
import bisect
import json
import struct
import sys
from array import array
from collections import defaultdict, namedtuple
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tsdb  # noqa: E402

LOCAL = tsdb.main_root() / "local"  # local/ lives in the main checkout, also when run from a worktree
MSVC = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.40.33807\lib\x86")
SDK = Path(r"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x86")
DEFAULT_LIBS = [MSVC / "libcmt.lib", MSVC / "libvcruntime.lib", MSVC / "libcpmt.lib", MSVC / "libconcrt.lib", SDK / "libucrt.lib"]
# The legacy stdio libraries: the image has legacy_stdio_wide_specifiers' XI entry (lane HQ) and the inline printf /
# scanf wrappers of legacy_stdio_definitions.
DEFAULT_LIBS += [MSVC / "legacy_stdio_wide_specifiers.lib", MSVC / "legacy_stdio_definitions.lib"]
# EA's UCRT is Windows SDK 10.0.22621's (lane IA: 319 functions more than 26100's, which still matches 3 of its own):
# both are inputs. Fetched into local/ by tools/sdkucrt.py --fetch (Microsoft's NuGet package; not committed).
import sdkucrt  # noqa: E402
if not sdkucrt.lib_path().exists():
    sys.exit(f"missing {sdkucrt.lib_path()}: run tools/py -I tools/sdkucrt.py --fetch {sdkucrt.UCRT_VERSION}")
DEFAULT_LIBS += [sdkucrt.lib_path()]
# STL template instantiations compiled by tools/stlprobe/stlprobe.ps1 (the game's STL code matches /O1, some of it /O2).
# --stlprobe DIR (any mode) takes them from DIR instead, e.g. a lane's own build (stlprobe.ps1 -Out DIR).
STLPROBE = LOCAL / "stlprobe"
if "--stlprobe" in sys.argv:
    _i = sys.argv.index("--stlprobe")
    STLPROBE = Path(sys.argv[_i + 1]).resolve()
    del sys.argv[_i:_i + 2]


def stlprobe_objects(directory):
    """The probe objects of a directory: its own, plus those of the directory base.txt names (stlprobe.ps1 -Only or
    -Missing with an -Out of its own: the lane built a few objects, the rest are the main checkout's). Own objects win."""
    objs = {p.name: p for p in directory.glob("probe*.obj")}
    base = directory / "base.txt"
    if base.exists():
        objs = {**{p.name: p for p in Path(base.read_text().strip()).glob("probe*.obj")}, **objs}
    return [objs[k] for k in sorted(objs)]


DEFAULT_LIBS += stlprobe_objects(STLPROBE)


def stlprobe_hash():
    """Hash of tools/stlprobe's sources and build script (line ends normalised): what the probe objects come from."""
    import hashlib
    h = hashlib.sha256()
    for p in sorted([*(ROOT / "tools/stlprobe").glob("*.cpp"), *(ROOT / "tools/stlprobe").glob("*.h"),
                     ROOT / "tools/stlprobe/stlprobe.ps1"]):
        h.update(p.name.encode() + b"\0" + p.read_bytes().replace(b"\r\n", b"\n"))
    return h.hexdigest()


def stlprobe_stale():
    """Whether the probe objects were built from other sources than tools/stlprobe's now (stlprobe.ps1 writes
    stlprobe_hash() to sources.sha256 next to them): rows from stale objects drop or add matches nobody asked for."""
    stamp = STLPROBE / "sources.sha256"
    return not stamp.exists() or stamp.read_text().strip() != stlprobe_hash()


# Embedded open-source libraries: tools/thirdparty_build.py's recipes (EA's builds: patched Lua, OpenSSL and expat by an
# older compiler, the hotpatch zlib/libpng copy), each taken when its objects exist; and tools/thirdparty.ps1 builds at
# the flags that match best, minus the ones a built recipe supersedes.
import thirdparty_build  # noqa: E402
SUPERSEDED = {"lua50p": ("lua50", "O2"), "libpng105b": ("libpng105", "O1OiGS")}
_built = {name for name in thirdparty_build.RECIPES if any(thirdparty_build.out_dir(name).glob("*.obj"))}
DEFAULT_LIBS += thirdparty_build.default_objs()
# Statically linked D3DX9 and DxErr9 from the DirectX 9.0 SDK (Summer 2004), fetched by thirdparty_build.py --fetch-dxsdk.
DEFAULT_LIBS += thirdparty_build.dxsdk_libs()
for name, flags in [("lua50", "O2"), ("zlib114", "O1"), ("zlib113", "O1"), ("zlib113", "O1OiGS"), ("jpeg6a", "O1OiGS"), ("libpng105", "O1OiGS"), ("speex103", "O1OiGS")]:
    if (name, flags) not in {SUPERSEDED[b] for b in _built if b in SUPERSEDED}:
        DEFAULT_LIBS += sorted((LOCAL / "thirdparty/obj" / f"{name}-{flags}").glob("*.obj"))

IMAGE_SCN_CNT_CODE = 0x20
MIN_SIZE = 16  # below this, identical bytes across unrelated functions are common
MIN_SIZE_UNIQUE = 8  # 8..15-byte functions are accepted only when the match is unique both ways
SHORT_PREFIX = 32  # non-exact matches under this length are accepted only when unique both ways
PREFIX = 8  # bytes of a library body indexed for the prefix and window searches
PROGRESS = 1000  # objects indexed between progress lines
REL32, DIR32, DIR32NB = 0x14, 0x06, 0x07
RELOC_SIZES = {0x06: 4, 0x07: 4, 0x14: 4, 0x0A: 2, 0x0B: 4}  # DIR32, DIR32NB, REL32, SECTION, SECREL
# tools/stlprobe's own types (probe_sizes.cpp's Blob4<N>, probe.cpp's Blob<N>, ProbeEval and comparators, ProbeAlloc
# standing in for the game's allocator-service allocator): never in the game, so a name using one is a label for
# whatever game type has the same size (or allocator).
STAND_IN = ("Blob4", "?$Blob@", "ProbeEval", "NvtLess", "NameEvalLess", "ProbeAlloc")
GENERIC = ("??__E", "??__F")  # dynamic initialisers / atexit destructors: kept only when every relocation confirms them
# Deleting destructors (call the destructor, then free when asked) have the shape of every other class's: a probe's
# are kept only when the code they call is that library code, not just operator delete (lane MJ; before, probe
# template classes' were never indexed, which also lost the game's basic_ifstream / basic_ofstream ones, and other
# probe classes' were kept unchecked).
PROBE_GENERIC = ("??_G", "??_E")
OPERATOR_DELETE = ("??3@", "??_V@")

# A relocation of a library function. off: offset in the body; sym: the target's symbol (None for anonymous data);
# addend: the field's value in the object (target = symbol + addend); local: the target as an offset from the
# function's start when it is in the function's own section (a jump table entry, a funclet), else None.
Reloc = namedtuple("Reloc", "off type sym addend local")

# Objects from tools/thirdparty.ps1 and thirdparty_build.py builds are reported under their library's name (keyed by path:
# several builds have a png.obj).
THIRDPARTY_OF = {}


def archive_members(data):
    """Yields (name, bytes) for each member of a .lib archive."""
    assert data[:8] == b"!<arch>\n"
    pos, longnames = 8, b""
    while pos + 60 <= len(data):
        hdr = data[pos:pos + 60]
        name = hdr[:16].decode("latin-1").strip()
        size = int(hdr[48:58].decode().strip())
        body = data[pos + 60:pos + 60 + size]
        if name == "//":
            longnames = body
        elif name != "/":
            if name.startswith("/") and name[1:].isdigit() and longnames:
                off = int(name[1:])
                end = longnames.find(b"\0", off)
                name = longnames[off:end if end >= 0 else None].decode("latin-1")
            yield name, body
        pos += 60 + size + (size & 1)


def coff_functions(obj, with_relocs=False):
    """Yields (symbol name, code bytes, masked offsets) for each function in a COFF object, plus its relocations
    (a tuple of Reloc) when with_relocs is set."""
    if len(obj) < 20:
        return
    machine, nsect, _, symptr, nsyms, optsize, _ = struct.unpack_from("<HHIIIHH", obj, 0)
    if machine != 0x14C or symptr == 0:
        return
    sections = []
    for i in range(nsect):
        o = 20 + optsize + 40 * i
        _, _, _, rawsize, rawptr, relptr, _, nrel, _, chars = struct.unpack_from("<8sIIIIIIHHI", obj, o)
        sections.append((rawsize, rawptr, relptr, nrel, chars))
    strtab = symptr + 18 * nsyms

    def symname(o):
        if obj[o:o + 4] == b"\0\0\0\0":
            off = struct.unpack_from("<I", obj, o + 4)[0]
            end = obj.find(b"\0", strtab + off)
            return obj[strtab + off:end].decode("latin-1")
        return obj[o:o + 8].rstrip(b"\0").decode("latin-1")

    funcs = defaultdict(list)  # section index -> [(value, name)]
    i = 0
    while i < nsyms:
        o = symptr + 18 * i
        # Section numbers are unsigned up to 0xFEFF (0xFFFF absolute, 0xFFFE debug): read signed, an object with over
        # 32767 sections (a big probe) lost the functions in its upper sections (lane MJ).
        value, secnum, typ, cls, naux = struct.unpack_from("<IHHBB", obj, o + 8)
        if 0 < secnum <= nsect and (typ & 0xF0) == 0x20 and cls in (2, 3):
            funcs[secnum - 1].append((value, symname(o)))
        i += 1 + naux
    func_at = {(sec, value): name for sec, syms in funcs.items() for value, name in syms}

    def reloc_target(symidx, sec, va, value):
        """(symbol name, addend, local offset) of a relocation at va (section offset) in function `value`."""
        o = symptr + 18 * symidx
        svalue, secnum, typ, cls, _ = struct.unpack_from("<IHHBB", obj, o + 8)
        name = symname(o)
        rawptr = sections[sec][1]
        addend = struct.unpack_from("<i", obj, rawptr + va)[0]
        if secnum == sec + 1:
            return name, addend, svalue + addend - value
        if 0 < secnum <= len(sections) and cls == 3 and svalue == 0 and (typ & 0xF0) != 0x20:
            # A section symbol (a static function in another COMDAT, or data): name the function at the offset.
            fn = func_at.get((secnum - 1, addend))
            return (fn, 0, None) if fn else (None, addend, None)
        return name, addend, None

    for sec, syms in funcs.items():
        rawsize, rawptr, relptr, nrel, chars = sections[sec]
        if not chars & IMAGE_SCN_CNT_CODE or rawptr == 0:
            continue
        code = obj[rawptr:rawptr + rawsize]
        masked = set()
        relocs = []
        for r in range(nrel):
            va, symidx, rtype = struct.unpack_from("<IIH", obj, relptr + 10 * r)
            masked.update(range(va, va + RELOC_SIZES.get(rtype, 4)))
            relocs.append((va, symidx, rtype))
        syms.sort()
        for k, (value, name) in enumerate(syms):
            end = syms[k + 1][0] if k + 1 < len(syms) else len(code)
            body = code[value:end]
            # Trailing int3/nop padding isn't part of the function as Ghidra sizes it.
            while body and body[-1] in (0xCC, 0x90) and len(body) > 1:
                body = body[:-1]
            mask = frozenset(m - value for m in masked if value <= m < value + len(body))
            if not with_relocs:
                yield name, body, mask
                continue
            mine = []
            for va, symidx, rtype in relocs:
                if value <= va and va + 4 <= value + len(body) and rtype in (REL32, DIR32, DIR32NB):
                    sym, addend, local = reloc_target(symidx, sec, va, value)
                    mine.append(Reloc(va - value, rtype, sym, addend, local))
            yield name, body, mask, tuple(sorted(mine))


def jump_table_cuts(body, mask):
    """Lengths at which a function with a switch jump table placed after its code may end in the game:
    Ghidra sizes the function without the table (a run of relocated 4-byte entries at the end of the
    object's function), so the library body is also indexed cut before that run, and cut after the last
    `ret` before it (a byte index map may precede the table)."""
    end = len(body)
    while end >= 4 and all(i in mask for i in range(end - 4, end)):
        end -= 4
    if end == len(body):
        return []
    cuts = [end]
    p = body.rfind(b"\xc3", 0, end)
    if p >= 0 and end - (p + 1) <= 512:
        cuts.append(p + 1)
    return [c for c in cuts if c >= MIN_SIZE_UNIQUE]


class Library:
    """The library functions, indexed by exact length (as before) and, for MIN_SIZE and longer, as unique bodies
    for the prefix and window searches. An entry is (decorated name, index into self.paths, object name)."""

    def __init__(self):
        self.paths = []
        self.exact = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))  # length -> mask -> key -> [entry]
        self.ids = {}  # (masked bytes, mask) -> body id
        self.keys, self.masks, self.entries = [], [], []
        self.total = 0
        self.defined = set()  # every function symbol with a body in the inputs
        self.big = defaultdict(set)  # name -> body ids (MIN_SIZE and longer)
        self.small = defaultdict(list)  # name -> [(masked bytes, mask)] of shorter bodies
        self._segments = {}
        self.objects = 0

    def add_path(self, path):
        data = path.read_bytes()
        if path.parent.parent.name == "obj" and path.parent.parent.parent.name == "thirdparty":
            THIRDPARTY_OF[str(path)] = path.parent.name.split("-")[0]
        elif path.parent.parent.name == "thirdparty" and path.parent.name.startswith("dxsdk-"):
            THIRDPARTY_OF[str(path)] = f"{path.stem.lower()}-{path.parent.name[6:]}"  # d3dx9-sum2004, dxerr9-sum2004
        self.paths.append(path)
        members = archive_members(data) if data[:8] == b"!<arch>\n" else [(path.name, data)]
        for objname, obj in members:
            self.add_object(len(self.paths) - 1, objname.split("\\")[-1], obj)

    def is_probe(self, pathidx):
        return self.paths[pathidx].name.startswith("probe")

    def add_object(self, pathidx, objname, obj):
        self.objects += 1
        if self.objects % PROGRESS == 0:
            print(f"indexed {self.objects} objects, {self.total} functions", flush=True)
        for name, body, mask in coff_functions(obj):
            self.defined.add(name)
            full = bytes(0 if i in mask else b for i, b in enumerate(body))
            if len(body) < MIN_SIZE:
                # Kept by name for the relocation check (is a callee this symbol's body?), e.g. 5-byte `jmp free`.
                if (full, mask) not in self.small[name]:
                    self.small[name].append((full, mask))
            if len(body) < MIN_SIZE_UNIQUE:
                continue  # short functions match unrelated code too easily
            entry = (name, pathidx, objname)
            for n in [len(body)] + jump_table_cuts(body, mask):
                m = frozenset(i for i in mask if i < n) if n != len(body) else mask
                self.exact[n][m][full[:n]].append(entry)
            if len(body) >= MIN_SIZE:
                bid = self.ids.get((full, mask))
                if bid is None:
                    bid = self.ids[(full, mask)] = len(self.keys)
                    self.keys.append(full)
                    self.masks.append(mask)
                    self.entries.append([])
                self.entries[bid].append(entry)
                self.big[name].add(bid)
            self.total += 1

    def matches_at(self, name, image, at):
        """Whether one of the bodies named `name` is at `at` in the image (relocations masked)."""
        if any(at + len(self.keys[bid]) <= len(image) and self.body_at(bid, image, at) for bid in self.big.get(name, ())):
            return True
        return any(at + len(full) <= len(image) and all(i in mask or image[at + i] == b for i, b in enumerate(full))
                   for full, mask in self.small.get(name, ()))

    def segments(self, bid):
        """The unmasked runs (start, end) of a body, for comparing it with the image."""
        seg = self._segments.get(bid)
        if seg is None:
            mask, n, seg, start = self.masks[bid], len(self.keys[bid]), [], None
            for i in range(n + 1):
                if i < n and i not in mask:
                    start = i if start is None else start
                elif start is not None:
                    seg.append((start, i))
                    start = None
            seg = self._segments[bid] = seg
        return seg

    def body_at(self, bid, image, at):
        key = self.keys[bid]
        return all(image[at + a:at + b] == key[a:b] for a, b in self.segments(bid))

    def names(self, bid):
        return {n for n, _, _ in self.entries[bid]}


def match_exact(lib, image, sizes):
    """The original rule: the library body (or a jump-table cut of it) as long as the function. sizes: rva -> set of
    lengths (Ghidra's and the merged db's). Returns rva -> [entries]."""
    candidates, hits_per_key = [], defaultdict(set)
    for rva, lengths in sizes.items():
        found = []
        for size in sorted(lengths):
            if size not in lib.exact:
                continue
            body = image[rva:rva + size]
            for mask, table in lib.exact[size].items():
                key = bytes(0 if i in mask else b for i, b in enumerate(body))
                if key in table:
                    found.append((table[key], (size, mask, key)))
                    hits_per_key[(size, mask, key)].add(rva)
        if found:
            candidates.append((rva, found))
    out = {}
    for rva, found in candidates:
        # A short body that the toolchain's libraries and an embedded library both define is the toolchain's (the
        # 8-byte __crt_debugger_hook is also D3DX's NXOF::YaccResetEof): the embedded name doesn't count against it.
        found = [([e for e in entries if not THIRDPARTY_OF.get(str(lib.paths[e[1]]))]
                  if k[0] < MIN_SIZE and any(not THIRDPARTY_OF.get(str(lib.paths[e[1]])) for e in entries) else entries, k)
                 for entries, k in found]
        # Short: keep only matches unique both ways (one game function, one library name).
        found = [(entries, k) for entries, k in found
                 if k[0] >= MIN_SIZE or (len(hits_per_key[k]) == 1 and len({n for n, _, _ in entries}) == 1)]
        entries = [e for entries, _ in found for e in entries]
        if entries:
            out[rva] = (max(k[0] for _, k in found), entries)
    return out


def prefix_index(lib):
    """andmask -> first PREFIX bytes (masked, little-endian int) -> [body ids]."""
    groups = defaultdict(lambda: defaultdict(list))
    for bid, key in enumerate(lib.keys):
        mask = lib.masks[bid]
        andmask = sum(0xFF << (8 * i) for i in range(PREFIX) if i not in mask)
        groups[andmask][int.from_bytes(key[:PREFIX], "little")].append(bid)
    return groups


def match_prefix(lib, image, starts, hi, groups=None):
    """Library bodies matching whole at each start, whatever the function's size. Returns [(rva, body id)]."""
    groups = groups or prefix_index(lib)
    out = []
    for rva in starts:
        if rva + PREFIX > hi:
            continue
        v = int.from_bytes(image[rva:rva + PREFIX], "little")
        for andmask, table in groups.items():
            for bid in table.get(v & andmask, ()):
                if rva + len(lib.keys[bid]) <= hi and lib.body_at(bid, image, rva):
                    out.append((rva, bid))
    return out


def match_windows(lib, image, starts, lo, hi, chunk=1 << 20):
    """Library bodies matching whole at an address before a start, the start being N >= 1 bytes into the body (a
    funclet or a split-off tail whose function's start the db lacks). Returns [(rva of the body, body id)]."""
    want = defaultdict(list)
    for s in starts:
        if s + PREFIX <= hi:
            want[int.from_bytes(image[s:s + PREFIX], "little")].append(s)
    offsets, total = [], 0
    for key in lib.keys:
        offsets.append(total)
        total += len(key)
    buf = b"".join(lib.keys)
    wanted, hits = set(want), set()
    for c in range(0, len(buf), chunk):
        seg = buf[c:c + chunk + PREFIX - 1]
        for a in range(PREFIX):
            part = seg[a:]
            vals = array("Q")
            vals.frombytes(part[:len(part) // 8 * 8])
            hits |= wanted.intersection(vals)
    out = set()
    for v in hits:
        pat = v.to_bytes(PREFIX, "little")
        pos = buf.find(pat)
        while pos >= 0:
            bid = bisect.bisect_right(offsets, pos) - 1
            n = pos - offsets[bid]
            if n >= 1 and pos + PREFIX <= offsets[bid] + len(lib.keys[bid]):
                for s in want[v]:
                    at = s - n
                    if lo <= at and at + len(lib.keys[bid]) <= hi and lib.body_at(bid, image, at):
                        out.add((at, bid))
            pos = buf.find(pat, pos + 1)
    return sorted(out)


def match_aligned(lib, image, sizes, starts, hi, groups):
    """Library bodies matching whole at the 16-byte boundary before a start, in code no db function covers. match_windows
    looks up the 8 bytes at the start, so it misses a start whose bytes hold a relocated operand (0x86c12, in D3DX's
    CNodeBuffer::Copy from 0x86c10; 0xc4b60f, a 5-byte call in __rtforcshinf from 0xc4b600; both starts came from Binary
    Ninja). sizes: rva -> lengths, as in locate. Returns [(rva of the body, body id)]."""
    known = sorted(sizes)
    cands = set()
    for s in starts:
        at = s & ~15
        i = bisect.bisect_right(known, at) - 1
        if at != s and at not in sizes and (i < 0 or known[i] + max(sizes[known[i]]) <= at):
            cands.add(at)
    return [(at, bid) for at, bid in match_prefix(lib, image, sorted(cands), hi, groups)]


def code_gaps(funcs, lo, hi):
    """(start, end) of the stretches of [lo, hi) no db function covers."""
    spans = sorted((int(f["rva"], 16), int(f["rva"], 16) + f["size"]) for f in funcs)
    out, pos = [], lo
    for a, b in spans:
        if a > pos:
            out.append((pos, min(a, hi)))
        pos = max(pos, b)
    if pos < hi:
        out.append((pos, hi))
    return [(a, b) for a, b in out if a < b]


def match_gaps(lib, image, funcs, lo, hi, groups=None):
    """Library bodies matching whole inside code no db function covers (functions Ghidra never made, e.g. ones reached
    only through a table: lane IE's deflate_fast/deflate_slow, expat's xmlrole states). Candidate starts: a gap's start,
    16-byte boundaries and the byte after an int3 or a ret. Returns [(rva, body id)] lying wholly inside one gap."""
    out = []
    for a, b in code_gaps(funcs, lo, hi):
        if b - a < MIN_SIZE:
            continue
        cands = {a} | {p for p in range((a + 15) & ~15, b, 16)}
        cands |= {p for p in range(a + 1, b) if image[p - 1] in (0xCC, 0xC3)}
        out += [(at, bid) for at, bid in match_prefix(lib, image, sorted(cands), b, groups)]
    return out


def accept_placements(lib, placements):
    """Non-exact placements kept: MIN_SIZE or longer; under SHORT_PREFIX only when the body is found at one address
    and has one name. Per address the longest body wins (all bodies of that length give names).
    Returns rva -> (length, [body ids])."""
    where = defaultdict(set)
    for at, bid in placements:
        where[bid].add(at)
    best = {}
    for at, bid in placements:
        n = len(lib.keys[bid])
        if n < MIN_SIZE or (n < SHORT_PREFIX and (len(where[bid]) > 1 or len(lib.names(bid)) > 1)):
            continue
        cur = best.get(at)
        if cur is None or n > cur[0]:
            best[at] = (n, [bid])
        elif n == cur[0] and bid not in cur[1]:
            cur[1].append(bid)
    return best


GAP_NEIGHBOUR = 0x10000


def drop_stray_gaps(lib, rows):
    """A gap row naming only an embedded open-source library's functions is kept when a non-gap row of the same library
    lies within GAP_NEIGHBOUR bytes: such libraries are linked as one block, and a lone small C function in a gap
    elsewhere is game code of the same shape (0xc6514a matched zlib's gzputs, a strlen-and-call wrapper calling a game
    function). CRT and STL bodies are inlined and instantiated anywhere, so they are not checked."""
    def labels(row):
        tp = {THIRDPARTY_OF.get(str(lib.paths[p])) for _, p, _ in row["entries"]}
        return None if None in tp else tp
    anchors = defaultdict(list)
    for at, row in rows.items():
        if row["kind"] in ("exact", "prefix"):
            for t in labels(row) or ():
                anchors[t].append(at)
    for t in anchors:
        anchors[t].sort()
    for at, row in list(rows.items()):
        if row["kind"] != "gap":
            continue
        tp = labels(row)
        if tp is None:
            continue
        near = False
        for t in tp:
            a = anchors.get(t, [])
            i = bisect.bisect_left(a, at)
            near |= any(abs(a[j] - at) <= GAP_NEIGHBOUR for j in (i - 1, i) if 0 <= j < len(a))
        if not near:
            del rows[at]


def locate(lib, image, funcs, lo, hi, log=print):
    """All matches. funcs: [{"rva", "size", ...}] as tsdb.functions(); raw: rva -> Ghidra's size
    (db/functions.jsonl). Returns rva -> row dict (without names formatting) and the conflicts seen."""
    sizes = defaultdict(set)
    for f in funcs:
        sizes[int(f["rva"], 16)].add(f["size"])
    starts = sorted(sizes)
    exact = match_exact(lib, image, sizes)
    log(f"exact: {len(exact)}")
    groups = prefix_index(lib)
    log(f"prefix index: {len(lib.keys)} distinct bodies, {len(groups)} prefix masks")
    rest = [s for s in starts if s not in exact]
    placed = accept_placements(lib, match_prefix(lib, image, rest, hi, groups))
    log(f"prefix: {len(placed)} starts")
    # Starts inside a placed (or exact) body are pieces of it, not functions of their own.
    spans = sorted([(at, at + n) for at, (n, _) in placed.items()] + [(r, r + n) for r, (n, _) in exact.items()])
    covered = set()
    for a, b in spans:
        i = bisect.bisect_right(starts, a)
        while i < len(starts) and starts[i] < b:
            covered.add(starts[i])
            i += 1
    rest = [s for s in starts if s not in exact and s not in placed and s not in covered]
    windows = accept_placements(lib, [(at, bid) for at, bid in match_windows(lib, image, rest, lo, hi)
                                      if at not in exact and at not in placed])
    log(f"windows: {len(windows)} bodies before a start")
    windows.update(accept_placements(lib, [(at, bid) for at, bid in match_aligned(lib, image, sizes, rest, hi, groups)
                                           if at not in windows]))
    gaps = accept_placements(lib, match_gaps(lib, image, funcs, lo, hi, groups))
    log(f"gaps: {len(gaps)} bodies where the db has no function")
    rows, conflicts = {}, []
    for rva, (n, entries) in exact.items():
        rows[rva] = {"kind": "exact", "length": n, "entries": entries}
    # Longest first, so a body inside a longer one becomes its piece, not a match of its own.
    order = sorted(list(placed.items()) + list(windows.items()) + list(gaps.items()), key=lambda kv: (-kv[1][0], kv[0]))
    taken = []  # (start, end) of accepted non-exact matches
    for at, (n, bids) in order:
        k = bisect.bisect_right(taken, (at, float("inf"))) - 1
        if k >= 0 and taken[k][0] < at < taken[k][1] or at in rows:
            continue  # inside an accepted match (or an exact match there)
        kind = "prefix" if at in sizes else "gap" if at in gaps and at not in windows else "unlisted"
        rows[at] = {"kind": kind, "length": n, "entries": [e for b in bids for e in lib.entries[b]], "bids": bids}
        bisect.insort(taken, (at, at + n))
    drop_stray_gaps(lib, rows)
    for at, row in list(rows.items()):
        i = bisect.bisect_right(starts, at)
        while i < len(starts) and starts[i] < at + row["length"]:
            s = starts[i]
            if s in rows and rows[s]["kind"] in ("exact", "prefix", "unlisted"):
                conflicts.append((at, s))
            elif s not in rows:
                rows[s] = {"kind": "inside", "in": at, "offset": s - at}
            i += 1
    return rows, conflicts


def reloc_tables(lib, rows):
    """(name, path index, object) -> relocations, for the candidates of the matched rows (objects parsed again). Every
    object counts: one name can have several bodies with different targets (libcmt's __scrt_common_main_seh calls
    main, WinMain or wWinMain depending on the object)."""
    need = defaultdict(set)
    for row in rows.values():
        for name, p, obj in row.get("entries", ()):
            need[(p, obj)].add(name)
    out, by_path = {}, defaultdict(dict)
    for p, obj in need:
        by_path[p][obj] = need[(p, obj)]
    for p, objs in by_path.items():
        path = lib.paths[p]
        data = path.read_bytes()
        members = archive_members(data) if data[:8] == b"!<arch>\n" else [(path.name, data)]
        for objname, obj in members:
            objname = objname.split("\\")[-1]
            if objname not in objs:
                continue
            for name, body, _, relocs in coff_functions(obj, with_relocs=True):
                if name in objs[objname]:
                    out.setdefault((name, p, objname), (len(body), relocs))
    return out


def site_target(image, at, r, base):
    """The address the game's operand at a relocation site refers to (symbol + addend), as an rva."""
    field = struct.unpack_from("<i", image, at + r.off)[0]
    if r.type == REL32:
        return at + r.off + 4 + field
    if r.type == DIR32:
        return (field & 0xFFFFFFFF) - base
    return field & 0xFFFFFFFF


def load_exclusions():
    """db/library-exclusions.jsonl: rows reviewed by hand and found to be game code of a library function's shape (one
    line per function: "rva", "reason"). A match there is dropped, with the "inside" rows under it, so the function stays
    a game function (named, tested and verified as such). Reviewed per entry: a shape match on a tiny body (a stored
    event's type constructor matched an STL list-node constructor) that no relocation confirms."""
    path = ROOT / "db/library-exclusions.jsonl"
    if not path.exists():
        return {}
    with open(path, encoding="utf-8") as f:
        return {int(e["rva"], 16): e["reason"] for e in map(json.loads, filter(str.strip, f)) if e.get("rva")}


def apply_exclusions(rows, excluded):
    """Marks the excluded matches (rva -> reason) as dropped; returns the rvas that had a row."""
    hit = sorted(at for at in excluded if at in rows)
    for at in hit:
        rows[at]["drop"] = True
    return hit


def analyse_relocs(rows, tables, image, base, text, known, undecorate=None, lib=None, confirm_only=frozenset()):
    """Checks each matched row's candidates against the game's relocation targets and records names_by_relocs,
    family, family_likely and calls in the row (drop for rejected ones). known: rva -> (decorated library names,
    plain CRT names) of library code; lib: the Library (which symbols have bodies, and where they match);
    confirm_only: rvas whose matches count only when every relocation lands on the library function it names (the
    game's static initialisers: they share the shapes of the library's)."""
    if undecorate is None:
        from names import undecorate
    lo, hi = text
    und, at_cache = {}, {}

    def plain(n):
        if n not in und:
            und[n] = undecorate(n)
        return und[n]

    def body_there(sym, addr):
        k = (sym, addr)
        if k not in at_cache:
            at_cache[k] = lib is not None and 0 <= addr < len(image) and lib.matches_at(sym, image, addr)
        return at_cache[k]

    def verdict(sym, addr):
        names, crt = known.get(addr, ((), ()))
        if sym in names or plain(sym) in crt or body_there(sym, addr):
            return "agree"  # the target is that library function (by its match, or its body is there: folded code)
        if names or crt:
            return "conflict"
        # "missing": our libraries have this symbol's body, yet the game's target doesn't match it: another
        # instantiation (or a callee built differently, or one Ghidra sized wrongly).
        return "missing" if lib is not None and sym in lib.defined else "unknown"

    for at, row in rows.items():
        if row["kind"] == "inside":
            continue
        n = row["length"]
        cands = {}  # (name, relocation signature) -> relocations: one per distinct body of a name
        for name, p, obj in row["entries"]:
            if (name, p, obj) in tables:
                relocs = tables[(name, p, obj)][1]
                cands.setdefault((name, tuple((r.off, r.sym, r.local) for r in relocs)), relocs)
        results = {}
        for key, relocs in cands.items():
            sites, conflicts, addr_of = [], 0, {}
            for r in relocs:
                if r.off + 4 > n:
                    continue
                t = site_target(image, at, r, base)
                if r.local is not None:
                    conflicts += t != at + r.local
                    continue
                if r.sym is None:
                    continue
                addr = t - r.addend
                if addr_of.setdefault(r.sym, addr) != addr or at < t < at + n:
                    conflicts += 1  # one symbol at two addresses, or another function's symbol inside this body
                v = verdict(r.sym, addr)
                conflicts += v == "conflict"
                sites.append((r.off, r.sym, addr, v))
            results[key] = (conflicts, sites)
        if not results:
            continue
        confirmed = any(c == 0 and sites and all(v == "agree" for *_, v in sites) for c, sites in results.values())
        if (lib is not None and all(k[0].startswith(PROBE_GENERIC) for k in results)
                and all(lib.is_probe(p) for _, p, _ in row["entries"])):
            # A probe's deleting destructor: its code calls (the destructor, or what it inlines) must land on those
            # library functions, and one must be more than operator delete; its vftable store (data) can't tell.
            confirmed = False
            for c, sites in results.values():
                code = [(s, v) for _, s, a, v in sites if lo <= a < hi]
                if (c == 0 and code and all(v == "agree" for _, v in code)
                        and any(not s.startswith(OPERATOR_DELETE) for s, _ in code)):
                    confirmed = True
            if not confirmed:
                row["drop"] = True
                continue
        if (at in confirm_only or all(k[0].startswith(GENERIC) for k in results)) and not confirmed:
            # A dynamic initialiser or atexit destructor has the shape of every other one (load an address, call a
            # constructor, register a destructor): only relocations landing on those very library functions tell.
            row["drop"] = True
            continue
        good = [k for k, (c, _) in results.items() if c == 0]
        good_names = sorted({k[0] for k in good})
        if good and len(good_names) < len({k[0] for k in results}):
            row["names_by_relocs"] = good_names
        chosen = good or list(results)
        if not good or all(any(s in k[0] for s in STAND_IN) for k in chosen):
            row["family"] = True
        elif all(any(v == "missing" and ("?$" in s or "?$" not in k[0]) for _, s, _, v in results[k][1])
                 and ("?$" in k[0] or not any(v == "agree" for *_, v in results[k][1])) for k in chosen):
            # Callees our libraries have bodies for, which the game's targets don't match: template code calling
            # other instantiations (the same source for another element type), or a shape shared by several
            # functions (libm's SSE2 dispatch stubs: modf's is floor's but for the target). Nothing confirms the
            # name, which may be a family label; the calls say what it really calls.
            row["family_likely"] = True
        if ((row.get("family") or row.get("family_likely")) and lib is not None
                and all(lib.is_probe(p) for _, p, _ in row["entries"])):
            # A probe body nothing confirms is a shape, and a short one made mostly of calls is any wrapper's shape
            # (basic_ostringstream::str matched scene-graph virtuals; lane MJ). Under MIN_SIZE bytes of its own, it
            # must call some library code (a _Hash's _Rehash_for_1 calls a _Forced_rehash row, those virtuals game
            # code); under SHORT_PREFIX, it must too when it is one instantiation only and calls a plain member
            # (not a function template) we have the body of, whose target the game has as something else: no other
            # element type explains the difference (vector<int>::operator= shares its shape with vector<float>'s and
            # calls their helpers, _Emplace_one_at_back calls _Emplace_reallocate<T>; str() has one name and calls
            # basic_stringbuf<char>::str).
            own = max(n - sum(RELOC_SIZES.get(r.type, 4) for r in relocs if r.off < n) for relocs in cands.values())
            code = [(a, s, v) for k in chosen for _, s, a, v in results[k][1] if lo <= a < hi]
            if (own < SHORT_PREFIX and not any(a in known for a, _, _ in code)
                    and (own < MIN_SIZE or (len({k[0] for k in chosen}) == 1
                                            and any(v == "missing" and not s.startswith("??$")
                                                    for _, s, v in code)))):
                row["drop"] = True
                continue
        calls = defaultdict(lambda: [None, set(), set()])  # off -> [addr, symbols, verdicts]
        for k in chosen:
            for off, sym, addr, v in results[k][1]:
                c = calls[off]
                c[0] = addr if c[0] is None else c[0]
                c[1].add(sym)
                c[2].add(v)
        out = []
        for off in sorted(calls):
            addr, syms, vs = calls[off]
            if vs == {"agree"} or not lo <= addr < hi:
                continue  # confirmed by a library match, or data
            e = {"off": hex(off), "target": hex(addr), "symbols": sorted(syms)}
            if "missing" in vs:
                e["unmatched"] = True  # we have the symbol's body; the target's bytes are not it
            out.append(e)
        if out:
            row["calls"] = out


LIB_CAP = 120  # inputs listed per row (above every row's count before probe_one)


def format_rows(lib, rows, raw_sizes, merged_sizes, funcs_sorted):
    """rows -> db lines (sorted by rva)."""
    starts = [s for s, _ in funcs_sorted]
    out = []
    for at in sorted(rows):
        row = rows[at]
        if row["kind"] == "inside" or row.get("drop"):
            continue
        entries = row["entries"]
        libs = sorted({THIRDPARTY_OF.get(str(lib.paths[p]), lib.paths[p].name) for _, p, _ in entries})
        # The first LIB_CAP inputs (readers take lib[0]): one instantiation is in hundreds of stlprobe probe_one units
        # (lane WB), which would add megabytes to the file. "lib_count" keeps the total.
        m = {"rva": hex(at), "names": sorted({n for n, _, _ in entries}), "lib": libs[:LIB_CAP],
             "objects": sorted({o for _, _, o in entries})[:5]}
        if len(libs) > LIB_CAP:
            m["lib_count"] = len(libs)
        if row["kind"] != "exact":
            m["kind"] = row["kind"]
            m["size"] = row["length"]
            if row["kind"] == "prefix":
                m["ghidra_size"] = raw_sizes.get(at)
                if merged_sizes.get(at) != raw_sizes.get(at):
                    m["db_size"] = merged_sizes.get(at)
            else:
                i = bisect.bisect_right(starts, at) - 1
                if i >= 0 and starts[i] + funcs_sorted[i][1] > at:
                    m["container"] = hex(starts[i])
        for k in ("names_by_relocs", "family", "family_likely", "calls"):
            if k in row:
                m[k] = row[k]
        row["line"] = m
        out.append(m)
    for at in sorted(rows):
        row = rows[at]
        if row["kind"] == "inside" and not rows[row["in"]].get("drop"):
            parent = rows[row["in"]]["line"]
            out.append({"rva": hex(at), "names": [], "kind": "inside", "in": hex(row["in"]), "offset": row["offset"],
                        "piece_of": parent["names"], "lib": parent["lib"]})
    out.sort(key=lambda m: int(m["rva"], 16))
    return out


def known_library(rows_lines, undecorate=None):
    """rva -> (decorated names, plain CRT names): library code the relocation check can compare names with. The CRT
    names (db/crt-equivalents.jsonl) are written either way, decorated or plain with a parameter list."""
    if undecorate is None:
        from names import undecorate
    known = defaultdict(lambda: (set(), set()))
    for m in rows_lines:
        if m.get("kind") != "inside":
            known[int(m["rva"], 16)][0].update(m["names"])
    for e in tsdb.jsonl("crt-equivalents.jsonl"):
        name = undecorate(e["name"]) if e["name"].startswith("?") else e["name"]
        known[int(e["rva"], 16)][1].add(name.split("(")[0].strip())
    return known


def load_image():
    import pefile
    pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    lo = text.VirtualAddress
    return pe.get_memory_mapped_image(), (lo, lo + text.Misc_VirtualSize), pe.OPTIONAL_HEADER.ImageBase


def main():
    sys.stdout.reconfigure(line_buffering=True)  # progress, also through a pipe or into a file
    args = sys.argv[1:]
    if args == ["--write-stamp"]:  # stlprobe.ps1, after building the objects
        (STLPROBE / "sources.sha256").write_text(stlprobe_hash() + "\n")
        return
    if any(a in ("-h", "--help") for a in args):
        print(__doc__)
        return
    if "--callees" in args:
        return callees(args[args.index("--callees") + 1])
    if "--relocation-names" in args:
        return relocation_names("--json" in args, "--all" in args)
    if "--boundaries" in args:
        return boundaries("--json" in args)
    for mode, fn in (("--score", score), ("--cover", cover), ("--diff", diff), ("--near", near),
                     ("--add-objects", add_objects)):
        if mode in args:
            return fn([a for a in args if a != mode])
    dry_run = "--dry-run" in args
    out_path = None
    if "--out" in args:  # with --dry-run: the rows go to this file instead of db/library-matches.jsonl
        out_path = args[args.index("--out") + 1]
        args = [a for i, a in enumerate(args) if a != "--out" and (i == 0 or args[i - 1] != "--out")]
    paths = []
    for arg in (a for a in args if a != "--dry-run"):
        paths += sorted(Path(arg).glob("*.obj")) if Path(arg).is_dir() else [Path(arg)]
    lib = Library()
    for p in paths or DEFAULT_LIBS:
        lib.add_path(p)
    print(f"{lib.total} library functions indexed from {len(lib.paths)} libs")
    if not paths and stlprobe_stale():
        print(f"WARNING: {STLPROBE} was not built from tools/stlprobe's sources as they are now: run "
              "tools/stlprobe/stlprobe.ps1 (-Out DIR, then --stlprobe DIR) before writing rows")

    image, text, base = load_image()
    funcs = tsdb.functions()
    raw_sizes = {int(f["rva"], 16): f["size"] for f in tsdb.jsonl("functions.jsonl")}
    merged = {int(f["rva"], 16): f["size"] for f in funcs}
    rows, conflicts = locate(lib, image, funcs, text[0], text[1])
    rejected = apply_exclusions(rows, load_exclusions())
    if rejected:
        print(f"rejected by db/library-exclusions.jsonl: {', '.join(hex(r) for r in rejected)}")
    funcs_sorted = sorted(merged.items())
    lines = format_rows(lib, rows, raw_sizes, merged, funcs_sorted)
    sys.path.insert(0, str(ROOT / "tools"))
    from names import undecorate
    tables = reloc_tables(lib, rows)
    # The C++ initialisers (XC); the C ones (XI, initterm_e) are all the CRT's own.
    statics = {int(e["rva"], 16) for e in tsdb.jsonl("static-initializers.jsonl") if e.get("table") != "initterm_e"}
    analyse_relocs(rows, tables, image, base, text, known_library(lines, undecorate), undecorate, lib, statics)
    lines = format_rows(lib, rows, raw_sizes, merged, funcs_sorted)
    if conflicts:
        print(f"{len(conflicts)} function starts inside a longer match also match on their own (kept both): "
              + ", ".join(f"{hex(s)} in {hex(a)}" for a, s in conflicts[:10]))

    # Keep the hand-written choice among identical names ("name", "note"; tools/names.py honours "name").
    path = ROOT / "db/library-matches.jsonl"
    old = {}
    if path.exists():
        for m in map(json.loads, open(path)):
            old[m["rva"]] = m
    for m in lines:
        o = old.get(m["rva"])
        if o is None:
            continue
        extra = {k: o[k] for k in ("name", "note") if k in o}
        if extra and extra.get("name", m["names"][0] if m["names"] else None) in m["names"]:
            m.update(extra)
    if not dry_run or out_path:
        with open(out_path or path, "w", newline="\n") as out:
            for m in lines:
                out.write(json.dumps(m) + "\n")
    summarise(lines, old, merged)


def rank(m):
    """How far a row's names are confirmed: 2 by relocations, 1 family_likely, 0 family label."""
    return 0 if m.get("family") else 1 if m.get("family_likely") else 2


def extend_row(o, n):
    """Row o with the names, inputs and objects of row n, a match of the same body from other inputs. The better
    confirmed side gives the relocation findings, as in a full run, where a confirmed candidate decides the row."""
    out = dict(o)
    names = sorted(set(o["names"]) | set(n["names"]))
    added = set(n["lib"]) - set(o["lib"])
    count = o.get("lib_count", len(o["lib"])) + len(added) + n.get("lib_count", len(n["lib"])) - len(n["lib"])
    libs = sorted(set(o["lib"]) | added)
    out.pop("lib_count", None)
    out.update(names=names, lib=libs[:LIB_CAP], objects=sorted(set(o["objects"]) | set(n["objects"]))[:5])
    if count > LIB_CAP:
        out["lib_count"] = count
    good_o, good_n = o.get("names_by_relocs", o["names"]), n.get("names_by_relocs", n["names"])
    if rank(n) > rank(o):
        for k in ("family", "family_likely", "calls"):
            out.pop(k, None)
        out.update({k: n[k] for k in ("family", "family_likely", "calls") if k in n})
        good = good_n
    else:
        good = good_o if rank(n) < rank(o) else sorted(set(good_o) | set(good_n))
    out.pop("names_by_relocs", None)
    if set(good) != set(names):
        out["names_by_relocs"] = sorted(good)
    return out


def merge_added(old_lines, new_lines, sizes, excluded=()):
    """The rows of old_lines with those of new_lines (the matches of a few new objects, indexed alone) merged in, as a
    full run would place them: an exact match beats a non-exact one at the same address, the longer non-exact one wins,
    equal bodies extend the row, a non-exact match inside an accepted one is dropped and one around a gap or unlisted
    row replaces it. An exact match inside a longer one keeps both, and its "inside" row goes. Rows the merge doesn't
    reach are returned unchanged. sizes: rva -> db size; excluded: rvas to remove (db/library-exclusions.jsonl), with
    their pieces. Returns (lines, {"added": [...], "extended": [...], "replaced": [...], "skipped": [...],
    "excluded": [...]}), rvas as text."""
    report = {"added": [], "extended": [], "replaced": [], "skipped": [], "excluded": []}
    rows = {m["rva"]: dict(m) for m in old_lines}
    span = lambda m: m.get("size") or sizes.get(int(m["rva"], 16), 0)
    taken = sorted((int(r, 16), int(r, 16) + span(m), r) for r, m in rows.items() if m.get("kind") != "inside")

    def remove(rva):
        del rows[rva]
        for r in [r for r, m in rows.items() if m.get("in") == rva]:
            del rows[r]
        taken[:] = [t for t in taken if t[2] != rva]

    def place(n):
        at = int(n["rva"], 16)
        o = rows.get(n["rva"])
        if o is not None:
            extra = {k: o[k] for k in ("name", "note") if k in o}
            if extra and extra.get("name", n["names"][0]) in n["names"]:
                n = {**n, **extra}
            if o.get("kind") == "inside":
                del rows[n["rva"]]
            else:
                remove(n["rva"])
        if n.get("kind", "exact") != "exact":
            end = at + span(n)
            for a, b, r in [t for t in taken if at < t[0] and t[1] <= end]:
                if rows[r].get("kind") in ("gap", "unlisted"):
                    remove(r)
                    report["replaced"].append(r)
        rows[n["rva"]] = n
        bisect.insort(taken, (at, at + span(n), n["rva"]))

    accepted = set()
    for n in sorted((m for m in new_lines if m.get("kind") != "inside"), key=lambda m: (-span(m), int(m["rva"], 16))):
        at, kind = int(n["rva"], 16), n.get("kind", "exact")
        o = rows.get(n["rva"])
        if o is not None and o.get("kind") != "inside":
            old_kind = o.get("kind", "exact")
            if kind == old_kind == "exact" or (kind != "exact" != old_kind and span(n) == span(o)):
                rows[n["rva"]] = extend_row(o, n)
                report["extended"].append(n["rva"])
            elif kind == "exact" or (old_kind != "exact" and span(n) > span(o)):
                place(n)
                accepted.add(n["rva"])
                report["replaced"].append(n["rva"])
            else:
                report["skipped"].append(n["rva"])
            continue
        k = bisect.bisect_left(taken, (at,)) - 1
        if kind != "exact" and any(a < at < b for a, b, _ in taken[max(0, k - 4):k + 1]):
            report["skipped"].append(n["rva"])
            continue
        place(n)
        accepted.add(n["rva"])
        report["added"].append(n["rva"])
    for n in new_lines:  # the pieces of what was placed; a piece of an extended row is there already
        if n.get("kind") == "inside" and n["in"] in accepted and rows.get(n["rva"], n).get("kind") == "inside":
            rows[n["rva"]] = n
    for m in list(rows.values()):  # pieces of a row whose names grew
        if m.get("kind") == "inside" and m["in"] in rows:
            m.update(piece_of=rows[m["in"]]["names"], lib=rows[m["in"]]["lib"])
    for at in sorted(excluded):
        if hex(at) in rows:
            remove(hex(at))
            report["excluded"].append(hex(at))
    return sorted(rows.values(), key=lambda m: int(m["rva"], 16)), report


def add_objects(args):
    """--add-objects DIR|OBJ... [--dry-run] [--out FILE]: what these objects match, merged into the rows as they are.
    No other row is rewritten and no row is lost (bar an excluded one). The differences from a full run: relocations
    are checked against the existing rows and the new objects only, so a target that is some old input's body but no row
    counts as "unknown", not "missing" (no family_likely flag there); and a body the new objects define that an old input
    also places elsewhere is not compared with it (the under-MIN_SIZE and under-SHORT_PREFIX uniqueness rules see only
    the new objects). A full run on main settles both; at the same probes it adds nothing else."""
    dry_run = "--dry-run" in args
    out_path = None
    if "--out" in args:
        i = args.index("--out")
        out_path, args = args[i + 1], args[:i] + args[i + 2:]
    paths = _paths([a for a in args if a != "--dry-run"])
    if not paths:
        raise SystemExit("--add-objects: no objects")
    lib = Library()
    for p in paths:
        lib.add_path(p)
    print(f"{lib.total} library functions indexed from {len(lib.paths)} objects")
    image, text, base = load_image()
    funcs = tsdb.functions()
    raw_sizes = {int(f["rva"], 16): f["size"] for f in tsdb.jsonl("functions.jsonl")}
    merged = {int(f["rva"], 16): f["size"] for f in funcs}
    rows, _ = locate(lib, image, funcs, text[0], text[1])
    excluded = load_exclusions()
    apply_exclusions(rows, excluded)
    funcs_sorted = sorted(merged.items())
    lines = format_rows(lib, rows, raw_sizes, merged, funcs_sorted)
    path = ROOT / "db/library-matches.jsonl"
    old_lines = tsdb.jsonl("library-matches.jsonl")
    from names import undecorate
    statics = {int(e["rva"], 16) for e in tsdb.jsonl("static-initializers.jsonl") if e.get("table") != "initterm_e"}
    analyse_relocs(rows, reloc_tables(lib, rows), image, base, text, known_library(old_lines + lines, undecorate),
                   undecorate, lib, statics)
    lines = format_rows(lib, rows, raw_sizes, merged, funcs_sorted)
    result, report = merge_added(old_lines, lines, merged, excluded)
    for k, v in report.items():
        print(f"{k} {len(v)}" + (f": {', '.join(v[:40])}" if v else ""))
    if not dry_run or out_path:
        with open(out_path or path, "w", newline="\n") as out:
            for m in result:
                out.write(json.dumps(m) + "\n")
    summarise(result, {m["rva"]: m for m in old_lines}, merged)


def summarise(lines, old, sizes=None):
    by_kind, by_lib = defaultdict(lambda: [0, 0]), defaultdict(int)
    for m in lines:
        k = m.get("kind", "exact")
        by_kind[k][0] += 1
        by_kind[k][1] += m.get("size", 0)
        if k != "inside":
            by_lib[m["lib"][0]] += 1
    print(f"{len(lines)} rows: " + ", ".join(f"{k} {n}" + (f" ({b} B)" if b else "") for k, (n, b) in by_kind.items()))
    print(f"family labels {sum(1 for m in lines if m.get('family'))}, names narrowed by relocations "
          f"{sum(1 for m in lines if 'names_by_relocs' in m)}, rows with calls {sum(1 for m in lines if 'calls' in m)}")
    new = [m for m in lines if m["rva"] not in old]
    gone = [r for r in old if r not in {m["rva"] for m in lines}]
    print(f"new rows {len(new)}, rows gone {len(gone)}" + (f": {', '.join(gone[:20])}" if gone else ""))
    if sizes is not None:  # new rows per library (the first input naming the body), with their bytes
        per = defaultdict(lambda: [0, 0])
        for m in new:
            if m.get("kind") != "inside":
                p = per[m["lib"][0]]
                p[0] += 1
                p[1] += m.get("size") or sizes.get(int(m["rva"], 16), 0)
        print("new by library: " + ", ".join(f"{k} {n} ({b} B)" for k, (n, b) in sorted(per.items(), key=lambda kv: -kv[1][1])))
    # Library code is never reimplemented: a status entry on a match is for the coordinator to settle (the entry and its
    # hook go, or the match is wrong). Listed, never deleted here.
    status = tsdb.load_status()
    for m in status_entry_matches(lines, status):
        e = status[m["rva"]]
        name = (m.get("names_by_relocs") or m["names"] or m.get("piece_of") or ["?"])[0]
        flags = [k for k in ("family", "family_likely") if m.get(k)] + (["new"] if m["rva"] not in old else [])
        print(f"  match on a status entry: {m['rva']} {m.get('kind', 'exact')} [{e['status']}, {e['_file']}] "
              f"{e.get('name', '')[:60]} = {name[:100]}" + (f" ({', '.join(flags)})" if flags else ""))


def status_entry_matches(lines, status):
    """The rows to settle: library matches that land on a function with a status entry. Lookalikes (tsdb.lookalike:
    Concurrency Runtime names on game code of the same shape, e.g. COM director destructors, cHTMLLink's deleting
    destructor) are not library code and stay game functions, so they are not listed."""
    return [m for m in lines if m["rva"] in status and not tsdb.lookalike(m)]


def load_rows():
    return tsdb.jsonl("library-matches.jsonl")


def callees(rva_text):
    """Relocation sites of the match at an rva, from its row's objects (parsed again): offset, symbol, game target."""
    from names import undecorate
    rva = hex(int(rva_text, 16))
    row = next((m for m in load_rows() if m["rva"] == rva), None)
    if row is None:
        sys.exit(f"{rva}: no row in db/library-matches.jsonl")
    if row.get("kind") == "inside":
        print(f"{rva} is +{row['offset']:#x} into {row['in']}; showing that match")
        return callees(row["in"])
    image, text, base = load_image()
    names_db = {n["rva"]: n["name"] for n in tsdb.jsonl("names.jsonl")}
    known = known_library(load_rows())
    want, shown, found = set(row["names"]), set(), set()
    for p in DEFAULT_LIBS:
        label = THIRDPARTY_OF.get(str(p), p.name)
        if p.parent.parent.name == "obj" and p.parent.parent.parent.name == "thirdparty":
            label = p.parent.name.split("-")[0]
        if label not in row["lib"]:
            continue
        data = p.read_bytes()
        members = archive_members(data) if data[:8] == b"!<arch>\n" else [(p.name, data)]
        for objname, obj in members:
            objname = objname.split("\\")[-1]
            if len(row["objects"]) < 5 and objname not in row["objects"]:
                continue
            for name, body, _, relocs in coff_functions(obj, with_relocs=True):
                sig = (name, tuple((r.off, r.sym, r.local) for r in relocs))
                if name not in want or sig in shown:
                    continue  # one listing per distinct body (a name can call different targets per object)
                shown.add(sig)
                found.add(name)
                print(f"{rva} as {undecorate(name)} ({objname}, {len(body)} bytes):")
                for r in relocs:
                    t = site_target(image, int(rva, 16), r, base)
                    if r.local is not None:
                        ok = "ok" if t == int(rva, 16) + r.local else f"EXPECTED {hex(int(rva, 16) + r.local)}"
                        print(f"  +{r.off:#06x} local +{r.local:#x} -> {hex(t)} {ok}")
                        continue
                    addr = t - r.addend
                    names, crt = known.get(addr, ((), ()))
                    v = "not a library match" if not names and not crt else (
                        "agree" if r.sym in names or undecorate(r.sym) in crt else "conflict")
                    label = undecorate(r.sym) if r.sym else "(data)"
                    print(f"  +{r.off:#06x} {label} -> {hex(addr)} {names_db.get(hex(addr), '')} [{v}]")
    if want - found:
        print(f"not found in the row's objects: {', '.join(sorted(want - found))}")


def relocation_names(as_json, show_all=False):
    """Targets the relocations of byte-matched callers name, that no library match confirms."""
    from names import undecorate
    rows = load_rows()
    library = {m["rva"] for m in rows}
    crt = {e["rva"]: e["name"] for e in tsdb.jsonl("crt-equivalents.jsonl")}
    starts = {f["rva"] for f in tsdb.functions()}
    status = tsdb.load_status()
    names_db = {n["rva"]: n["name"] for n in tsdb.jsonl("names.jsonl")}
    by_target = defaultdict(lambda: defaultdict(set))
    family_only = defaultdict(bool)
    for m in rows:
        for c in m.get("calls", ()):
            for s in c["symbols"]:
                by_target[c["target"]][s].add(m["rva"])
            family_only[c["target"]] |= not (m.get("family") or m.get("family_likely"))
    out = []
    for t in sorted(by_target, key=lambda x: int(x, 16)):
        syms = by_target[t]
        if t in library:
            continue
        plain = sorted({undecorate(s) for s in syms})
        e = {"rva": t, "symbols": plain, "callers": sorted({c for v in syms.values() for c in v})[:5],
             "unique": len(plain) == 1, "start": t in starts, "from_family_only": not family_only[t]}
        if t in crt:
            e["crt_equivalent"] = crt[t]
        if t in status:
            e["status"] = status[t]["status"]
        if t in names_db:
            e["db_name"] = names_db[t]
        out.append(e)
    if as_json:
        for e in out:
            print(json.dumps(e))
        return
    for e in out:
        if not show_all and (not e["unique"] or e["from_family_only"]):
            continue  # several names, or named only by family-label callers: --all lists them
        flags = [k for k in ("status", "crt_equivalent") if k in e]
        syms = " | ".join(s[:100] for s in e["symbols"][:3]) + (f" (+{len(e['symbols']) - 3} more)" if len(e["symbols"]) > 3 else "")
        print(f"{e['rva']} {syms}" + ("" if e["start"] else " (no function start)")
              + (" (family caller)" if e["from_family_only"] else "")
              + "".join(f" [{k}: {e[k]}]" for k in flags) + f"  <- {', '.join(e['callers'])}")
    print(f"{len(out)} targets, {sum(e['unique'] for e in out)} with one name, "
          f"{sum(e['unique'] and not e['from_family_only'] for e in out)} of them from non-family callers")


def boundaries(as_json):
    """Function-boundary fixes for the shared Ghidra program: from the library matches (a library body's length is the
    function's), plus db/missed-functions.jsonl's records of functions that call _EH_prolog (Ghidra treats it as
    no-return and sizes them 10 bytes)."""
    import missedfuncs
    from names import undecorate
    rows = load_rows()
    raw = {f["rva"]: f for f in tsdb.jsonl("functions.jsonl")}
    out = []
    for m in rows:
        k = m.get("kind")
        if k in ("prefix", "unlisted", "gap"):
            name = undecorate((m.get("names_by_relocs") or m["names"])[0])
            e = {"rva": m["rva"], "fix": "resize" if k == "prefix" else "create", "ghidra_size": m.get("ghidra_size"),
                 "size": m["size"], "evidence": f"library body {name[:100]}"}
            if m.get("container"):
                e["container"] = m["container"]
            out.append(e)
        elif k == "inside":
            out.append({"rva": m["rva"], "fix": "remove (part of the function at " + m["in"] + ")",
                        "ghidra_size": raw.get(m["rva"], {}).get("size"), "offset": m["offset"]})
    listed = {e["rva"] for e in out}
    eh = []
    for e in tsdb.jsonl("missed-functions.jsonl"):
        if e["kind"] == "resized" and e["ghidra_size"] == 10 and e["rva"] not in listed \
                and hex(missedfuncs.EH_PROLOG) in raw.get(e["rva"], {}).get("callees", ()):
            eh.append({"rva": e["rva"], "fix": "resize", "ghidra_size": 10, "size": e["size"],
                       "evidence": "_EH_prolog caller (missedfuncs)"})
    out.sort(key=lambda e: int(e["rva"], 16))
    if as_json:
        for e in sorted(out + eh, key=lambda e: int(e["rva"], 16)):
            print(json.dumps(e))
        return
    for e in out:
        print(f"{e['rva']} {e['fix']}" + (f" {e.get('ghidra_size')} -> {e['size']}" if "size" in e else "")
              + (f" (inside {e['container']})" if e.get("container") else "") + f"  {e.get('evidence', '')}")
    if eh:
        print(f"and {len(eh)} functions that call _EH_prolog ({hex(missedfuncs.EH_PROLOG)}), sized 10 bytes because "
              "Ghidra treats _EH_prolog as no-return (e.g. " + ", ".join(e["rva"] for e in eh[:3]) + "): clear that flag "
              "and repair their flow; db/missed-functions.jsonl has the sizes (--json lists them)")
    out += eh
    kinds = defaultdict(int)
    for e in out:
        kinds[e["fix"].split(" ")[0]] += 1
    print(f"{len(out)} fixes: {dict(kinds)}")


# ---------------------------------------------------------------------------------------------------------------- probes
# Lane IN (from lane IE's notes): modes for trying candidate builds without writing anything.

def _paths(args):
    paths = []
    for a in args:
        paths += sorted(Path(a).glob("*.obj")) if Path(a).is_dir() else [Path(a)]
    return paths


def _range_arg(args):
    if "--range" in args:
        i = args.index("--range")
        return int(args[i + 1], 16), int(args[i + 2], 16), args[:i] + args[i + 3:]
    return None, None, args


def score(args):
    """--score INPUT... [--range LO HI] [--list|--json]: what these inputs alone match (counts and bytes per kind, rows
    the db does not have yet), for comparing builds: one call per flag set, nothing written."""
    show, as_json = "--list" in args, "--json" in args
    lo, hi, args = _range_arg([a for a in args if a not in ("--list", "--json")])
    lib = Library()
    for p in _paths(args):
        lib.add_path(p)
    image, text, base = load_image()
    rows, _ = locate(lib, image, tsdb.functions(), text[0], text[1], log=lambda *_: None)
    db_rows = load_rows()
    old = {int(m["rva"], 16) for m in db_rows if m.get("kind") != "inside"}
    # The relocation check as a full run makes it (it drops unconfirmed generic shapes such as deleting destructors),
    # with the db's rows plus these as the known library code.
    here = [{"rva": hex(a), "names": sorted({n for n, _, _ in r["entries"]})} for a, r in rows.items() if r["kind"] != "inside"]
    analyse_relocs(rows, reloc_tables(lib, rows), image, base, text, known_library(db_rows + here), None, lib)
    by_kind, new = defaultdict(lambda: [0, 0]), [0, 0]
    listed = []
    for rva in sorted(rows):
        r = rows[rva]
        if r["kind"] == "inside" or r.get("drop") or (lo is not None and not lo <= rva < hi):
            continue
        by_kind[r["kind"]][0] += 1
        by_kind[r["kind"]][1] += r["length"]
        if rva not in old:
            new[0] += 1
            new[1] += r["length"]
        listed.append({"rva": hex(rva), "kind": r["kind"], "size": r["length"],
                       "names": sorted({n for n, _, _ in r["entries"]}), "new": rva not in old})
    print(f"{lib.total} functions from {len(lib.paths)} inputs: "
          + ", ".join(f"{k} {n} ({b} B)" for k, (n, b) in sorted(by_kind.items()))
          + f"; total {sum(n for n, _ in by_kind.values())} ({sum(b for _, b in by_kind.values())} B);"
          + f" not in db/library-matches.jsonl {new[0]} ({new[1]} B)")
    for e in listed if (show or as_json) else ():
        print(json.dumps(e) if as_json else
              f"{e['rva']} {e['kind']:8s} {e['size']:6d} {e['names'][0][:80]}" + (" NEW" if e["new"] else ""))


def near_misses(lib, image, funcs, text, lo, hi, only=None, fmin=0.6):
    """--near's search: [(db rva, library name, library length, db size, fraction agreeing, input path)], sorted.
    A body is skipped when that body (its name in its input) is placed, not when only its name is: one member
    instantiated in two units can have two shapes (lane WB: a helper called once in its unit is inlined)."""
    rows, _ = locate(lib, image, funcs, text[0], text[1], log=lambda *_: None)
    placed = {(n, pi) for r in rows.values() for n, pi, _ in r.get("entries", ())}
    cands = sorted((int(f["rva"], 16), f["size"]) for f in funcs if lo <= int(f["rva"], 16) < hi
                   and (only is None or int(f["rva"], 16) in only))
    taken = set(rows)
    out = []
    for bid, key in enumerate(lib.keys):
        if any((n, pi) in placed for n, pi, _ in lib.entries[bid]):
            continue
        mask, n = lib.masks[bid], len(key)
        best = (0.0, None)
        for rva, size in cands:
            if rva in taken or abs(size - n) > 16:
                continue
            m = min(n, size)
            same = sum(1 for i in range(m) if i in mask or image[rva + i] == key[i])
            f = same / max(n, size)
            if f > best[0]:
                best = (f, rva, size)
        if best[0] >= fmin:
            name, pi, _ = min(lib.entries[bid])
            out.append((best[1], name, n, best[2], best[0], lib.paths[pi]))
    return sorted(out)


def near(args):
    """--near INPUT... --range LO HI [--min F]: for each library function the inputs define that no exact or prefix match
    places, the db function in [LO, HI) of about its length (within 16 bytes) whose bytes agree most (same offsets,
    relocations masked), when at least F of them (default 0.6) agree: where the near-misses are, for --diff.
    --rvas FILE instead of (or with) --range: only the db functions FILE lists (RVAs, one per line; lane WB: a triage
    set scattered over the image)."""
    full = "--full" in args  # the whole decorated name and the input file, for --diff INPUT SYMBOL RVA
    lo, hi, args = _range_arg([a for a in args if a != "--full"])
    fmin = 0.6
    if "--min" in args:
        i = args.index("--min")
        fmin, args = float(args[i + 1]), args[:i] + args[i + 2:]
    only = None
    if "--rvas" in args:
        i = args.index("--rvas")
        only = {int(l.split()[0], 16) for l in open(args[i + 1]) if l.strip() and not l.startswith("#")}
        args = args[:i] + args[i + 2:]
    if lo is None:
        if only is None:
            raise SystemExit("--near needs --range LO HI or --rvas FILE")
        lo, hi = 0, 1 << 32
    lib = Library()
    for p in _paths(args):
        lib.add_path(p)
    image, text, base = load_image()
    out = near_misses(lib, image, tsdb.functions(), text, lo, hi, only, fmin)
    for rva, name, n, size, f, path in out:
        print(f"{hex(rva)} {size:6d}  {name if full else name[:50]:50s} {n:6d}  {f:.2f}" + (f"  {path}" if full else ""))
    print(f"{len(out)} near misses")


def cover(args):
    """--cover LO HI [--rows FILE] [--limit N]: db functions starting in [LO, HI) that no library match covers (no row,
    not inside a matched body), biggest first, with their name and status; for finding what a build still misses."""
    limit = 40
    if "--limit" in args:
        limit = int(args[args.index("--limit") + 1])
    rows_path = args[args.index("--rows") + 1] if "--rows" in args else None
    lo, hi = int(args[0], 16), int(args[1], 16)
    # A --dry-run --out file, or the output of --score --json (its summary line is skipped).
    rows = [json.loads(l) for l in open(rows_path) if l.startswith("{")] if rows_path else load_rows()
    spans = sorted((int(m["rva"], 16), int(m["rva"], 16) + (m.get("size") or 0)) for m in rows if m.get("kind") != "inside")
    matched = {m["rva"] for m in rows}
    sizes = {int(f["rva"], 16): f["size"] for f in tsdb.functions()}
    names = {n["rva"]: n.get("name", "") for n in tsdb.jsonl("names.jsonl")}
    status = tsdb.load_status()
    starts = [r for r in sorted(sizes) if lo <= r < hi]
    out, cov = [], 0
    for r in starts:
        k = bisect.bisect_right(spans, (r, float("inf"))) - 1
        inside = k >= 0 and spans[k][0] <= r < spans[k][1]
        if hex(r) in matched or inside:
            cov += sizes[r]
            continue
        out.append((sizes[r], r))
    total = sum(sizes[r] for r in starts)
    print(f"{hex(lo)}-{hex(hi)}: {len(starts)} db functions ({total} B); covered {len(starts) - len(out)} ({cov} B), "
          f"not covered {len(out)} ({total - cov} B)")
    for size, r in sorted(out, reverse=True)[:limit]:
        e = status.get(hex(r))
        print(f"  {hex(r)} {size:6d} {names.get(hex(r), '')[:60]}" + (f"  [{e['status']}, {e['_file']}]" if e else ""))


def diff(args):
    """--diff INPUT SYMBOL RVA [--lines N]: the library body SYMBOL (exact decorated name, else a unique substring) from
    INPUT (.obj or .lib) next to the game's bytes at RVA, disassembled from a few instructions before the first
    difference outside the relocation mask; '*' marks lines whose unmasked bytes differ."""
    import capstone
    n_lines = int(args[args.index("--lines") + 1]) if "--lines" in args else 40
    path, symbol, rva = Path(args[0]), args[1], int(args[2], 16)
    data = path.read_bytes()
    members = archive_members(data) if data[:8] == b"!<arch>\n" else [(path.name, data)]
    cands = [(name, body, mask) for _, obj in members for name, body, mask in coff_functions(obj)]
    pick = [c for c in cands if c[0] == symbol] or [c for c in cands if symbol in c[0]]
    if len({c[0] for c in pick}) != 1:
        sys.exit(f"{symbol}: {len(pick)} candidates" + (": " + ", ".join(sorted({c[0] for c in pick})[:10]) if pick else ""))
    name, body, mask = pick[0]
    image, _, base = load_image()
    sizes = {int(f["rva"], 16): f["size"] for f in tsdb.functions()}
    game = image[rva:rva + max(len(body), sizes.get(rva, 0)) + 16]
    first = next((i for i in range(len(body)) if i not in mask and body[i] != game[i]), None)
    print(f"{name}: {len(body)} B, game function {sizes.get(rva, '?')} B at {hex(rva)}; "
          + ("whole body matches (relocations masked)" if first is None else f"first difference at +{first:#x}"))
    if first is None:
        return
    cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    lib_ins = list(cs.disasm(bytes(body), 0))
    game_ins = list(cs.disasm(bytes(game), 0))
    k = max(0, next((i for i, ins in enumerate(lib_ins) if ins.address + ins.size > first), 0) - 5)
    start = lib_ins[k].address if lib_ins else 0
    g = next((i for i, ins in enumerate(game_ins) if ins.address >= start), len(game_ins))
    for j in range(n_lines):
        a = lib_ins[k + j] if k + j < len(lib_ins) else None
        b = game_ins[g + j] if g + j < len(game_ins) else None
        if a is None and b is None:
            break
        diff_mark = " "
        if a is not None:
            diff_mark = "*" if any(i not in mask and (i >= len(game) or body[i] != game[i])
                                   for i in range(a.address, a.address + a.size)) else " "
        la = f"{a.address:5x} {a.mnemonic} {a.op_str}" if a else ""
        lb = f"{b.address:5x} {b.mnemonic} {b.op_str}" if b else ""
        print(f"{diff_mark} {la[:58]:58s} | {lb[:58]}")


if __name__ == "__main__":
    main()
