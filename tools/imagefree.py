"""Image-free indicator: how much of our verified code could already link and run without the game image, and what
blocks the rest (notes/decisions.md 2026-11-09, process review item 6). A measure for the weekly reflection, never a
target and never a lane picker (docs/roadmap.md, Metrics).

Usage: tools/py -I tools/imagefree.py [--build <dir>] [--config Release] [--top 30] [--json <out>] [--no-save]
                                      [--rva <rva>...] [--program <name>]... [--no-lower]

Reads our compiled objects (the build's ts2core and ts2hooktables object folders, plus the standalone/ objects that
define owned statics) and db/; no game bytes apart from the import directory's names. The build is the main
checkout's by default (tools/tsdb.py main_root()/build): it needs current ts2core and ts2hooktables objects, which
any build of ts2host or difftest leaves (`Invoke-Build ts2host`), and the objects of one standalone program
(`Invoke-Build render_object`): owned_statics.obj defines the owned statics, without it they count as image
references. About a minute (the lower bound's second closure is a part of it; --no-lower skips it).

How it works (lane 261109t: object level, the linker's own model, rather than db/status original_calls):
  - Graph: one node per COMDAT section of every object (with /Gy every function, vtable, literal and template
    instance is one), one per symbol in a non-COMDAT data section; edges are the relocations, as /OPT:REF follows
    them. An associative COMDAT (EH tables, a template static's initialiser) hangs off its parent. External names
    resolve to every object's definition (the union of COMDAT copies: the linker keeps one, which one depends on link
    order, so the measure takes the worst); statics (unnamed-namespace helpers, one copy per object) resolve inside
    their object. A function inlined or folded into its callers has no node of its own: it is part of theirs.
    Debug, .drectve, .chks64 and .sxdata sections are not code paths.
  - Object coupling: a linked ts2core object always brings its non-COMDAT .CRT$X* sections (the dynamic
    initialisers of its globals run whenever the object is linked), so every node of the object reaches them. Hook
    table objects get none: their registrars are host glue, not part of the measure.
  - Image references (as tools/noimage.py reads them): `_ts2_rva_0` + addend (GameAddress, ImportFunction, the game
    vtables hook-shaped classes store, and GameBase() itself at RVA 0), `OriginalEntry<RVA>` (OriginalFunction), and
    an owned static `_ts2_rva_<hex>` no standalone/ object defines (a defined one resolves to that definition, whose
    own references count). A name no object defines is library (CRT, STL, Windows), not the image; /alternatename
    aliases resolve to their target.
  - Roots: a `verified` entry's RVA maps to our code by (1) the hook tables' kHooks relocations (the exact replacement
    or adapter; an adapter forwards, so its closure is the member's), else (2) a vtable tie: our vtable of the same
    class and subobject offset (RTTI name exact, or unique without namespaces, as matchcheck v8) and the original's
    slot count holds our function where the original vftable holds the RVA (dropped adapters, folded shared_vf
    bodies, adjustor thunks only in our vtables; several of our vtables: all of them). Entries neither finds
    (non-virtual code in effect through callers, host-started code) are "not located": counted apart, outside the
    share.
  - A function is image-free when its closure reaches no image reference. Blockers: per image reference, the verified
    bytes whose closure reaches it ("any") and those for which it is the only one left ("only").
  - Lower bound (one line and one column): the same closure with an edge from every member function to its own
    class's vtables of ours, for slot calls through `this` that leave no relocation. It over-reaches (every slot of
    the class, and the class's whole vtable family), so it bounds the share from below; blockers, the saved run and
    --program use the static model.

--program <name> checks the model against a linked standalone program (render_object, gmdc_decode): its map
(<build>/<config>/<name>.map) says what it links, tools/noimage.py (run on the same map and objects) its image
references. Each verified function the program links must reach only references the program has and only external
symbols the program links; each linked verified function noimage names as a reference's owner must be blocked here.
--rva prints, for the given verified RVAs, how each maps to our code, its verdict and a reference path to each
blocker (for hand checks).
After the report, what changed against the previous run (build/imagefree.last.json of this checkout; --no-save
leaves it as it is).
"""
import argparse
import bisect
import json
import re
import struct
import subprocess
import sys
import time
from collections import Counter, defaultdict, deque
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import noimage  # noqa: E402
import origsyms  # noqa: E402
import tsdb  # noqa: E402

