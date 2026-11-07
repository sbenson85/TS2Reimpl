"""Every reference to original functions in the game image, and whether each is neutralised: the check behind a
replacement that is *in effect through its callers* (CLAUDE.md "Function status levels", `"in_effect": "callers"`).

Usage: tools/py -I tools/callers.py <rva>... [--assume <rva>...] [--handled <rva>:<site>=<reason>]... [--json]
                                            [--write-traps [<file>]] [--check-traps [<file>]]
                                            [--binary <exe> | --no-binary]   (our build to scan; default
                                            build/Release/ts2host.exe: build ts2host first, see "vtable slot" below)
       tools/py -I tools/callers.py --vtable-only (--class <name> | --group <status file stem>)... [--json]
  --vtable-only lists, for the hooked functions in a class's vtables (db/classes.jsonl, exact name) or in a status
                group (db/status/<group>.jsonl), which are referenced only by vtable slots of vtables no live object
                carries (every reference to the vtable is in hooked or replaced code, and our build doesn't hold it)
                or by original code that never runs (adjustor thunks such vtables hold):
                their hooks (virtual-slot adapters) can go. The others are listed with what still references them.
                Each droppable one names the hooks its verdict rests on (Checker.hook_deps: the nearest hooked code
                that keeps the vtable from being stored, usually the class's constructor or factory hook). With one,
                its entry's trap is "Hooks.<that hook>=1" (the host reports every hook as that switch, 1 when
                installed); with several, their conjunction "Hooks.A=1&Hooks.B=1" (armed only while all are
                installed). --check verifies the trap names exactly those hooks.
  <rva>...      functions to check (hex RVAs).
  --assume      treat these as replaced through their callers before their status entries say so (a plan): they
                then cover the references inside them when their own check is clean. The checked RVAs are assumed
                too.
  --handled     a reference the host neutralises, as a status entry's "callers_handled" would list it (site keys
                below); repeatable.
  --json        one JSON object per RVA instead of the text report.
  --write-traps write the host's trap table (default src/hook/trap_table.cpp) from the status entries with
                "in_effect": "callers"; --check-traps exits 1 if that file is out of date.

What it sees (each reference gets a site key, used by "callers_handled"):
  code:0x...   direct calls and jumps: rel32 `call` / `jmp` / `jcc` anywhere in executable sections, rel8 `jmp` /
               `jcc` within reach of the target, each confirmed as an instruction by disassembling the function
               that contains it (unconfirmed byte matches are reported too, marked); plus calls db/functions.jsonl
               and db/tail-calls.jsonl list whose site the scan did not find.
  abs:0x...    absolute references: relocated dwords holding the target's address (immediates and memory operands
               in code, function-pointer tables, initialiser tables, vtable slots, SEH scope tables).
  pe:entry     the PE header's AddressOfEntryPoint; pe:export:<name> export entries; the TLS callback list is
               relocated, so it shows up as abs:.
  src:<file>:<line>  our code naming the original: OriginalFunction / OriginalAddress, or the RVA as a hex literal
               outside comments (constants for GameAddress); src/hook/ (hook rows, traps) excepted.
  host:<file>  the host naming the RVA as a hex literal (host/*.cpp, *.h).
  bin:0x...    our built host (build/Release/ts2host.exe, tools/ourrefs.py) holding the target's game address (game
               base + RVA) as an operand or in data: a GameAddress the source scan missed (hook rows and traps hold
               bare RVAs, so they don't show up here).
What it can't see: addresses computed at run time (base + offset arithmetic, lookups by name), code written at run
time, references from other modules, and jumps into the middle of a function (only references to its first byte
count). The image has no Control Flow Guard function table to cross-check address-taken functions against.

A reference is neutralised when
  - it sits in code of a function that is in effect itself: hooked (an entry in src/hook/tables/*.cpp; the original
    body never runs) or replaced through its own callers with a clean check (recursively; cycles count as open);
    the target itself (a recursive call) counts too;
  - it sits in code of an original function that never runs (Checker.dead: every reference to it is a slot of a
    vtable no live object carries, or code in effect or dead itself; e.g. an adjustor thunk only the game's vtable
    holds), the same rule the vtable check applies to the code storing a vtable; not for functions replaced through
    their callers (their own check decides) nor for functions nothing visibly references (run-time addresses).
    --vtable-only counts such references as droppable;
  - it is a vtable slot (db/classes.jsonl) and every reference to that vtable is neutralised the same way and our
    code holds the vtable nowhere (Db.vtable_uses; live objects then carry our vtables). "Holds" is checked on what we
    build (tools/ourrefs.py): build/Release/ts2host.exe and its map are scanned, at every byte offset, for the game
    address (game base + RVA) or the bare RVA of any slot of the vtable, as an instruction's immediate or displacement
    or in data (look-alikes straddling instructions, packed compiler metadata and hook / trap rows' CRCs excepted); a
    constant in a table, a helper's
    argument (VTableOf(rva), lane GT's SimAnticsParams), a value computed from another constant all show up. src/ uses
    through GameAddress count too. Without a build (or with one older than src/ and host/), every hex literal outside
    comments in src/ (src/hook/ excepted) and host/ whose value lies in the vtable counts instead, and status.py
    --check reports a problem for each entry whose verdict rests on a vtable: build ts2host first. Unseen either way:
    a vtable pointer copied at run time from a game object into one of ours.
    A reference to the vtable from a relocated word in a writable section, outside any function and vtable, is a
    constant-initialised static object's vptr (static_objects(); lane NI, 2026-10-26: cImageManipDX's static
    0xfecf98). The object's base is the word less its vtable's subobject offset (db/classes.jsonl vftables[].offset,
    from RTTI; 0 for a single vtable) and it spans its class's vptr words, [base, last vptr word + 4); each of the
    class's vtables must sit at base + its offset, or the word stays a live data reference. The object is dead when
    every reference to an address in it is in code in effect or dead (the rule above for vtable stores) and there is
    at least one; Checker.hook_deps then rests on those references' hooks (a getter's). A data word pointing at it,
    our sources or the host naming an address in it, our build holding one (the binary scan looks for the objects'
    RVAs and game addresses too), or no visible reference at all keep it live. Fields past the last vptr word aren't
    counted (no dispatch through this vtable reads them);
  - it is a slot of a declared data table (db/data-tables.jsonl, read-only tables of original functions, reviewed one by
    one; data_tables()) nothing live reads: every reference to an address in the table, row pointers and rows alike, is
    in code in effect or code that never runs, and there is at least one. The table's own row pointer words don't count
    as references. Data outside the table, our sources or the host naming an address in it, our build holding one (the
    binary scan looks for the tables' addresses too), or no visible reference at all keep it live. A declaration the
    image doesn't bear out is ignored and is a status --check problem. Checker.hook_deps rests on the readers' cuts
    (lane XP, 2026-11-06: the 909 span routines, docs/verification.md);
  - it is listed as handled (the status entry's "callers_handled" {site: reason}, or --handled).
  - it is an XC static initialiser our build owns: its slot in the XC table (0xd9a724..0xd9b3a8) together with its row
    in an owned list (src/app/StaticInit*.cpp, the RVA as the hex literal the runner checks the slot against). Both are
    covered; the entry's trap must then imply Startup.OwnStart=1,2&Startup.OwnStatics=1 (the runner's switches).
Other src: references (our code calling or naming the original) are never neutralised.
"""
import argparse
import bisect
import json
import re
import struct
import sys
from collections import namedtuple
from datetime import date
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import origcalls  # noqa: E402
import ourrefs  # noqa: E402
import tsdb  # noqa: E402

VERSION = 1  # the vtable rule widened 2026-10-18 (lane GX: hex literals anywhere, the binary scan) without a bump: status
# --check reruns every entry's check, and a bump would only add a note per existing entry
TRAP_TABLE = "src/hook/trap_table.cpp"
# The image's XC initialiser table (__xc_a..__xc_z) and the lists of the entries our build owns there: the host's runner
# (winmainstart::RunInitialisersOwned) runs ours in those slots only under these switches.
XC_TABLE = (0xd9a724, 0xd9b3a8)
OWNED_LIST = re.compile(r"src:src/app/StaticInit\w*\.cpp:\d+$")
XC_TRAP = "Startup.OwnStart=1,2&Startup.OwnStatics=1"
HOOK_ROW = re.compile(r'^\s*\{\s*"([^"]*)"\s*,\s*(0x[0-9a-fA-F]+)\s*,', re.M)
HOOK_CRC = re.compile(r'\{\s*"[^"]*"\s*,\s*0x[0-9a-fA-F]+\s*,\s*\w+\s*,\s*(0x[0-9a-fA-F]+)\s*,')  # hook and trap rows
HEX = re.compile(r"\b0x[0-9a-fA-F]{5,8}\b")
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
SHORT_BRANCH = re.compile(rb"[\xeb\x70-\x7f]")
TRAP = re.compile(r"([A-Za-z0-9_]+)\.([A-Za-z0-9_]+)=(\d+(?:,\d+)*)")