SKIP_SECTIONS = (".debug", ".drectve", ".chks64", ".sxdata", ".gfids", ".giats", ".gljmp", ".gehcont")
COUPLED = ".CRT$X"  # non-COMDAT sections a linked object always brings
SEL_ASSOCIATIVE = 5
DIR32 = noimage.IMAGE_REL_I386_DIR32
HOOK_ENTRY = 20  # HookSpec (src/hook/hooks.h): {const char *name, uint32_t rva, checkLen, checkCrc, void *replacement}
IMAGE_OBJECTS = ("gamebase.obj", "origsyms.obj")  # the image's providers inside ts2core (ts2core_noimage drops them)
RTTI_PLAIN = re.compile(r"\.\?A([UV])(\w+)(?:@\w+)*@@")
# a non-static member function: ?name@<class scope>@@<access A E I M Q U>, or a constructor, destructor or operator,
# whose code the class scope follows directly: ??0<class scope>@@..., ??_G<class scope>@@...
MEMBER = re.compile(r"\?(?:\?[0-9A-Z]|\?_[0-9A-Z]|[^?@$][^@]*@)(.+?)@@[AEIMQU]")
CAVEATS = """caveats (what the number does and does not mean):
  - It is the static link closure of our code: what /OPT:REF would keep to link the function, read from relocations.
    A virtual call through an interface pointer is no reference: a slot call on an object the image creates is not
    seen (a vtable of ours that still holds image slots is: it references them). "Image-free" means the function
    links without the image, not that every object handed to it at run time is ours. The lower bound adds slot
    calls through `this` to the function's own class, over-reaching.
  - Owned statics count as ours only if a standalone program's objects are in the build (render_object).
  - Run-time addresses (GameBase() + computed offsets, lookups by name, pointers read from game data) show only as
    the GameBase() call itself (RVA 0x0).
  - COMDAT copies are united (the worst copy counts) and a linked object brings its dynamic initialisers: the
    measure errs towards "blocked".
  - "Only" bytes become image-free once that reference is ours only if the replacement adds no references itself.
  - Verified entries not located in our objects are outside the share (counted on their own line)."""


def rtti_bare(mangled):
    """matchcheck.rtti_bare (v8 class pairing): the RTTI name without its qualifiers, None for templates and anonymous
    namespaces. Repeated so this tool doesn't import matchcheck's disassembler stack."""
    m = RTTI_PLAIN.fullmatch(mangled)
    return f".?A{m.group(1)}{m.group(2)}@@" if m else None


class Graph:
    """Nodes (COMDAT sections, symbols of non-COMDAT sections), relocation edges, direct image references."""

    def __init__(self):
        self.obj, self.name, self.edges, self.refs = [], [], [], []  # per node
        self.objects = []  # object index -> (path, kind)
        self.obj_nodes = defaultdict(list)
        self.defs = defaultdict(list)  # external name -> nodes defining it
        self.aliases = {}  # /alternatename symbol -> target
        self.owned_aliases = set()  # image symbols a standalone object sends to code (ours, or a library's)
        self.locate = {}  # (object index, symbol index) -> node
        self.regions = {}  # (object index, non-COMDAT section number) -> (sorted starts, nodes)
        self.coupled = defaultdict(list)  # object index -> nodes the object always brings
        self.pending = []
        self.parsed = []  # object index -> (sections, symbols)
        self.external = set()  # names referenced and defined by no object (library)

    def node(self, oi, name):
        self.obj.append(oi)
        self.name.append(name)
        self.edges.append(set())
        self.refs.append(set())
        n = len(self.obj) - 1
        self.obj_nodes[oi].append(n)
        return n

    def add_object(self, path, kind, data=None):
        """kind: core (ts2core), hooks (a hook table), owned (a standalone/ object: only its owned statics are
        exported, so its main() and helpers never stand in for ours)."""
        data = data if data is not None else Path(path).read_bytes()
        oi = len(self.objects)
        self.objects.append((str(path), kind))
        sections, symbols = noimage.read_coff_data(data)
        self.parsed.append((sections, symbols))
        exported = set()  # an owned object's names besides its owned statics: what they alias
        for src, dst in noimage.aliases_in(data).items():
            # a standalone object's aliases that matter here are the owned statics it maps to code or data
            # (owned_statics.cpp: `_ts2_rva_d9b470` = its own rzface cRZString vtable)
            if kind != "owned" or noimage.is_image_symbol(src):
                self.aliases[src] = dst
                exported.add(dst)
                if kind == "owned" and dst != noimage.TRAP_SYMBOL:
                    self.owned_aliases.add(src)
        owners = noimage.owner_symbols(symbols)
        secnode = {}
        for si, sec in enumerate(sections, start=1):
            if sec["name"].startswith(SKIP_SECTIONS):
                continue
            own = [n for v, n in owners.get(si, ())]
            if "comdat" in sec:
                secnode[si] = self.node(oi, own[0] if own else sec["name"])
                continue
            starts, nodes = [], []
            for value, n in owners.get(si, ()):
                if starts and starts[-1] == value:
                    continue
                starts.append(value)
                nodes.append(self.node(oi, n))
            if not starts or starts[0] > 0:
                starts.insert(0, 0)
                nodes.insert(0, self.node(oi, sec["name"]))
            self.regions[(oi, si)] = (starts, nodes)
            if sec["name"].startswith(COUPLED) and kind == "core":
                self.coupled[oi].extend(nodes)

        def at(si, offset):
            if si in secnode:
                return secnode[si]
            return self.at_offset(oi, si, offset)

        for idx, (name, value, secnum, typ, cls) in symbols.items():
            if secnum > 0:
                n = at(secnum, value)
                if n is None:
                    continue
                self.locate[(oi, idx)] = n
                if cls == 2 and (kind != "owned" or noimage.is_image_symbol(name) or name in exported):
                    self.defs[name].append(n)
        for si, sec in enumerate(sections, start=1):
            if sec["name"].startswith(SKIP_SECTIONS):
                continue
            sel = sec.get("comdat")
            if sel and sel[0] == SEL_ASSOCIATIVE:
                child, parent = at(si, 0), at(sel[1], 0)
                if parent is not None and sel[1] not in secnode:  # with a non-COMDAT section: kept with the object
                    self.coupled[oi].append(child)
                elif parent is not None:
                    self.edges[parent].add(child)
            for va, symidx, typ, addend in sec["relocs"]:
                target = symbols.get(symidx)
                src = at(si, va)
                if target is not None and src is not None:
                    self.pending.append((src, oi, symidx, target, addend, typ))
        return oi

    def at_offset(self, oi, si, offset):
        r = self.regions.get((oi, si))
        if r is None:
            return None
        starts, nodes = r
        return nodes[max(0, bisect.bisect_right(starts, offset) - 1)]

    def link(self):
        """Resolve the relocations into edges and image references; add the object-coupling hubs."""
        owned_defined = {n for n in self.defs if noimage.OWNED_SYMBOL.match(n)}
        for src, oi, symidx, (name, value, secnum, typ_, cls), addend, typ in self.pending:
            if secnum > 0:
                if cls == 2 and name in self.defs:
                    self.edges[src].update(self.defs[name])
                    continue
                sections = self.parsed[oi][0]
                if cls == 3 and value == 0 and name == sections[secnum - 1]["name"] and \
                        (oi, secnum) in self.regions:
                    dst = self.at_offset(oi, secnum, addend)  # a section symbol + addend: the symbol there
                else:
                    dst = self.locate.get((oi, symidx))
                if dst is not None:
                    self.edges[src].add(dst)
                continue
            if secnum < 0:
                continue  # absolute and debug symbols
            seen, first = set(), name
            while name not in self.defs and name in self.aliases and name not in seen:
                seen.add(name)
                name = self.aliases[name]
            if name in self.defs and name != noimage.GAME_BASE_SYMBOL:
                # ours, including an image symbol a standalone object provides (an owned static, a D3DX entry)
                self.edges[src].update(self.defs[name])
                continue
            if first in self.owned_aliases:
                # a standalone object sends it to a library none of our objects define (owned_graphics.cpp: the
                # D3DX entries to the SDK's d3dx9.lib), as the image itself links that library
                self.external.add(name)
                continue
            key = image_key(first, addend, owned_defined)  # an alias nobody resolves leaves the symbol as it was
            if key is not None:
                self.refs[src].add(key)
            else:
                self.external.add(first)
        self.pending = None
        # An object comes into the link for a symbol only it defines (a COMDAT copy other objects also hold doesn't
        # bring it: the linker takes the copy of an object it already has), so those nodes, and the static ones
        # only its own code reaches, lead to its initialisers.
        for oi, nodes in self.coupled.items():
            members = [n for n in self.obj_nodes[oi] if len(self.defs.get(self.name[n], ())) <= 1]
            hub = self.node(oi, "<initialisers of the object>")
            self.edges[hub].update(nodes)
            nodeset = set(nodes)
            for n in members:
                if n not in nodeset:
                    self.edges[n].add(hub)

    def add_class_edges(self):
        """The pessimistic variant: a non-static member function may call any virtual of its own class through
        `this` (a slot call, no relocation), so when the class's vtable is ours the member reaches it, and every slot.
        Returns {node: the edges it gained}, for remove_edges."""
        vt = defaultdict(list)
        for name, nodes in self.defs.items():
            if name.startswith("??_7") and "@@6B" in name:
                vt[name[4:name.index("@@6B")]].extend(nodes)
        added = {}
        for v in range(len(self.name)):
            m = MEMBER.match(self.name[v])
            if m and m.group(1) in vt:
                new = set(vt[m.group(1)]) - self.edges[v]
                if new:
                    self.edges[v] |= new
                    added[v] = new
        return added

    def remove_edges(self, added):
        for v, new in added.items():
            self.edges[v] -= new

    def symbol_nodes(self, oi, symidx):
        """The nodes a symbol of object oi stands for: its own definition, else every definition of its name."""
        name, value, secnum, typ, cls = self.parsed[oi][1][symidx]
        if secnum > 0 and cls != 2:
            n = self.locate.get((oi, symidx))
            return [n] if n is not None else []
        seen = set()
        while name not in self.defs and name in self.aliases and name not in seen:
            seen.add(name)
            name = self.aliases[name]
        return list(self.defs.get(name, ()))


def image_key(name, addend, owned_defined):
    """(kind, rva) for an image symbol (noimage.is_image_symbol), None otherwise or for an owned static we define."""
    if name == noimage.GAME_BASE_SYMBOL:
        return ("address", addend & 0xFFFFFFFF)
    m = noimage.OWNED_SYMBOL.match(name)
    if m:
        # an owned static no standalone object defines is the image's address, as a GameAddress is
        return None if name in owned_defined else ("address", (int(m.group(1), 16) + addend) & 0xFFFFFFFF)
    rva = origsyms.decode(name)
    return ("original", rva) if rva is not None else None