def check_version():
    return f"callers.py v{VERSION} {date.today().isoformat()}"


def parse_trap(text):
    """'Section.Key=1,2' -> [(section, key, {1, 2})]; a conjunction 'Hooks.A=1&Hooks.B=1' -> one term per switch, all
    required (the trap arms only when every one holds). None if malformed or a switch repeats."""
    terms = []
    for part in (text or "").split("&"):
        m = TRAP.fullmatch(part)
        if not m:
            return None
        values = {int(v) for v in m.group(3).split(",")}
        if not all(0 <= v < 32 for v in values):
            return None
        terms.append((m.group(1), m.group(2), values))
    if len({(s.lower(), k.lower()) for s, k, _ in terms}) != len(terms):
        return None
    return terms


def trap_implies(trap, other):
    """Is `other` (terms) armed whenever `trap` is? Each of other's terms needs a term of trap on the same switch whose
    values are among other's."""
    return all(any(t[:2] == o[:2] and t[2] <= o[2] for t in trap) for o in other)


def trap_text(terms):
    return "&".join(f"{s}.{k}={','.join(str(v) for v in sorted(vs))}" for s, k, vs in terms)


class Image:
    """The unpacked exe: sections, relocations, header references."""

    def __init__(self, path=None):
        import pefile
        self.pe = pefile.PE(str(path or tsdb.unpacked_exe()), fast_load=True)
        self.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.entry = self.pe.OPTIONAL_HEADER.AddressOfEntryPoint
        self.sections = []  # (name, rva, size, executable, bytes)
        self.writable = []  # (start, end) of writable sections: where a constant-initialised static object lives
        for s in self.pe.sections:
            name = s.Name.rstrip(b"\0").decode(errors="replace")
            size = s.Misc_VirtualSize
            self.sections.append((name, s.VirtualAddress, size, bool(s.Characteristics & 0x20000000),
                                  self.pe.get_data(s.VirtualAddress, size)))
            if s.Characteristics & 0x80000000:
                self.writable.append((s.VirtualAddress, s.VirtualAddress + size))
        self.exports = [(e.name.decode() if e.name else f"#{e.ordinal}", e.address)
                        for e in getattr(getattr(self.pe, "DIRECTORY_ENTRY_EXPORT", None), "symbols", [])]
        self._relocs = None
        self._branches = None

    def is_writable(self, rva):
        return any(start <= rva < end for start, end in getattr(self, "writable", ()))

    def section_of(self, rva):
        for name, start, size, _, _ in self.sections:
            if start <= rva < start + size:
                return name
        return None

    def bytes_at(self, rva, n):
        for _, start, size, _, data in self.sections:
            if start <= rva and rva + n <= start + size:
                return data[rva - start:rva - start + n]
        return b""

    def relocations(self):
        """{value (RVA it points at): [site RVAs]} for every HIGHLOW relocation."""
        if self._relocs is None:
            d = self.pe.OPTIONAL_HEADER.DATA_DIRECTORY[5]
            raw = self.pe.get_data(d.VirtualAddress, d.Size)
            out, off = {}, 0
            start, data, last = 1, b"", 0  # the section the last site fell in, and the last dword it holds
            while off + 8 <= len(raw):
                page, block = struct.unpack_from("<II", raw, off)
                if block < 8:
                    break
                for e in struct.unpack_from(f"<{(block - 7) // 2}H", raw, off + 8):
                    if e >> 12 == 3:
                        site = page + (e & 0xFFF)
                        if not start <= site <= last:  # 0 if no section holds the whole dword
                            start, data, last = next(((s, d, s + min(z, len(d)) - 4) for _, s, z, _, d in self.sections
                                                      if s <= site <= s + min(z, len(d)) - 4), (1, b"", 0))
                        value = (struct.unpack_from("<I", data, site - start)[0] if start <= site <= last else 0) - self.base
                        out.setdefault(value & 0xFFFFFFFF, []).append(site)
                off += block
            self._relocs = out
        return self._relocs

    def branch_sites(self, targets):
        """{target: [(opcode site, kind)]}: byte patterns of rel32 call/jmp/jcc in executable sections, and rel8
        jmp/jcc within reach, whose destination is a target. Not yet confirmed as instructions."""
        targets = set(targets)
        if self._branches is None:  # every rel32 byte pattern, once: destination -> [(site, kind)]
            self._branches = {}
            for _, start, size, executable, data in self.sections:
                if not executable:
                    continue
                for m in re.finditer(rb"[\xe8\xe9]", data):
                    p = m.start()
                    if p + 5 <= len(data):
                        dest = (start + p + 5 + struct.unpack_from("<i", data, p + 1)[0]) & 0xFFFFFFFF
                        self._branches.setdefault(dest, []).append((start + p, "call" if data[p] == 0xE8 else "jmp"))
                for m in re.finditer(rb"\x0f[\x80-\x8f]", data):
                    p = m.start()
                    if p + 6 <= len(data):
                        dest = (start + p + 6 + struct.unpack_from("<i", data, p + 2)[0]) & 0xFFFFFFFF
                        self._branches.setdefault(dest, []).append((start + p, "jcc"))
        out = {t: list(self._branches.get(t, [])) for t in targets}
        for _, start, size, executable, data in self.sections:
            if not executable:
                continue
            for t in targets:
                for m in SHORT_BRANCH.finditer(data, max(0, t - start - 129), max(0, min(len(data) - 1, t - start + 127))):
                    p = m.start()
                    if (start + p + 2 + struct.unpack_from("<b", data, p + 1)[0]) & 0xFFFFFFFF == t:
                        out[t].append((start + p, "jmp short" if data[p] == 0xEB else "jcc short"))
        return out


def static_objects(image, db):
    """{vptr word: (vtable start, object base, object end)}: the constant-initialised static objects of the image (lane
    NI, 2026-10-26). A relocated word in a writable section, outside any function and vtable, holding a vtable's start
    is an object's vptr; the object's base is the word less the offset of that vtable's subobject (db/classes.jsonl
    "offset", from RTTI; 0 for a single vtable) and it spans the class's vptr words: [base, last vptr word + 4). Each
    of the class's other vtables must sit at base + its offset, or the word isn't taken for an object (it stays a live
    data reference to the vtable, as before the rule)."""
    layouts = getattr(db, "vt_layout", {})
    relocs = image.relocations()
    holds = {}  # site -> the vtable start it holds
    for start, _, _ in db.vtables:
        for site in relocs.get(start, []):
            holds[site] = start
    out = {}
    for site, vt in holds.items():
        if not image.is_writable(site) or db.function_at(site) is not None or db.vtable_at(site):
            continue
        layout = layouts.get(vt, (0, ((vt, 0),)))
        if layout is None:
            continue  # the vtable is listed with different layouts: not decided
        offset, words = layout
        base = site - offset
        if any(holds.get(base + o) != v for v, o in words):
            continue
        out[site] = (vt, base, base + max(o for _, o in words) + 4)
    return out


DataTable = namedtuple("DataTable", "start end pointers slots")  # pointers {site: row}, slots {site: function}


def data_tables(image, db):
    """([DataTable], [problem]): the declared tables (db/data-tables.jsonl, db.table_rows) the image bears out. A row is
    {"rva", "end", "row_pointers", "rows", "row_words", "reason"}: `row_pointers` words, then `rows` rows of `row_words`
    words each, contiguous (row_pointers 0 for a plain table), in a read-only section, outside functions and vtables.
    Every pointer word is relocated and holds a row's start, every row has a pointer to it, every row word is relocated
    and holds a function's start. A declaration that fails any of this is left out (what it would have covered stays
    live), and reported."""
    relocs = image.relocations()

    def word(site):
        data = image.bytes_at(site, 4)
        value = (struct.unpack("<I", data)[0] - image.base) & 0xFFFFFFFF if len(data) == 4 else None
        return value if site in relocs.get(value, ()) else None

    def read(row):
        start, end = int(row["rva"], 16), int(row["end"], 16)
        first = start + 4 * row["row_pointers"]
        step = 4 * row["row_words"]
        if row["rows"] < 1 or step < 4 or end != first + row["rows"] * step:
            return None, f"end {end:#x} isn't the declared structure's end {first + row['rows'] * step:#x}"
        for site in range(start, end, 4):
            if image.is_writable(site) or image.section_of(site) is None:
                return None, f"{site:#x} isn't in a read-only section"
            if db.function_at(site) is not None or db.vtable_at(site):
                return None, f"{site:#x} is inside a function or vtable"
        pointers, slots = {}, {}
        for site in range(start, first, 4):
            pointers[site] = word(site)
            if pointers[site] is None or not (first <= pointers[site] < end and (pointers[site] - first) % step == 0):
                return None, f"the row pointer at {site:#x} isn't a relocated word holding a row's start"
        for at in range(first, end, step):
            if pointers and at not in pointers.values():
                return None, f"nothing in the table points at the row {at:#x}"
        for site in range(first, end, 4):
            slots[site] = word(site)
            if slots[site] not in db.funcs:
                return None, f"the slot at {site:#x} isn't a relocated word holding a function's start"
        return DataTable(start, end, pointers, slots), None

    tables, problems = [], []
    for row in getattr(db, "table_rows", ()):
        table, why = read(row)
        if table:
            tables.append(table)
        else:
            problems.append(f"db/data-tables.jsonl: {row['rva']}: {why}: the declaration is ignored")
    return tables, problems


class Db:
    """The facts the check reads: functions, names, status, hooks, vtables, our sources."""

    def __init__(self, binary=None, image=None, sources=None, found=None, funcs=None, entries=None):
        """binary: our built host to scan (ourrefs.py), default build/Release/ts2host.exe; False skips the scan. image:
        the game image (Image), for the scan's static-object addresses (loaded when the scan runs without it). sources,
        found, funcs, entries: origcalls.source_files(), its origcalls.scan(), tsdb.functions() and
        tsdb.status_entries(), when the caller already has them."""
        self._image = image
        self.funcs = {int(f["rva"], 16): f for f in (tsdb.functions() if funcs is None else funcs)}
        self.starts = sorted(self.funcs)
        self.table_rows = tsdb.jsonl("data-tables.jsonl")  # data_tables()
        self.names = {int(n["rva"], 16): n.get("name", "") for n in tsdb.jsonl("names.jsonl")}
        self.status = {int(e["rva"], 16): e for _, e in (tsdb.status_entries() if entries is None else entries)}
        self.hooked = set()
        self.hook_names = {}  # rva -> the hook's name (its ini key: [Hooks] <name>)
        for p in (tsdb.ROOT / "src/hook/tables").glob("*.cpp"):
            for name, r in HOOK_ROW.findall(p.read_text(encoding="utf-8", errors="replace")):
                self.hooked.add(int(r, 16))
                self.hook_names.setdefault(int(r, 16), name)
        self.vtables = []  # (start, end, class name)
        self.vt_layout = {}  # vtable start -> (its subobject's offset, ((vtable start, offset) of each of the class's))
        for c in tsdb.jsonl("classes.jsonl"):
            words = tuple((int(vt["rva"], 16), vt.get("offset", 0)) for vt in c.get("vftables", []))
            for vt in c.get("vftables", []):
                s = int(vt["rva"], 16)
                self.vtables.append((s, s + 4 * len(vt.get("entries", [])), c["name"]))
                layout = (vt.get("offset", 0), words)
                self.vt_layout[s] = layout if self.vt_layout.get(s, layout) == layout else None
        self.vtables.sort()
        self.calls_by_db = {}
        for rva, f in self.funcs.items():
            for c in f.get("callees", []):
                if c.startswith("0x"):
                    self.calls_by_db.setdefault(int(c, 16), set()).add(rva)
        for e in tsdb.jsonl("tail-calls.jsonl"):
            for t in e["targets"]:
                self.calls_by_db.setdefault(int(t, 16), set()).add(int(e["rva"], 16))
        self.sources = origcalls.source_files() if sources is None else sources
        self.src_refs = {}  # rva -> [(file, line, how)]
        for path, sites in (origcalls.scan(self.sources) if found is None else found).items():
            for line, rva in sites:
                if rva:
                    self.src_refs.setdefault(int(rva, 16), []).append((path, line, "OriginalFunction/OriginalAddress"))
        for path, text in self.sources.items():
            if path.startswith("src/hook/"):
                continue  # hook rows and traps name the sites they patch, not uses of the original
            code, line, at = self._code(path), 1, 0
            for m in HEX.finditer(code):
                line, at = line + code.count("\n", at, m.start()), m.start()
                refs = self.src_refs.setdefault(int(m.group(0), 16), [])
                if (path, line, "OriginalFunction/OriginalAddress") not in refs:
                    refs.append((path, line, "hex literal"))
        self.game_address_uses = self._game_address_uses()
        self.host_refs = {}  # rva -> {file}
        for p in sorted((tsdb.ROOT / "host").glob("*")):
            if p.suffix in (".cpp", ".h", ".c"):
                rel = str(p.relative_to(tsdb.ROOT)).replace("\\", "/")
                for h in HEX.findall(p.read_text(encoding="utf-8", errors="replace")):
                    self.host_refs.setdefault(int(h, 16), set()).add(rel)
        self.binary_note = None
        self.bin_refs = {}  # function rva -> [description]: our binary holds its game address
        self.object_uses = {}  # static object or data table start -> [description]: our binary holds an address in it
        self.vtable_uses = self._vtable_uses(binary)

    def _vtable_uses(self, binary):
        """{vtable start: [where]}: every place our code holds a game vtable, whatever the form, so objects may carry it
        (the vtable stays live). Sources: GameAddress uses (game_address_uses), any hex literal outside comments in src/
        (src/hook/ excepted) or host/ whose value lies in the vtable (a constant, a table row, a helper's argument), and
        the binary scan of our built host (ourrefs.py: the game address or the bare RVA of any slot, as an operand or
        in data; covers constants computed from others). Also fills bin_refs for function starts held as game
        addresses, and binary_note when the scan was skipped or the build is older than src/."""
        out = {}

        def add(value, where):
            vt = self.vtable_at(value)
            if vt and where not in out.setdefault(vt[0], []):
                out[vt[0]].append(where)

        for value, sites in self.game_address_uses.items():
            for p, l in sites:
                add(value, f"{p}:{l} (GameAddress)")
        def literals():
            # Without a current build, every hex literal in a vtable counts, whether or not built code uses it (a
            # constant only tests read, a comment-free mention): conservative. With one, the binary decides.
            for value, sites in self.src_refs.items():
                for p, l, how in sites:
                    if how == "hex literal":
                        add(value, f"{p}:{l} (hex literal)")
            for value, files in self.host_refs.items():
                for p in sorted(files):
                    add(value, f"{p} (hex literal)")

        if binary is False:
            literals()
            return out  # asked for (--no-binary; the trap table alone): the caller says so
        if hasattr(binary, "scan"):
            b = binary  # a ready ourrefs.Binary (tests)
        else:
            exe = Path(binary) if binary else ourrefs.default_exe()
            if not exe.exists() or not exe.with_suffix(".map").exists():
                self.binary_note = (f"no build of our host at {exe} (with its .map): the binary scan is skipped (hex "
                                    "literals in src/ and host/ stand in for it, conservatively), so game addresses "
                                    "our code computes (kBase + 8) are unseen; build ts2host first")
                literals()
                return out
            newest = max((p.stat().st_mtime for d in ("src", "host") for p in (tsdb.ROOT / d).rglob("*")
                          if p.suffix in (".cpp", ".h", ".c")), default=0)
            if newest > exe.stat().st_mtime:
                self.binary_note = (f"{exe} is older than src/ or host/ (hex literals stand in for the newer code, "
                                    "conservatively): rebuild ts2host for an up-to-date binary scan")
                literals()
            b = ourrefs.Binary(exe)
        gb = ourrefs.game_base()
        want = {}
        for start, end, _ in self.vtables:
            for v in range(start, max(end, start + 4), 4):
                want.setdefault(v, ("vt", v, "RVA"))
                want.setdefault((gb + v) & 0xFFFFFFFF, ("vt", v, "game address"))
        for rva in self.funcs:
            want.setdefault((gb + rva) & 0xFFFFFFFF, ("fn", rva, "game address"))
        image = getattr(self, "_image", None)
        if image is None and not hasattr(binary, "scan"):
            image = Image()  # a real scan: the static objects' addresses are looked for too
        for _, base, end in (static_objects(image, self).values() if image is not None else ()):
            for v in range(base, end):
                want.setdefault(v, ("obj", base, "RVA"))
                want.setdefault((gb + v) & 0xFFFFFFFF, ("obj", base, "game address"))
        for row in getattr(self, "table_rows", ()):  # as declared: holding one counts whatever data_tables() makes of it
            base = int(row["rva"], 16)
            for v in range(base, int(row["end"], 16)):
                want.setdefault(v, ("obj", base, "RVA"))
                want.setdefault((gb + v) & 0xFFFFFFFF, ("obj", base, "game address"))
        # Hook rows and trap rows carry a CRC of the original's first bytes, any 32-bit value: one that looks like a
        # vtable address in the hook objects' data is that CRC (the rows' RVAs are bare function RVAs, not looked for).
        crcs = getattr(self, "hook_crcs", None)
        if crcs is None:
            crcs = set()
            for p in (tsdb.ROOT / "src/hook").rglob("*.cpp"):
                crcs.update(int(c, 16) for c in HOOK_CRC.findall(p.read_text(encoding="utf-8", errors="replace")))
        ignore = lambda value, name, obj, code: not code and ":" not in obj and value in crcs  # noqa: E731
        for value, hits in b.scan(want, ignore).items():
            what, rva, how = want[value]
            for site, name, obj, code in hits:
                if not code and site % 4:
                    continue  # data holds pointers aligned: an unaligned match straddles two fields (a hook row's
                    # target and the next row's name pointer read as 0xa0140050)
                if code and site % 4 and ourrefs.undecorate(name).startswith("$LN"):
                    continue  # a switch's jump table ($LN label) in .text: entries are aligned addresses in our exe, so
                    # an unaligned match straddles two entries (0x005ce0a0, 0x005ce6a0 read as 0xa0e6005c)
                where = (f"{b.exe.name} {site:#x} in {ourrefs.undecorate(name)[:60]} ({obj}): "
                         f"{'an operand' if code else 'data'} holds the {how} {value:#x}")
                if what == "vt":
                    add(rva, where)
                elif what == "obj":
                    self.object_uses.setdefault(rva, []).append(where)
                else:
                    self.bin_refs.setdefault(rva, []).append(where)
        return out

    def _code(self, path):
        """A source with its comments blanked, line breaks kept."""
        if not hasattr(self, "_stripped"):
            self._stripped = {}
        code = self._stripped.get(path)
        if code is None:
            def blank(m):
                text = m.group(0)
                return re.sub(r"[^\n]", " ", text) if "\n" in text else " " * len(text)
            code = self._stripped[path] = COMMENT.sub(blank, self.sources[path])
        return code

    def _game_address_uses(self):
        """rva -> [(file, line)]: addresses src/ turns into pointers with GameAddress<...>(x), x a hex literal or a
        named constant (resolved in the same file, then the header of the same stem, else every constant of that name:
        conservative). A vtable used this way may be stored into objects by our code."""
        consts = {}  # path -> {name: {values}}
        in_class = {}  # (class, name) -> {values}
        stripped = {}
        for path in self.sources:
            code = self._code(path)
            stripped[path] = code
            classes = [(m.start(), m.group(1)) for m in re.finditer(r"\b(?:struct|class)\s+(\w+)\s*(?:final\s*)?[:{]", code)]
            class_starts = [at for at, _ in classes]
            for m in re.finditer(r"\b(k\w+)\s*=\s*(0x[0-9a-fA-F]+)\b", code):
                consts.setdefault(path, {}).setdefault(m.group(1), set()).add(int(m.group(2), 16))
                owner = bisect.bisect_left(class_starts, m.start())
                if owner:  # the innermost class declared before it (approximate: nesting and closing aren't tracked)
                    in_class.setdefault((classes[owner - 1][1], m.group(1)), set()).add(int(m.group(2), 16))
        every = {}
        for table in consts.values():
            for name, values in table.items():
                every.setdefault(name, set()).update(values)
        by_stem = {}  # file name without directory or extensions -> {name: {values}}
        for path, table in consts.items():
            stem = path.rsplit("/", 1)[-1].split(".")[0]
            for name, values in table.items():
                by_stem.setdefault(stem, {}).setdefault(name, set()).update(values)
        out = {}
        for path, code in stripped.items():
            stem = path.split(".")[0]
            line, at = 1, 0
            # The template argument is optional (GameAddress(kVTable) is the common form), and GameAddressOpaque counts.
            for m in re.finditer(r"\bGameAddress(?:Opaque)?\s*(?:<[^;()]*?>)?\s*\(([^;]*?)\)", code):
                line, at = line + code.count("\n", at, m.start()), m.start()
                values = {int(h, 16) for h in HEX.findall(m.group(1))}
                for scope, name in re.findall(r"(?:\b(\w+)::)?\b(k\w+)\b", m.group(1)):
                    if scope:  # Class::kName: declared in that class, else in the file named after it
                        found = in_class.get((scope, name)) or by_stem.get(scope, {}).get(name)
                    else:  # the file, its header, then the headers it includes
                        found = consts.get(path, {}).get(name) or consts.get(stem + ".h", {}).get(name)
                        for inc in ([] if found else re.findall(r'#include\s+"([^"]+)"', code)):
                            found = found or consts.get("src/" + inc, {}).get(name)
                    values |= found or every.get(name, set())
                for v in values:
                    out.setdefault(v, []).append((path, line))
        return out

    def function_at(self, rva):
        i = bisect.bisect_right(self.starts, rva) - 1
        if i >= 0 and rva < self.starts[i] + max(self.funcs[self.starts[i]]["size"], 1):
            return self.starts[i]
        return None

    def name(self, rva):
        e = self.status.get(rva)
        n = (e or {}).get("name") or self.names.get(rva) or self.funcs.get(rva, {}).get("name", "?")
        return n.split(" (")[0][:60]

    def vtable_at(self, rva):
        i = bisect.bisect_right(self.vtables, (rva, float("inf"), "")) - 1
        if i >= 0 and self.vtables[i][0] <= rva < self.vtables[i][1]:
            return self.vtables[i]
        return None