def hook_targets(g):
    """{rva: set of nodes} from every hook table's kHooks array, and the RVAs whose entry has no static replacement
    (a dynamic initialiser fills it). The layout matchcheck.hook_entries reads."""
    targets, dynamic = defaultdict(set), set()
    for oi, (path, kind) in enumerate(g.objects):
        if kind != "hooks":
            continue
        sections, symbols = g.parsed[oi]
        for idx, (name, value, secnum, typ, cls) in symbols.items():
            if secnum <= 0 or not name.startswith("?kHooks@"):
                continue
            sec = sections[secnum - 1]
            raw, rel = sec["raw"], {va: (si, t, a) for va, si, t, a in sec["relocs"]}
            end = min((v for n, v, s, t, c in symbols.values() if s == secnum and v > value and c in (2, 3)),
                      default=len(raw))
            for k in range(value, end - HOOK_ENTRY + 1, HOOK_ENTRY):
                rva = struct.unpack_from("<I", raw, k + 4)[0]
                r = rel.get(k + 16)
                nodes = g.symbol_nodes(oi, r[0]) if r and r[1] == DIR32 and r[2] == 0 else []
                if nodes:
                    targets[rva].update(nodes)
                else:
                    dynamic.add(rva)
    return targets, dynamic - set(targets)


def our_vtables(g):
    """[(RTTI class name, subobject offset, [slot nodes])] for every vtable `??_7...` in ts2core objects: the locator
    in the word before it, slots up to the end of its COMDAT section (the layout matchcheck.Checker.our_vtable reads)."""
    out = []
    for oi, (path, kind) in enumerate(g.objects):
        if kind != "core":
            continue
        sections, symbols = g.parsed[oi]
        for idx, (name, value, secnum, typ, cls) in symbols.items():
            if secnum <= 0 or not name.startswith("??_7"):
                continue
            sec = sections[secnum - 1]
            rel = {va: (si, t) for va, si, t, a in sec["relocs"]}
            col = rel.get(value - 4)
            if not col or col[1] != DIR32:
                continue
            cname, cval, csec = symbols[col[0]][:3]
            if csec <= 0:
                continue
            craw = sections[csec - 1]["raw"]
            crel = {va: (si, t) for va, si, t, a in sections[csec - 1]["relocs"]}
            p = cval + struct.unpack_from("<I", sec["raw"], value - 4)[0]
            td = crel.get(p + 12)
            if not td or td[1] != DIR32 or symbols[td[0]][2] <= 0 or p + 8 > len(craw):
                continue
            tsym = symbols[td[0]]
            traw = sections[tsym[2] - 1]["raw"]
            q = tsym[1] + struct.unpack_from("<I", craw, p + 12)[0] + 8
            if b"\0" not in traw[q:]:
                continue
            mangled = traw[q:traw.index(b"\0", q)].decode("latin-1")
            offset = struct.unpack_from("<I", craw, p + 4)[0]
            end = len(sec["raw"]) if "comdat" in sec else \
                min((v for n, v, s, t, c in symbols.values() if s == secnum and v > value), default=len(sec["raw"]))
            slots = []
            for k in range(value, end - 3, 4):
                r = rel.get(k)
                if r is None or r[1] != DIR32 or struct.unpack_from("<I", sec["raw"], k)[0]:
                    break
                slots.append(g.symbol_nodes(oi, r[0]))
            out.append((mangled, offset, slots))
    return out


def vtable_ties(g, classes):
    """{rva: set of nodes}: for each of our vtables paired with an original vftable (same class by exact RTTI name or
    by a bare name unique on both sides; same subobject offset and slot count), slot k's function for the RVA in the
    original's slot k."""
    game = {(c["mangled"], v["offset"]): v["entries"] for c in classes for v in c["vftables"]}
    game_bare = defaultdict(set)
    for m, _ in game:
        if rtti_bare(m):
            game_bare[rtti_bare(m)].add(m)
    ours = our_vtables(g)
    our_bare = defaultdict(set)
    for m, _, _ in ours:
        if rtti_bare(m):
            our_bare[rtti_bare(m)].add(m)
    ties = defaultdict(set)
    for mangled, offset, slots in ours:
        gm = mangled
        if (gm, offset) not in game:
            b = rtti_bare(mangled)
            cands = game_bare.get(b, set()) if b else set()
            if len(cands) != 1 or len(our_bare[b]) != 1:
                continue
            gm = next(iter(cands))
        entries = game.get((gm, offset))
        if entries is None or len(entries) != len(slots):
            continue
        for rva, nodes in zip(entries, slots):
            ties[int(rva, 16)].update(nodes)
    return ties