class Checker:
    DEAD_DEPTH = 4  # how many callers deep dead() follows original code before assuming it runs

    def __init__(self, image, db, assume=(), handled=None):
        self.image, self.db = image, db
        self.assume = set(assume)
        self.handled = handled or {}  # rva -> {site: reason}
        self._verdicts, self._busy, self._vt, self._vt_busy = {}, set(), {}, set()
        self._dead, self._dead_busy = {}, set()
        self._only, self._cut_memo, self._cut_stack = {}, {}, {}
        self.uncut = {}  # rva -> sites hook_deps() found live though no hook cuts them (the verdict decides them)
        self._decoded = {}
        self._refs = {}
        self._statics = None
        self._tables, self._table_problems = None, []

    def table_of(self, site):
        """The DataTable whose row word is `site`, a reference to a function; else None."""
        if self._tables is None:
            tables, self._table_problems = data_tables(self.image, self.db)
            self._tables = {s: t for t in tables for s in t.slots}
        return self._tables.get(site)

    def table_problems(self):
        """data_tables()'s complaints about the declarations."""
        self.table_of(0)
        return self._table_problems

    def table_references(self, table):
        """object_references()'s rows for every address in `table`, less its own row pointer words."""
        refs = self._range_references(table.start, table.end, "the data table")
        return [r for r in refs if not (r[0].startswith("abs:") and int(r[0][4:], 16) in table.pointers)]

    def table_open(self, table):
        """The references that keep `table` live ([] when nothing live reads it: every reference to it is code in
        effect or code that never runs, and there is at least one; never_runs() also asks for a visible reference to
        the reader, since an address computed at run time may reach it)."""
        refs = self.table_references(table)
        if not refs:
            return [f"the data table {table.start:#x} (nothing visibly references it)"]
        return [f"the data table {table.start:#x} from {site}" + (f" in {owner:#x}" if owner is not None else "")
                for site, _, owner, kind in refs
                if not (kind == "code" and owner is not None and (
                    self.function_in_effect(owner, table.start)[0] or self.never_runs(owner)))]

    def static_object(self, site):
        """(vtable start, base, end) when `site`, a reference to a vtable, is a constant-initialised static object's
        vptr word (static_objects()); else None."""
        if self._statics is None:
            self._statics = static_objects(self.image, self.db)
        return self._statics.get(site)

    def object_references(self, obj):
        """[(site key, description, containing function or None, kind)]: every reference to an address in the static
        object obj (vtable, base, end): relocated words holding it (in code: "code" with the function; elsewhere:
        "data"), our sources and the host naming it, our built host holding it ("src"/"host")."""
        return self._range_references(obj[1], obj[2], "the static object")

    def _range_references(self, base, end, label):
        refs = []
        for v in range(base, end):
            at = f"{base:#x}" + (f"+{v - base:#x}" if v != base else "")
            for site in sorted(self.image.relocations().get(v, [])):
                func = self.db.function_at(site)
                if func is not None and not self.db.vtable_at(site):
                    refs.append((f"abs:{site:#x}", f"{label} {at}'s address in code of {func:#x} "
                                                   f"{self.db.name(func)}", func, "code"))
                else:
                    refs.append((f"abs:{site:#x}", f"{label} {at}'s address in "
                                                   f"{self.image.section_of(site)} (data)", None, "data"))
            for path, line, how in self.db.src_refs.get(v, []):
                refs.append((f"src:{path}:{line}", f"our code names {label} {at} ({how})", None, "src"))
            for path, line in getattr(self.db, "game_address_uses", {}).get(v, []):
                refs.append((f"src:{path}:{line}", f"our code names {label} {at} (GameAddress)", None, "src"))
            for path in sorted(self.db.host_refs.get(v, set())):
                refs.append((f"host:{path}", f"the host names {label} {at}", None, "host"))
        for where in getattr(self.db, "object_uses", {}).get(base, []):
            refs.append((f"bin:{where.split(' ')[1]}", f"our binary holds {label}'s address ({where})", None, "src"))
        return refs

    def object_open(self, obj):
        """The references that keep the static object obj live ([] when it is dead: every reference to it is in code
        in effect or dead, and there is at least one; an object nothing visibly references may be reached through an
        address computed at run time, as in never_runs())."""
        refs = self.object_references(obj)
        if not refs:
            return [f"the static object {obj[1]:#x} (nothing visibly references it)"]
        out = []
        for site, _, owner, kind in refs:
            if not (kind == "code" and owner is not None and (
                    self.function_in_effect(owner, obj[1])[0] or self.dead(owner))):
                out.append(f"the static object {obj[1]:#x} from {site}" + (f" in {owner:#x}" if owner is not None else ""))
        return out

    def instruction_starts(self, func):
        """Instruction offsets of a function, decoded linearly from its start (capstone)."""
        if func not in self._decoded:
            import capstone
            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
            md.skipdata = True
            size = self.db.funcs[func]["size"]
            self._decoded[func] = {i[0] for i in md.disasm_lite(self.image.bytes_at(func, size), func)}
        return self._decoded[func]

    def references(self, target):
        """[(site key, description, containing function or None, kind)]."""
        if target not in self._refs:
            self._refs[target] = self._find_references(target)
        return list(self._refs[target])

    def _find_references(self, target):
        refs, seen_funcs = [], set()
        for site, kind in self.image.branch_sites([target])[target]:
            func = self.db.function_at(site)
            confirmed = func is not None and site in self.instruction_starts(func)
            if func is not None and not confirmed and kind.endswith("short"):
                continue  # rel8 patterns are common in operand bytes: inside a decoded function, only real instructions
            if func is not None:
                seen_funcs.add(func)
            where = f"in {func:#x} {self.db.name(func)}" if func is not None else f"in {self.image.section_of(site)} outside any known function"
            refs.append((f"code:{site:#x}", f"{kind} {where}" + ("" if confirmed else " (byte match, not confirmed as an instruction)"),
                         func, "code"))
        for func in sorted(self.db.calls_by_db.get(target, set()) - seen_funcs):
            refs.append((f"code:{func:#x}", f"call listed in db from {func:#x} {self.db.name(func)} (site not located)",
                         func, "code"))
        for site in sorted(self.image.relocations().get(target, [])):
            vt = self.db.vtable_at(site)
            func = self.db.function_at(site)
            if vt:
                refs.append((f"abs:{site:#x}", f"slot {(site - vt[0]) // 4} of vtable {vt[0]:#x} {vt[2][:60]}", vt, "vtable"))
            elif func is not None:
                refs.append((f"abs:{site:#x}", f"address in code of {func:#x} {self.db.name(func)}", func, "code"))
            else:
                refs.append((f"abs:{site:#x}", f"address in {self.image.section_of(site)} (data)", None, "data"))
        if target == self.image.entry:
            refs.append(("pe:entry", "PE header AddressOfEntryPoint", None, "pe"))
        for name, rva in self.image.exports:
            if rva == target:
                refs.append((f"pe:export:{name}", f"export {name}", None, "pe"))
        for path, line, how in self.db.src_refs.get(target, []):
            refs.append((f"src:{path}:{line}", f"our code names the original ({how})", None, "src"))
        for path in sorted(self.db.host_refs.get(target, set())):
            refs.append((f"host:{path}", "the host names the RVA", None, "host"))
        for where in getattr(self.db, "bin_refs", {}).get(target, []):
            refs.append((f"bin:{where.split(' ')[1]}", f"our binary holds its address ({where})", None, "src"))
        return refs

    def function_in_effect(self, func, target):
        """(True/False, why) for code in `func` referencing `target`."""
        if func == target:
            return True, "the target itself (recursion)"
        if func in self.db.hooked:
            return True, f"{func:#x} is hooked"
        e = self.db.status.get(func, {})
        if func in self.assume or (e.get("in_effect") == "callers" and e.get("status") in tsdb.OURS):
            v = self.verdict(func)
            if v is None:
                return False, f"{func:#x} is replaced through its callers, but its check is part of a cycle"
            if v["open"]:
                return False, f"{func:#x} is replaced through its callers, but its own check has {len(v['open'])} open"
            return True, f"{func:#x} is replaced through its callers (its check is clean)"
        return False, f"{func:#x} {self.db.name(func)} is original code ({e.get('status', 'no status')}, not hooked)"

    def never_runs(self, func):
        """The verdict's test for a code reference from original code `func`: dead() with at least one reference seen
        (a function nothing visibly references may be reached through an address computed at run time, so it stays
        open here), not hooked and not replaced through its own callers (their own checks decide)."""
        e = self.db.status.get(func, {})
        if func in self.db.hooked or func in self.assume or (e.get("in_effect") == "callers" and e.get("status") in tsdb.OURS):
            return False
        return bool(self.references(func)) and self.dead(func)

    def vtable_covered(self, vt):
        if vt[0] in self._vt:
            return self._vt[vt[0]]
        if vt[0] in self._vt_busy:
            return True, "cycle"  # assumed dead while it is being decided (callers of the cycle decide)
        self._vt_busy.add(vt[0])
        bad = self.db.vtable_uses.get(vt[0], [])
        if bad:
            result = (False, f"our code holds the original vtable {vt[0]:#x}, so objects may carry it: "
                             f"{'; '.join(bad[:3])}" + (f" (+{len(bad) - 3} more)" if len(bad) > 3 else ""))
        else:
            open_, objects = [], []
            for site in self.image.relocations().get(vt[0], []):
                func = self.db.function_at(site)
                obj = self.static_object(site) if func is None else None
                if obj:
                    # A constant-initialised static object's vptr word: live unless every reference to the object is
                    # in code in effect or dead.
                    live = self.object_open(obj)
                    open_ += [f"{site:#x} ({x})" for x in live]
                    objects.append(f"{obj[1]:#x}")
                    continue
                ok = func is not None and (self.function_in_effect(func, vt[0])[0] or self.dead(func))
                if not ok:
                    open_.append(f"{site:#x}" + (f" in {func:#x}" if func is not None else ""))
            result = (not open_, f"vtable {vt[0]:#x}: " + ("every reference is in replaced code, or in original code "
                                                          "reached only through vtables no live object carries"
                                                          + (f"; the static object(s) {', '.join(objects)} carrying it "
                                                             "are referenced only from such code" if objects else "")
                                                          if not open_ else
                                                          f"referenced from original code at {', '.join(open_[:4])}"))
        self._vt_busy.discard(vt[0])
        if not self._vt_busy and not self._dead_busy:  # a result that assumed something dead isn't final
            self._vt[vt[0]] = result
        return result

    def dead(self, func):
        """True when original code `func` never runs: every reference to it is a slot of a vtable no live object
        carries, or code in a function that is in effect or dead itself (e.g. the original constructor only the hooked
        Create called, a destructor that re-stores its class's vtable). Data, PE, src/ and host references keep it
        alive. Inside a cycle, functions being decided count as dead (a cycle nothing live enters never runs)."""
        if func in self._dead:
            return self._dead[func]
        if func in self._dead_busy:
            return True
        if len(self._dead_busy) >= self.DEAD_DEPTH:
            return False  # too far from the vtable to decide cheaply: assume it runs
        self._dead_busy.add(func)
        result = True
        for site, _, owner, kind in self.references(func):
            table = self.table_of(int(site[4:], 16)) if kind == "data" and site.startswith("abs:") else None
            if kind == "vtable":
                ok = self.vtable_covered(owner)[0]
            elif kind == "code" and owner is not None:
                ok = owner == func or self.function_in_effect(owner, func)[0] or self.dead(owner)
            elif table:
                ok = not self.table_open(table)
            else:
                ok = False
            if not ok:
                result = False
                break
        self._dead_busy.discard(func)
        if not self._dead_busy and not self._vt_busy:
            self._dead[func] = result
        return result

    def hook_deps(self, target):
        """The hooked functions whose hooks a verdict on `target` rests on: with every one of them installed (and any
        other hook off), nothing reaches the original. A trap on a dropped adapter is armed only while all of them are
        installed ("trap": "Hooks.A=1&Hooks.B=1"); status.py --check, traps.py --sync, callers.py --vtable-only and
        stagea.py all take the trap from here (trap_hooks()).

        The rule (lane LP, 2026-10-23), from what must hold for the original to be unreachable (docs/verification.md, "In
        effect through callers"): every reference to `target` is cut, where
          - a vtable slot is cut when every piece of code storing (referencing) that vtable is cut and our code holds the
            vtable nowhere;
          - code in a hooked function is cut by its hook: the nearest hook counts. Except a hook reached only through
            vtable slots (a destructor re-storing its class's vtable, Release, an adjustor thunk; also code reached that way
            through unhooked callers): its hook is beside the point when its own references are cut without it (the
            vtables decide, usually the class's constructor hook). When they aren't (it is reached from another class's
            live vtable: cDeviceStateDX's constructor, called from cGraphicSystemDX::Init's slot; cTSAudioEvent's
            constructors, called from cTSAudioSoundInstance's), its own hook is the cut;
          - code in a function replaced through its callers is cut by the hooks its own check rests on;
          - code in original code is cut when every reference to that function is cut (it never runs; dead()'s rule);
          - a slot of a declared data table is cut when every reference to the table is cut, as for a static object.
            Anything else reaching it (data, the PE header, our sources naming it, code outside any known function)
            leaves it live, and so does a vtable our code holds.
        At the top level only vtable and code references count (the verdict decides the others: handled sites, XC slots);
        a top-level reference the verdict covers but this walk finds live is recorded in `uncut` (status --check reports
        it: the two rules disagree).
        Before lane LP the climb past a vtable-only hook ignored live ends (returned no hook for them), so a trap could
        arm with the original still reachable (cTSAudioEvent's 48 slots rested on the audio managers' Create hooks while
        cTSAudioSoundInstance's live vtable reaches the constructor), or rest on none (cDeviceStateDX)."""
        return set(self._cut(("deps", target))[0])

    def trap_hooks(self, target):
        """The hook names hook_deps() gives, sorted: a trap's Hooks. terms ("Hooks.A=1&Hooks.B=1")."""
        return sorted(self.db.hook_names.get(h, f"{h:#x}") for h in self.hook_deps(target))

    def vtables_only(self, func, busy=frozenset()):
        """func is reached only through vtable slots (and code reached the same way, unhooked): a destructor that
        re-stores the vtable, Release, adjustor thunks. hook_deps() then tries its references before its own hook."""
        if func in self._only:
            return self._only[func]
        if func in busy:
            return True
        ok = True
        for _, _, owner, kind in self.references(func):
            if kind == "vtable":
                continue
            if kind == "code" and owner is not None and (owner == func or (
                    owner not in self.db.hooked and self.vtables_only(owner, busy | {func}))):
                continue
            ok = False
            break
        if not busy:
            self._only[func] = ok
        return ok

    def _cut(self, node):
        """(frozenset of hooks, or None when something live reaches it; low) for node ("deps", rva): hook_deps; ("f",
        rva): the hooks that keep original code from running; ("vt", start): the hooks that keep a vtable off every
        object; ("table", site): the same for the data table with that row word. A node met again while it is being
        decided counts as cut by nothing ({}: a cycle nothing live enters never runs); `low` is the depth of the outermost such node a result leaned on, and only results that leaned on
        nothing still open are kept."""
        if node in self._cut_memo:
            return self._cut_memo[node], float("inf")
        if node in self._cut_stack:
            return frozenset(), self._cut_stack[node]
        depth = len(self._cut_stack)
        self._cut_stack[node] = depth
        low, hooks = float("inf"), set()
        for r, lo in self._cut_children(node):
            low = min(low, lo)
            if r is None:
                hooks = None  # live: final whatever the cycle assumptions (they only make things deader)
                break
            hooks |= r
        del self._cut_stack[node]
        result = None if hooks is None else frozenset(hooks)
        if result is None or low >= depth:
            self._cut_memo[node] = result
            low = float("inf")
        return result, low

    def _cut_children(self, node):
        """The _cut() results a node is the union of (None: live)."""
        kind, at = node
        live = (None, float("inf"))
        if kind == "vt":
            if self.db.vtable_uses.get(at):
                yield live  # our code holds it: objects may carry it whatever the hooks
                return
            for site in self.image.relocations().get(at, []):
                func = self.db.function_at(site)
                obj = self.static_object(site) if func is None else None
                if obj:
                    # A static object's vptr word: cut by the cuts of every reference to the object (code only; any
                    # other reference, or none at all, keeps it live, as object_open() decides for the verdict).
                    refs = self.object_references(obj)
                    if not refs:
                        yield live
                    for _, _, owner, kind in refs:
                        yield self._cut_code(owner, None) if kind == "code" and owner is not None else live
                    continue
                yield self._cut_code(func, None) if func is not None else live
            return
        if kind == "table":
            refs = self.table_references(self.table_of(at))
            if not refs:
                yield live
            for _, _, owner, rkind in refs:
                yield self._cut_code(owner, None) if rkind == "code" and owner is not None else live
            return
        e = self.db.status.get(at, {})
        handled = (set(e.get("callers_handled", {})) | set(self.handled.get(at, {}))) if kind == "deps" else set()
        # Original code nothing visibly references counts as never running, as in dead() (the verdict's rule; only the
        # owner of a reference to the target itself must have one, never_runs()).
        for site, _, owner, rkind in self.references(at):
            if site in handled:
                continue
            table = self.table_of(int(site[4:], 16)) if rkind == "data" and site.startswith("abs:") else None
            if rkind == "vtable":
                r = self._cut(("vt", owner[0]))
            elif rkind == "code" and owner is not None:
                r = self._cut_code(owner, at)
            elif table:
                r = self._cut(("table", min(table.slots)))
            elif kind == "deps":
                continue  # data, PE, src/, host/: the verdict decides (handled, an XC slot, or open)
            else:
                r = live
            if kind == "deps" and r[0] is None:
                # Live, but the verdict calls it covered (or open: then status --check reports that): no hook cuts it.
                self.uncut.setdefault(at, []).append(site)
                r = (frozenset(), r[1])
            yield r

    def _cut_code(self, owner, of):
        """_cut() for a reference in the code of `owner` (to `of`; None for a vtable store)."""
        if owner == of:
            return frozenset(), float("inf")  # recursion
        if owner in self.db.hooked:
            if self.vtables_only(owner):
                r, low = self._cut(("f", owner))
                if r is not None:
                    return r, low  # dead without its hook: the hooks that keep its vtables off decide
                return frozenset({owner}), low
            return frozenset({owner}), float("inf")  # the nearest hook: with it installed, this code doesn't run
        e = self.db.status.get(owner, {})
        if owner in self.assume or (e.get("in_effect") == "callers" and e.get("status") in tsdb.OURS):
            # Replaced through its own callers: off only while the hooks its own check rests on are in (e.g. a creator
            # trapped under another class's constructor switch).
            return self._cut(("deps", owner))
        return self._cut(("f", owner))

    def verdict(self, target):
        """{"rva", "name", "references": [{site, what, covered, why}], "open": [site...]}; None in a cycle."""
        if target in self._verdicts:
            return self._verdicts[target]
        if target in self._busy:
            return None
        self._busy.add(target)
        handled = dict(self.db.status.get(target, {}).get("callers_handled", {}))
        handled.update(self.handled.get(target, {}))
        rows = []
        refs = self.references(target)
        # An XC static initialiser our build owns: its slot in the image's XC table and its row in an owned list
        # (src/app/StaticInit*.cpp, the RVA as a hex literal the runner checks the slot against) go together.
        xc = any(kind == "data" and XC_TABLE[0] <= int(site[4:], 16) < XC_TABLE[1] for site, _, _, kind in refs
                 if site.startswith("abs:")) and any(kind == "src" and OWNED_LIST.match(site) and what.endswith("(hex literal)")
                                                     for site, what, _, kind in refs)
        for site, what, owner, kind in refs:
            via, dead, extra = None, False, {}
            if xc and site not in handled and (
                    (kind == "data" and site.startswith("abs:") and XC_TABLE[0] <= int(site[4:], 16) < XC_TABLE[1])
                    or (kind == "src" and OWNED_LIST.match(site) and what.endswith("(hex literal)"))):
                ok, why = True, (f"XC static initialiser our build owns: the host's runner runs ours in this slot (owned "
                                 f"list src/app/StaticInit*.cpp) while {XC_TRAP}")
                rows.append({"site": site, "what": what, "covered": ok, "why": why, "via": None, "dead": False, "xc": True})
                continue
            if kind == "src":
                ok, why = False, "our code must not call or name the original"
            elif site in handled:
                ok, why = True, f"handled: {handled[site]}"
            elif kind == "code" and owner is not None:
                ok, why = self.function_in_effect(owner, target)
                if ok and owner != target and owner not in self.db.hooked:
                    via = f"{owner:#x}"  # in effect only where that function's own replacement is (its switch)
                elif not ok and self.never_runs(owner):
                    # Original code that never runs (an adjustor thunk only a dead vtable holds, a constructor only a
                    # hooked factory calls), as vtable_covered() accepts it; hook_deps() names the hooks this rests on.
                    ok, why, dead = True, (f"{owner:#x} {self.db.name(owner)} is original code that never runs (reached "
                                           "only through vtables no live object carries, or from code in effect or dead)"), True
            elif kind == "vtable":
                ok, why = self.vtable_covered(owner)
            elif kind == "data" and site.startswith("abs:") and self.table_of(int(site[4:], 16)):
                table = self.table_of(int(site[4:], 16))
                live = self.table_open(table)
                readers = sorted({o for _, _, o, k in self.table_references(table) if k == "code" and o is not None})
                ok, why = not live, (f"a slot of the declared data table {table.start:#x}, which only code in effect or "
                                     "dead reads" if not live else f"read from live code or data: {'; '.join(live[:4])}")
                # "vias": the readers in effect only where their own replacement is, as "via" for code.
                extra = {"table": f"{table.start:#x}", "readers": [f"{o:#x}" for o in readers],
                         "vias": [f"{o:#x}" for o in readers if o not in self.db.hooked and (
                             o in self.assume or self.db.status.get(o, {}).get("in_effect") == "callers")]}
            else:
                ok, why = False, "not neutralised (list it in callers_handled if the host takes care of it)"
            rows.append({"site": site, "what": what, "covered": ok, "why": why, "via": via, "dead": dead, **extra})
        unused = sorted(set(handled) - {r["site"] for r in rows})
        v = {"rva": f"{target:#x}", "name": self.db.name(target), "references": rows,
             "open": [r["site"] for r in rows if not r["covered"]], "unused_handled": unused}
        self._busy.discard(target)
        if not self._vt_busy and not self._busy and not self._dead_busy:
            self._verdicts[target] = v
        return v