def build_graph(build, config):
    """The graph over the build's ts2core, hook table and owned-static objects."""
    g = Graph()
    core = tsdb.object_dir(build, "ts2core", config)
    hooks = tsdb.object_dir(build, "ts2hooktables", config)
    if not core.is_dir() or not hooks.is_dir():
        raise SystemExit(f"imagefree: no ts2core / ts2hooktables objects under {build} ({config}); build ts2host first"
                         " (Invoke-Build ts2host)")
    for p in sorted(core.rglob("*.obj")):
        if p.name.lower() not in IMAGE_OBJECTS:
            g.add_object(p, "core")
    for p in sorted(hooks.rglob("*.obj")):
        g.add_object(p, "hooks")
    root = core.parent.parent
    owned = [p for p in sorted(root.glob(f"*.dir/{config}/standalone/*.obj")) if p.name.lower() != noimage.TRAP_OBJECT]
    if not owned:
        print(f"imagefree: warning: no standalone program objects under {root}: owned statics count as image"
              " references (Invoke-Build render_object)", file=sys.stderr)
    for p in owned:
        g.add_object(p, "owned")
    g.link()
    return g


def closures(g):
    """Per node, the bitset (int) of image references it reaches, and the key index. Tarjan's SCCs, iteratively: an SCC
    completes after everything it reaches, so its set is its own and its successors' (already final)."""
    keys = sorted({k for r in g.refs for k in r})
    bit = {k: 1 << i for i, k in enumerate(keys)}
    n = len(g.obj)
    own = [0] * n
    for v in range(n):
        for k in g.refs[v]:
            own[v] |= bit[k]
    index, low, onstack = [-1] * n, [0] * n, [False] * n
    comp_bits = [0] * n
    stack, counter = [], 0
    edges = [list(e) for e in g.edges]
    for start in range(n):
        if index[start] >= 0:
            continue
        work = [(start, 0)]
        while work:
            v, i = work[-1]
            if i == 0:
                index[v] = low[v] = counter
                counter += 1
                stack.append(v)
                onstack[v] = True
            if i < len(edges[v]):
                work[-1] = (v, i + 1)
                w = edges[v][i]
                if index[w] < 0:
                    work.append((w, 0))
                elif onstack[w]:
                    low[v] = min(low[v], index[w])
                continue
            work.pop()
            if work:
                u = work[-1][0]
                low[u] = min(low[u], low[v])
            if low[v] == index[v]:
                members = []
                while True:
                    w = stack.pop()
                    onstack[w] = False
                    members.append(w)
                    if w == v:
                        break
                bits = 0
                for w in members:
                    bits |= own[w]
                    for x in edges[w]:
                        if not onstack[x]:
                            bits |= comp_bits[x]
                for w in members:
                    comp_bits[w] = bits
    return comp_bits, keys


def short(mangled):
    """A rough readable form of a decorated name (Class::Member), enough to recognise it."""
    m = re.match(r"\?+([^@?$]*)@(?:((?:[^?$]|\?A0x)*?)@)?@", mangled)  # ?A0x...: an unnamed namespace, dropped
    if not m:
        return mangled.lstrip("_?")
    parts = [p for p in (m.group(2) or "").split("@") if p and not p.startswith("?A0x")]
    return "::".join(parts[::-1] + [m.group(1)]) if m.group(1) else "::".join(parts[::-1])


def direct_users(g):
    """{key: (number of our nodes referring to it directly, the most common user's readable name)}."""
    users = defaultdict(Counter)
    for v, refs in enumerate(g.refs):
        for k in refs:
            users[k][g.name[v]] += 1
    return {k: (sum(c.values()), short(max(c, key=lambda n: (c[n], -len(n))))) for k, c in users.items()}


def iter_bits(x):
    while x:
        low = x & -x
        yield low.bit_length() - 1
        x ^= low


class Db:
    """What the report names things with: sizes, names, status, subsystems, vtables, import slots."""

    def __init__(self):
        self.funcs = {int(f["rva"], 16): f for f in tsdb.functions()}
        self.names = {int(n["rva"], 16): n["name"] for n in tsdb.jsonl("names.jsonl")}
        self.status = {int(r, 16): e for r, e in tsdb.load_status().items()}
        self.library = {int(r, 16) for r in tsdb.library_rvas()}
        self.subsystem = {int(e["rva"], 16): e["subsystem"] for e in tsdb.jsonl("subsystems.jsonl")}
        self.classes = tsdb.jsonl("classes.jsonl")
        self.vtables = {int(v["rva"], 16): c["name"] for c in self.classes for v in c["vftables"]}
        self.imports = {}
        try:
            import pefile
            pe = pefile.PE(str(tsdb.unpacked_exe()), fast_load=True)
            pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
            base = pe.OPTIONAL_HEADER.ImageBase
            for d in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
                for i in d.imports:
                    nm = i.name.decode("latin-1") if i.name else f"#{i.ordinal}"
                    self.imports[i.address - base] = f"{d.dll.decode('latin-1')}!{nm}"
        except Exception:  # noqa: BLE001 (no unpacked exe: imports show as data)
            pass

    def size(self, rva):
        f = self.funcs.get(rva)
        return f["size"] if f else 0

    def fname(self, rva):
        return self.names.get(rva) or (self.funcs[rva].get("name") if rva in self.funcs else "") or ""

    def describe(self, key):
        """(kind, name, size, status text) of an image reference."""
        kind, rva = key
        st = self.status.get(rva)
        sttext = f"{st['status']} ({st['_file'].removesuffix('.jsonl')})" if st else "-"
        if kind == "original":
            return "function", self.fname(rva), self.size(rva), sttext
        if rva == 0:
            return "base", "game base (GameBase(), GameAddressOpaque)", 0, "-"
        if rva in self.vtables:
            return "vtable", f"vtable of {self.vtables[rva]}", 0, "-"
        if rva in self.imports:
            return "import", self.imports[rva], 0, "-"
        if rva in self.funcs:
            return "func-addr", self.fname(rva), self.size(rva), sttext
        return "data", self.names.get(rva, ""), 0, "-"