CHECK_FIELD = re.compile(r"callers\.py v(\d+) \d{4}-\d{2}-\d{2}")


def check_status(entries, image=None, db=None, table=None, sources=None, found=None, funcs=None):
    """(problems, notes) for status entries [(file, entry)] that say "in_effect" (tools/status.py --check). image, db
    and table (the trap table's path) default to the real ones; sources, found and funcs as for Db. Without a db, the
    entries are taken for all of them (Db's status)."""
    problems, notes = [], []
    callers = []
    for f, e in entries:
        mode = e.get("in_effect", "hook")
        if mode == "hook":
            continue
        where = f"{f}: {e.get('rva')} {str(e.get('name'))[:40]}"
        if mode != "callers":
            problems.append(f"{where}: unknown \"in_effect\" {mode!r} (\"hook\" or \"callers\")")
            continue
        if e.get("status") not in tsdb.OURS:
            problems.append(f"{where}: \"in_effect\": \"callers\" on a {e.get('status')} entry")
        m = CHECK_FIELD.fullmatch(str(e.get("callers_check", "")))
        if not m:
            problems.append(f"{where}: \"in_effect\": \"callers\" needs \"callers_check\": \"callers.py vN YYYY-MM-DD\"")
        elif int(m.group(1)) != VERSION:
            notes.append(f"{where}: callers_check names v{m.group(1)}, the tool is v{VERSION}: rerun and update it")
        trap = parse_trap(e.get("trap"))
        if not trap:
            problems.append(f"{where}: \"in_effect\": \"callers\" needs \"trap\": \"Section.Key=v1,v2\" (the host switch "
                            "values under which the replacement is in use and the original is trapped; several switches, "
                            "all required: \"A.B=1&C.D=1\", each switch once)")
        elif any(0 in t[2] for t in trap):
            notes.append(f"{where}: trap {e['trap']} includes 0, usually a switch's default: is the replacement on by default?")
        if not isinstance(e.get("callers_handled", {}), dict):
            problems.append(f"{where}: \"callers_handled\" must be an object {{site: reason}}")
        callers.append((where, e, trap))
    path = Path(table) if table else tsdb.ROOT / TRAP_TABLE
    stale = f"{TRAP_TABLE} is out of date: tools/py -I tools/callers.py --write-traps"
    if not callers:
        if not path.exists() or path.read_text(encoding="utf-8") != trap_table_text([]):
            problems.append(stale)
        return problems, notes
    image = image or Image()
    db = db or Db(image=image, sources=sources, found=found, funcs=funcs, entries=entries)
    checker = Checker(image, db)
    problems += checker.table_problems()
    traps = {int(e["rva"], 16): t for _, e, t in callers}
    by_name = {n: r for r, n in db.hook_names.items()}
    note = getattr(db, "binary_note", None)
    if note:
        # Without a current scan of our build, a vtable our code holds through a computed constant can look dead: a
        # problem only where a verdict rests on a vtable slot being covered.
        on_vtables = [where for where, e, _ in callers if any(
            r["covered"] and not r["why"].startswith("handled:") and (
                r["what"].startswith("slot ") or r.get("dead") or r.get("table"))
            for r in checker.verdict(int(e["rva"], 16))["references"])]
        if on_vtables:
            problems.append(f"{note} ({len(on_vtables)} entries rest on vtable slots being dead, e.g. {on_vtables[0]})")
        else:
            notes.append(note)
    for where, e, trap in callers:
        rva = int(e["rva"], 16)
        if rva in db.hooked:
            problems.append(f"{where}: \"in_effect\": \"callers\" but also hooked (src/hook/tables)")
        if db.funcs.get(rva, {}).get("size", 0) < 5:
            problems.append(f"{where}: under 5 bytes: the trap's jump doesn't fit")
        v = checker.verdict(rva)
        for r in v["references"]:
            if not r["covered"]:
                problems.append(f"{where}: open reference {r['site']} ({r['what']}): {r['why']} (tools/callers.py {e['rva']})")
            elif trap:
                for via in ([r["via"]] if r["via"] else []) + r.get("vias", []):
                    other = traps.get(int(via, 16))
                    if not other or not trap_implies(trap, other):
                        problems.append(f"{where}: {r['site']} is covered by {via}, which isn't replaced under every "
                                        f"value of this entry's trap {e.get('trap')} (its trap: "
                                        f"{'none' if not other else trap_text(other)})")
        if trap and any(r.get("xc") for r in v["references"]) and not trap_implies(trap, parse_trap(XC_TRAP)):
            problems.append(f"{where}: its XC slot is covered by the host's runner only under {XC_TRAP}; the trap "
                            f"{e.get('trap')} can be armed without it")
        for s in v["unused_handled"]:
            notes.append(f"{where}: callers_handled lists {s}, which is not a reference (stale?)")
        if "absorbed_by" in e:
            # Reached only through declared tables, and read only by the functions the entry says took its work.
            readers = {x for r in v["references"] for x in r.get("readers", ())}
            if not v["references"] or not all(r.get("table") for r in v["references"]):
                problems.append(f"{where}: \"absorbed_by\" needs every reference to be a declared data table's slot")
            elif readers != set(e["absorbed_by"]):
                problems.append(f"{where}: \"absorbed_by\" {e['absorbed_by']}, but its tables' readers are "
                                f"{sorted(readers)}")
        # A verdict resting on hooks (vtables only hooked code stores, hooked callers) holds only while they are installed.
        deps = checker.hook_deps(rva)
        names = checker.trap_hooks(rva)
        for site in checker.uncut.get(rva, []):
            problems.append(f"{where}: {site} is covered by the check, but original code reaching it runs whatever the "
                            f"hooks (Checker.hook_deps found no hook that cuts it): tools/callers.py {e['rva']}")
        hook_terms = [t for t in trap or [] if t[0] == "Hooks"]
        if hook_terms:
            named, bad = set(), False
            for t in hook_terms:
                hook = by_name.get(t[1])
                if hook is None:
                    problems.append(f"{where}: trap {e.get('trap')}: no hook named {t[1]} in src/hook/tables")
                    bad = True
                elif t[2] != {1}:
                    problems.append(f"{where}: trap {e.get('trap')}: a hook's switch is 1 while it is installed (=1 only)")
                    bad = True
                named.add(hook)
            if not bad and named != deps:
                problems.append(f"{where}: trap {e.get('trap')}, but the check rests on the hooks {', '.join(names) or 'none'}"
                                " (callers.py --vtable-only): the trap must name exactly the hooks it rests on, joined "
                                "with & (\"Hooks.A=1&Hooks.B=1\")")
        elif deps:
            notes.append(f"{where}: the check rests on the hooks {', '.join(names)}: with one turned off in the ini, "
                         f"the original may run while the trap {e.get('trap')} is armed")
    if not path.exists() or path.read_text(encoding="utf-8") != trap_table_text(trap_rows(db, image)):
        problems.append(stale)
    return problems, notes


def trap_rows(db, image):
    """(rva, name, checkLen, crc, section, key, mask[, also]) for status entries replaced through their callers; `also`
    ([(section, key, mask)], the conjunction's further switches) only when the trap has several."""
    import zlib
    rows = []
    for rva, e in sorted(db.status.items()):
        if e.get("in_effect") != "callers":
            continue
        trap = parse_trap(e.get("trap"))
        if not trap:
            continue
        size = db.funcs.get(rva, {}).get("size", 0)
        n = min(size, 32)
        crc = zlib.crc32(image.bytes_at(rva, n)) & 0xFFFFFFFF
        key = re.sub(r"[^A-Za-z0-9]+", "_", db.name(rva).replace("::", "_")).strip("_")
        masks = [(s, k, sum(1 << v for v in vs)) for s, k, vs in trap]
        row = (rva, key, n, crc) + masks[0]
        rows.append(row + (masks[1:],) if len(masks) > 1 else row)
    return rows


def trap_table_text(rows):
    lines = ["// Generated by tools/callers.py --write-traps from db/status (entries with \"in_effect\": \"callers\"): don't edit.",
             "// Each trap patches the original's entry with a jump to a stub that logs and stops the process, armed only when",
             "// the host reports the named switch at one of the listed values (src/hook/traps.h); a trap on several switches",
             "// (\"A.B=1&C.D=1\") lists the further ones in a kAlso_ array, and arms only when every one holds.",
             '#include "hook/traps.h"', ""]

    def text(section, name, mask):
        return f"{section}.{name}=" + ",".join(str(v) for v in range(32) if mask >> v & 1)

    table, arrays = [], {}  # rows with the same further terms share one array, named after the first
    for row in rows:
        rva, key, n, crc, section, name, mask = row[:7]
        also = row[7] if len(row) > 7 else []
        comment = " & ".join(text(*t) for t in [(section, name, mask)] + list(also))
        if also:
            terms = ", ".join(f'{{"{s}", "{k}", {m:#x}}}' for s, k, m in also)
            if terms not in arrays:
                arrays[terms] = f"kAlso_{rva:x}"
                lines.append(f"static const TrapTerm {arrays[terms]}[] = {{{terms}, {{nullptr, nullptr, 0}}}};")
            table.append(f'    {{"{key}", {rva:#x}, {n}, 0x{crc:08x}, "{section}", "{name}", {mask:#x}, {arrays[terms]}}}, // {comment}')
        else:
            table.append(f'    {{"{key}", {rva:#x}, {n}, 0x{crc:08x}, "{section}", "{name}", {mask:#x}}}, // {comment}')
    if any(len(r) > 7 for r in rows):
        lines.append("")
    lines += ["extern const TrapSpec kTrapTable[] = {"] + table
    lines += ["    {nullptr, 0, 0, 0, nullptr, nullptr, 0},", "};", ""]
    return "\n".join(lines)