def analyse(g, db, comp_bits):
    """Map verified RVAs to nodes; per RVA its blocker bitset (None: not located) and how it was located."""
    hooks, dynamic = hook_targets(g)
    ties = vtable_ties(g, db.classes)
    verified = sorted(r for r, e in db.status.items() if e["status"] == "verified" and r not in db.library)
    roots, how, bits = {}, {}, {}
    for r in verified:
        nodes, h = hooks.get(r), "hook"
        if not nodes:
            nodes, h = ties.get(r), "vtable tie"
        if not nodes:
            bits[r], how[r] = None, ("dynamic hook entry" if r in dynamic else "not located")
            continue
        roots[r], how[r] = nodes, h
        b = 0
        for n in nodes:
            b |= comp_bits[n]
        bits[r] = b
    return verified, roots, how, bits


def attribute(bits, size):
    """Per blocker index: 'any' (bytes of the functions whose closure reaches it) and 'only' (bytes of the functions
    it alone blocks, which become image-free once it goes). bits: {rva: bitset or None (not located)}."""
    by_set = defaultdict(int)  # functions with the same blocker set share one walk over its bits
    for r, b in bits.items():
        if b:
            by_set[b] += size(r)
    anyb, only = defaultdict(int), defaultdict(int)
    for b, nbytes in by_set.items():
        idx = list(iter_bits(b))
        for i in idx:
            anyb[i] += nbytes
        if len(idx) == 1:
            only[idx[0]] += nbytes
    return anyb, only


def report(db, keys, verified, how, bits, top, users, lower=None):
    """The headline, the per-subsystem table and the top blockers; returns the saved summary."""
    size = db.size
    total = sum(size(r) for r in verified)
    located = [r for r in verified if bits[r] is not None]
    free = [r for r in located if bits[r] == 0]
    lb, fb = sum(size(r) for r in located), sum(size(r) for r in free)
    print(f"image-free indicator (tools/imagefree.py): verified code whose static closure in our objects has no image"
          f" reference")
    print(f"  verified (excluding library): {total:,} bytes in {len(verified):,} functions; located in our objects"
          f" {lb:,} bytes ({len(located):,}: {sum(1 for r in located if how[r] == 'hook'):,} by hook,"
          f" {sum(1 for r in located if how[r] == 'vtable tie'):,} by vtable tie)")
    print(f"  IMAGE-FREE: {fb:,} bytes in {len(free):,} functions = {100 * fb / lb if lb else 0:.1f}% of located"
          f" verified bytes ({100 * fb / total if total else 0:.1f}% of all verified)")
    low = sum(size(r) for r in located if not lower.get(r)) if lower is not None else None
    if lower is not None:
        print(f"  lower bound, each member function also reaching its own class's vtable (slot calls through `this`):"
              f" {low:,} bytes = {100 * low / lb if lb else 0:.1f}% of located")
    unloc = [r for r in verified if bits[r] is None]
    print(f"  not located (no hook and no vtable tie: non-virtual code in effect through callers, host-started code):"
          f" {sum(size(r) for r in unloc):,} bytes in {len(unloc):,} functions")

    per = defaultdict(lambda: [0, 0, 0, 0, 0])
    for r in verified:
        p = per[db.subsystem.get(r, "unassigned")]
        p[0] += size(r)
        if bits[r] is not None:
            p[1] += size(r)
            if bits[r] == 0:
                p[2] += size(r)
            if lower is not None and not lower.get(r):
                p[4] += size(r)
        else:
            p[3] += size(r)
    print(f"\n  {'subsystem':24} {'verified B':>11} {'located B':>11} {'image-free B':>13} {'share':>6} "
          f"{'lower':>6} {'not located B':>14}")
    for name, (v, loc, f, u, lo) in sorted(per.items(), key=lambda kv: -kv[1][0]):
        if not v:
            continue
        lot = f"{100 * lo / loc if loc else 0:5.1f}%" if lower is not None else "     -"
        print(f"  {name:24} {v:11,} {loc:11,} {f:13,} {100 * f / loc if loc else 0:5.1f}% {lot} {u:14,}")

    anyb, only = attribute(bits, size)
    rows = sorted(anyb, key=lambda i: (-anyb[i], -only[i], keys[i]))
    print(f"\n  top blockers ({min(top, len(rows))} of {len(rows)} image references our verified code reaches; 'any':"
          f" verified bytes reaching it, 'only': bytes it alone still blocks)")
    print(f"  {'any B':>9} {'only B':>8}  {'ref':8} {'rva':>9} {'kind':9} {'size':>6}  {'status (group)':32} "
          f"{'subsystem':16} name")
    for i in rows[:top]:
        kind, name, sz, st = db.describe(keys[i])
        sub = db.subsystem.get(keys[i][1], "-") if kind in ("function", "func-addr") else "-"
        nu, eg = users.get(keys[i], (0, ""))
        name = name or f"(used by {nu} of ours, e.g. {eg[:60]})"
        print(f"  {anyb[i]:9,} {only[i]:8,}  {keys[i][0]:8} {keys[i][1]:#09x} {kind:9} {sz:6}  {st[:32]:32} "
              f"{sub[:16]:16} {name[:80]}")
    onlyrows = sorted(only, key=lambda i: -only[i])[:10]
    print(f"\n  most bytes freed by one reference ('only', top {len(onlyrows)}):")
    for i in onlyrows:
        kind, name, sz, st = db.describe(keys[i])
        nu, eg = users.get(keys[i], (0, ""))
        name = name or f"(used by {nu} of ours, e.g. {eg[:60]})"
        print(f"  {only[i]:9,}  {keys[i][0]:8} {keys[i][1]:#09x} {kind:9} {sz:6}  {st[:32]:32} {name[:80]}")
    print()
    print(CAVEATS)
    return {"verified": total, "located": lb, "free": fb, "free_rvas": sorted(hex(r) for r in free),
            "blocked_rvas": sorted(hex(r) for r in located if bits[r]),
            "blockers": {f"{keys[i][0]}:{keys[i][1]:#x}": [anyb[i], only[i]] for i in rows}}


def compare(summary, last):
    """The "against the previous run" line."""
    if not last.exists():
        print(f"\nagainst the previous run: none at {last} (this run is the baseline)")
        return
    before = json.loads(last.read_text())
    bf, nf = set(before["free_rvas"]), set(summary["free_rvas"])
    gone = set(before["blockers"]) - set(summary["blockers"])
    new = set(summary["blockers"]) - set(before["blockers"])
    print(f"\nagainst the previous run ({last}): image-free {before['free']:,} -> {summary['free']:,} bytes"
          f" (verified {before['verified']:,} -> {summary['verified']:,}); {len(nf - bf)} functions newly image-free,"
          f" {len(bf - nf)} no longer; blockers {len(before['blockers'])} -> {len(summary['blockers'])}"
          f" ({len(gone)} gone, {len(new)} new)")
    for label, ks in (("gone", gone), ("new", new)):
        src = before if label == "gone" else summary
        for k in sorted(ks, key=lambda k: -src["blockers"][k][0])[:5]:
            print(f"  {label:4} {k} ({src['blockers'][k][0]:,} bytes reached it)")


def path_to(g, starts, key):
    """A shortest reference chain from the start nodes to a node holding `key`, as node names."""
    prev = {s: None for s in starts}
    q = deque(starts)
    while q:
        v = q.popleft()
        if key in g.refs[v]:
            chain = []
            while v is not None:
                chain.append(f"{g.name[v]} [{Path(g.objects[g.obj[v]][0]).name}]")
                v = prev[v]
            return chain[::-1]
        for w in g.edges[v]:
            if w not in prev:
                prev[w] = v
                q.append(w)
    return []


def show_rvas(g, db, keys, rvas, roots, how, bits):
    for r in rvas:
        print(f"\n{r:#x} {db.fname(r)} ({db.size(r)} B, {db.status.get(r, {}).get('status', 'no status')}): "
              f"{how.get(r, 'not verified')}")
        if r not in roots:
            continue
        for n in sorted(roots[r])[:4]:
            print(f"  ours: {g.name[n]} [{Path(g.objects[g.obj[n]][0]).name}]")
        b = list(iter_bits(bits[r]))
        print(f"  {'IMAGE-FREE' if not b else f'blocked by {len(b)} image reference(s)'}")
        for i in b[:12]:
            kind, name, sz, st = db.describe(keys[i])
            print(f"   - {keys[i][0]} {keys[i][1]:#x} {kind} {name[:60]} ({st})")
            for step in path_to(g, list(roots[r]), keys[i]):
                print(f"       {step[:150]}")