def vtable_only(checker, rvas):
    """[{rva, name, droppable, references, keep: [why...]}] for each RVA: droppable when every reference is a slot of a
    vtable no live object carries."""
    out = []
    for rva in rvas:
        v = checker.verdict(rva)
        refs = v["references"]
        others = [r for r in refs if not (r["covered"] and (r.get("dead") or (
            r["site"].startswith("abs:") and r["what"].startswith("slot "))))]
        keep = [f"{'covered' if r['covered'] else 'OPEN'} {r['site']} {r['what']}: {r['why']}" for r in others]
        row = {"rva": v["rva"], "name": v["name"], "droppable": not keep, "references": len(refs), "keep": keep,
               "open": sum(not r["covered"] for r in others)}
        if not keep:
            hooks = checker.trap_hooks(rva)
            row["hooks"] = hooks  # the hooks the verdict rests on (Checker.hook_deps)
            # armed only while every one is installed; no hook at all: nothing keeps the vtable off, no trap
            row["trap"] = "&".join(f"Hooks.{h}=1" for h in hooks) or None
        out.append(row)
    return out


def group_rvas(db, classes, groups):
    """Hooked functions in the named classes' vtables and status groups, in order, without repeats."""
    rvas = []
    if classes:
        by_name = {c["name"]: c for c in tsdb.jsonl("classes.jsonl")}
        for name in classes:
            if name not in by_name:
                sys.exit(f"--class {name}: not in db/classes.jsonl (exact name, as tools/classinfo.py prints it)")
            for vt in by_name[name].get("vftables", []):
                rvas += [int(s, 16) for s in vt.get("entries", [])]
    for g in groups:
        path = tsdb.ROOT / "db/status" / (g if g.endswith(".jsonl") else g + ".jsonl")
        if not path.exists():
            sys.exit(f"--group {g}: no {path}")
        rvas += [int(json.loads(l)["rva"], 16) for l in open(path, encoding="utf-8") if l.strip()]
    seen, out = set(), []
    for r in rvas:
        if r in db.hooked and r not in seen:
            seen.add(r)
            out.append(r)
    return out