def check_program(g, db, build, config, program, roots, bits, keys, comp_bits):
    """--program: the model against a linked program's map and noimage's reading of it."""
    mp = Path(build) / config / f"{program}.map"
    if not mp.exists():
        print(f"\nprogram {program}: no map at {mp} (Invoke-Build {program})")
        return 1
    out = Path(build) / f"imagefree-{program}.noimage.jsonl"
    dirs = [str(tsdb.object_dir(build, t, config)) for t in ("ts2core", program)]
    subprocess.run([sys.executable, "-I", str(ROOT / "tools/noimage.py"), str(mp), *dirs, "--no-save", "--json",
                    str(out)], check=True, capture_output=True)
    nrefs = [json.loads(l) for l in out.read_text().splitlines() if l.strip()]
    out.unlink()
    theirs = {("original" if r["ref"] == "original" else "address", int(r["rva"], 16)) for r in nrefs}
    owners = {u.split(":", 1)[1] for r in nrefs for u in r["users"]}
    linked = {name for name, obj in noimage.MAP_LINE.findall(mp.read_text(errors="replace"))}
    ext = {n for n in g.defs}
    # a root with several of our functions (a folded original body): the ones the program links
    linked_roots = {r: [n for n in ns if g.name[n] in linked] for r, ns in roots.items()}
    in_prog = [r for r in roots if linked_roots[r]]
    extra_refs, over, missed = defaultdict(list), defaultdict(list), []
    pbits = {}
    for r in in_prog:
        b = 0
        for n in linked_roots[r]:
            b |= comp_bits[n]
        pbits[r] = b
        for i in iter_bits(b):
            k = keys[i]
            k2 = ("address", k[1]) if k[0] == "owned" else k
            if k2 not in theirs:
                extra_refs[k].append(r)
        if any(g.name[n] in owners for n in roots[r]) and not bits[r]:
            missed.append(r)
    came = {}  # one search from all of them; each node credited to the first root that reaches it
    q = deque()
    for r in in_prog:
        for n in linked_roots[r]:
            if n not in came:
                came[n] = r
                q.append(n)
    while q:
        v = q.popleft()
        nm = g.name[v]
        if nm in ext and nm not in linked and g.objects[g.obj[v]][1] == "core":
            over[nm].append(came[v])
        for w in g.edges[v]:
            if w not in came:
                came[w] = came[v]
                q.append(w)
    free = sum(1 for r in in_prog if not pbits[r])
    print(f"\nprogram {program}: noimage finds {len(theirs)} image reference(s); it links {len(in_prog)} verified"
          f" functions, {free} image-free here ({sum(db.size(r) for r in in_prog if not pbits[r]):,} bytes)")
    print(f"  references our closures reach that the program lacks: {len(extra_refs)}"
          + "".join(f"\n    {k[0]} {k[1]:#x} <- {', '.join(hex(r) for r in v[:4])}" for k, v in
                    sorted(extra_refs.items())[:10]))
    folded = {n for n in over if n.startswith("??$") or "@std@@" in n}  # /OPT:ICF folds most of these away
    rest = sorted(n for n in over if n not in folded)
    print(f"  external symbols our closures reach that the program's map doesn't name: {len(over)}"
          f" ({len(folded)} templates and STL, which /OPT:ICF folds under another name; the others:)"
          + "".join(f"\n    {n[:100]} <- {over[n][0]:#x}" for n in rest[:12]))
    print(f"  noimage owners we call image-free: {len(missed)}"
          + "".join(f"\n    {r:#x} {db.fname(r)}" for r in missed[:10]))
    return 1 if extra_refs or missed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build", help="build tree (default: the main checkout's build/)")
    ap.add_argument("--config", default="Release")
    ap.add_argument("--top", type=int, default=30, help="blockers to list (default 30)")
    ap.add_argument("--json", help="write the per-function verdicts and the blockers here")
    ap.add_argument("--no-save", action="store_true", help="don't save this run as the previous one")
    ap.add_argument("--rva", nargs="+", default=[], help="show mapping, verdict and reference paths for these RVAs")
    ap.add_argument("--program", action="append", default=[],
                    help="check the model against a linked program's map (repeatable)")
    ap.add_argument("--no-lower", action="store_true",
                    help="skip the lower bound (member functions tied to their class's vtables; about 10 s more)")
    args = ap.parse_args()
    t0 = time.time()
    build = Path(args.build) if args.build else tsdb.main_root() / "build"
    g = build_graph(build, args.config)
    comp_bits, keys = closures(g)
    db = Db()
    verified, roots, how, bits = analyse(g, db, comp_bits)
    users = direct_users(g)
    lower = None
    if not args.no_lower:
        added = g.add_class_edges()
        low_bits, _ = closures(g)
        lower = {r: any(low_bits[n] for n in ns) for r, ns in roots.items()}
        g.remove_edges(added)  # the rest (program checks, paths) is of the static model
        del low_bits
    summary = report(db, keys, verified, how, bits, args.top, users, lower)
    rc = 0
    for p in args.program:
        rc |= check_program(g, db, build, args.config, p, roots, bits, keys, comp_bits)
    if args.rva:
        show_rvas(g, db, keys, [int(r, 16) for r in args.rva], roots, how, bits)
    last = ROOT / "build" / "imagefree.last.json"
    compare(summary, last)
    if args.json:
        with open(args.json, "w") as f:
            for r in verified:
                f.write(json.dumps({"rva": hex(r), "size": db.size(r), "located": how[r],
                                    "image_free": bits[r] == 0 if bits[r] is not None else None,
                                    "blockers": [f"{keys[i][0]}:{keys[i][1]:#x}" for i in iter_bits(bits[r] or 0)]})
                        + "\n")
    if not args.no_save:
        last.parent.mkdir(exist_ok=True)
        last.write_text(json.dumps(summary))
    print(f"\n({len(g.objects)} objects, {len(g.obj):,} nodes, {len(keys):,} distinct image references;"
          f" build {build}; {time.time() - t0:.1f} s)")
    return rc


if __name__ == "__main__":
    sys.exit(main())