def report(v):
    state = "all references covered" if not v["open"] else f"{len(v['open'])} OPEN"
    print(f"{v['rva']} {v['name']}: {state} ({len(v['references'])} references)")
    for r in v["references"]:
        print(f"  {'ok  ' if r['covered'] else 'OPEN'} {r['site']:24} {r['what']}")
        print(f"       {'':24} -> {r['why']}")
    for s in v["unused_handled"]:
        print(f"  note: callers_handled lists {s}, which is not a reference (stale?)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rvas", nargs="*")
    ap.add_argument("--assume", nargs="+", default=[])
    ap.add_argument("--handled", action="append", default=[], help="<rva>:<site>=<reason>")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--write-traps", nargs="?", const=TRAP_TABLE)
    ap.add_argument("--check-traps", nargs="?", const=TRAP_TABLE)
    ap.add_argument("--vtable-only", action="store_true")
    ap.add_argument("--class", dest="classes", action="append", default=[])
    ap.add_argument("--group", dest="groups", action="append", default=[])
    ap.add_argument("--binary", default=None, help=f"our built host to scan (default {ourrefs.DEFAULT_EXE})")
    ap.add_argument("--no-binary", action="store_true", help="skip the binary scan (source rules only)")
    args = ap.parse_args()
    if not (args.rvas or args.write_traps or args.check_traps or args.vtable_only):
        ap.error("give RVAs, --vtable-only, --write-traps or --check-traps")
    image = Image()
    db = Db(binary=False if args.no_binary or ((args.write_traps or args.check_traps) and not args.rvas) else args.binary,
            image=image)
    if args.no_binary:
        print("warning: --no-binary: game addresses our code computes (kBase + 8, a constant from another file) are "
              "unseen", file=sys.stderr)
    elif db.binary_note and not (args.write_traps or args.check_traps):
        print(f"warning: {db.binary_note}", file=sys.stderr)
    if args.vtable_only:
        rvas = group_rvas(db, args.classes, args.groups) + [int(r, 16) for r in args.rvas]
        if not rvas:
            ap.error("--vtable-only: no hooked functions in the given --class / --group (or RVAs)")
        rows = vtable_only(Checker(image, db), rvas)
        for r in rows:
            if args.json:
                print(json.dumps(r))
            elif r["droppable"] and r["trap"]:
                print(f"{r['rva']:>9} {r['name'][:50]:50} vtable slots only ({r['references']}): its hook can go, "
                      f"\"trap\": \"{r['trap']}\"")
            elif r["droppable"]:
                print(f"{r['rva']:>9} {r['name'][:50]:50} vtable slots only ({r['references']}), resting on no hook: "
                      "nothing names a switch for its trap, keep the hook")
            else:
                print(f"{r['rva']:>9} {r['name'][:50]:50} keep: {len(r['keep'])} other reference(s), {r['open']} open"
                      + ("" if r["open"] else " (the rest from hooked or replaced code: droppable only while that stays)"))
                for k in r["keep"][:6]:
                    print(f"          {k}")
        if not args.json:
            print(f"{sum(r['droppable'] for r in rows)} of {len(rows)} hooked functions are referenced only by vtables no "
                  f"live object carries ({check_version()})")
        return
    if args.write_traps or args.check_traps:
        path = tsdb.ROOT / (args.write_traps or args.check_traps)
        rows = trap_rows(db, image)
        text = trap_table_text(rows)
        if args.write_traps:
            # Only on change: a rewrite makes the file newer than the build, and the binary scan then distrusts it.
            unchanged = path.exists() and path.read_text(encoding="utf-8") == text
            if not unchanged:
                path.write_text(text, encoding="utf-8", newline="\n")
            print(f"{path}: {len(rows)} traps" + (" (unchanged)" if unchanged else ""))
        elif not path.exists() or path.read_text(encoding="utf-8") != text:
            sys.exit(f"{path} is out of date: run tools/py -I tools/callers.py --write-traps")
        if not args.rvas:
            return
    handled = {}
    for h in args.handled:
        m = re.fullmatch(r"(0x[0-9a-fA-F]+):([^=]+)=(.*)", h)
        if not m:
            ap.error(f"--handled {h!r}: expected <rva>:<site>=<reason>")
        handled.setdefault(int(m.group(1), 16), {})[m.group(2)] = m.group(3)
    targets = [int(r, 16) for r in args.rvas]
    checker = Checker(image, db, assume=[int(r, 16) for r in args.assume] + targets, handled=handled)
    verdicts = [checker.verdict(t) for t in targets]
    for v in verdicts:
        if args.json:
            print(json.dumps(v))
        else:
            report(v)
    if not args.json:
        print(f"{check_version()}; can't see computed addresses, code written at run time, references from other "
              "modules, jumps into a function's middle")
    sys.exit(1 if any(v["open"] for v in verdicts) else 0)


if __name__ == "__main__":
    main()
