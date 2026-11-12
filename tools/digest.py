"""One class on a screen: its members with the compiler's offsets, its functions with RVA / slot / status, and the
free helpers and constants of its header. Read this before paging through a long class header.

Usage: tools/py -I tools/digest.py cLightingManager                 # all three sections
       tools/py -I tools/digest.py src/sg/cLightingManager.h        # the header's main class (named like the file)
       tools/py -I tools/digest.py cLightingManager 0x22c           # the member at +0x22c (one inside it, expanded)
       tools/py -I tools/digest.py cLightingManager 0x50c-0x520     # members overlapping an offset range
       tools/py -I tools/digest.py cLightingManager --methods mapped   # rows whose text contains `mapped`
       tools/py -I tools/digest.py cLightingManager slot13          # the function in vtable slot 13 (s13 too)
  --members / --methods / --helpers   print one section
  --expand   expand every class-typed member one level (by default only the ones a filter falls in)

Members: offset, size, name, type, header line and comment, from the MSVC 14.40 layout report of the header compiled
on its own (/d1reportAllClassLayout; cached in build/digest/ until a header changes). Base classes, vfptr / vbptr
and virtual bases are rows of their own; a member's type is the header's, else the compiler's (demangled roughly).
Sizes come from the type (scalars, pointers, arrays, std containers, classes in the same report), else from the
distance to the next offset (printed `~N`). When the header does not compile alone, or the class is a template
nobody instantiates there, the reason is printed and the offsets come from the header's comments (`?`, unchecked).
Methods: every RVA tools/whereis.py ties to the class (vtables, db/names, db/status, hook tables) with its size
(db/functions), slot (`13` in the primary vtable, `+0x8:3` in the one at +8), status, our C++ name and where it is
declared (src/ and tests/ headers, tools/symfind.py's index), and its comment's first sentence.
"""
import argparse
import hashlib
import json
import pickle
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import symfind  # noqa: E402

ROOT = symfind.ROOT
MAIN = ROOT
for parent in ROOT.parents:  # a worktree's main checkout (its build/generated, its compiler environment cache)
    if (parent / ".git").is_dir():
        MAIN = parent
        break
OUT = ROOT / "build/digest"

# ---------------------------------------------------------------- the compiler's layout report


def msvc_env():
    """The MSVC 14.40 x86 environment (vcvarsall, as tools/matchcheck.py), cached in build/digest/env.json."""
    cache = OUT / "env.json"
    if cache.exists():
        return json.loads(cache.read_text())
    vswhere = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
    vs = subprocess.run([str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
                        capture_output=True, text=True).stdout.strip()
    bat = Path(vs) / "VC/Auxiliary/Build/vcvarsall.bat"
    out = subprocess.run(f'"{bat}" x64_x86 -vcvars_ver=14.40 >nul && set', shell=True, capture_output=True,
                         text=True).stdout
    env = dict(l.split("=", 1) for l in out.splitlines() if "=" in l)
    if "VCToolsInstallDir" not in env:
        raise RuntimeError("vcvarsall gave no VCToolsInstallDir (is MSVC 14.40 installed?)")
    OUT.mkdir(parents=True, exist_ok=True)
    cache.write_text(json.dumps(env))
    return env


def generated_dir():
    for base in (ROOT, MAIN):
        if (base / "build/generated").is_dir():
            return base / "build/generated"
    return None


def compile_layouts(header):
    """(report text, error or None) of /d1reportAllClassLayout for a TU including `header` (a Path), cached under
    build/digest/ while no header under src/ or tests/ changes."""
    rel = header.resolve().relative_to(ROOT).as_posix()
    stamp = max((f.stat().st_mtime_ns for f in symfind.source_files() if f.suffix == ".h"), default=0)
    key = hashlib.sha1(f"{rel}|{stamp}|1".encode()).hexdigest()[:16]
    cached = OUT / f"layout-{key}.txt"
    if cached.exists():
        text = cached.read_text(encoding="utf-8", errors="replace")
        return text, None
    try:
        env = msvc_env()
    except (OSError, RuntimeError) as e:
        return "", f"no compiler environment ({e})"
    OUT.mkdir(parents=True, exist_ok=True)
    tu = OUT / "tu.cpp"
    tu.write_text(f'#include "{header.resolve().as_posix()}"\n')
    args = [env["VCToolsInstallDir"] + r"bin\Hostx64\x86\cl.exe", "/nologo", "/TP", "/std:c++20", "/permissive-",
            "/Zc:inline", "/utf-8", "/D_MBCS", "/EHsc", "/c", f"/Fo{OUT / 'tu.obj'}", "/d1reportAllClassLayout",
            f"/I{ROOT / 'src'}", f"/I{ROOT / 'tests'}", f"/I{ROOT / 'harness'}"]
    gen = generated_dir()
    if gen:
        args.append(f"/I{gen}")
    try:
        r = subprocess.run(args + [str(tu)], env=env, capture_output=True, text=True, cwd=OUT, timeout=120)
    except (OSError, subprocess.TimeoutExpired) as e:
        return "", f"cl did not run ({e})"
    text = r.stdout + r.stderr
    if r.returncode != 0:
        err = next((l for l in text.splitlines() if re.search(r"\berror\b", l)), text.strip()[-200:])
        return text, f"the header does not compile on its own: {err.strip()[:200]}"
    for old in OUT.glob("layout-*.txt"):
        old.unlink()
    cached.write_text(text, encoding="utf-8")
    return text, None


_BLOCK = re.compile(r"^class (.+?)\tsize\((\d+)\):$")


def parse_layouts(text):
    """{qualified class name: {"size": n, "rows": [(depth, offset or None, content)]}} of a layout report."""
    out, cur = {}, None
    for line in text.splitlines():
        m = _BLOCK.match(line)
        if m:
            cur = out.setdefault(m.group(1), {"size": int(m.group(2)), "rows": []})
            continue
        if cur is None:
            continue
        if not line.strip():
            cur = None
            continue
        off, _, rest = line.partition("\t")
        if not _ and not rest:
            continue
        depth = 0
        while rest.startswith("| "):
            depth += 1
            rest = rest[2:]
        if rest == "|":
            continue
        off = off.strip()
        cur["rows"].append((depth, int(off) if off.isdigit() else None, rest.strip()))
    return out


def layout_entries(block):
    """The rows of one layout block as dicts: kind (`base`, `vbase`, `vfptr`, `vbptr`, `vtordisp`, `member`), depth
    (1 = the class's own), offset, name, ctype (the compiler's type text for a class-typed member, else ''), and
    `within` (the innermost base or virtual base the row belongs to, '' for the class's own)."""
    # The report draws a base as `+--- (base class X)` at the depth of the rows around it, its rows one deeper and
    # a closing `+---` at the header's depth; a virtual base the same at depth 0, after the class's own `+---`.
    rows, stack = [], []  # stack: (header depth, base name), the open bases
    vbase = None
    for depth, off, text in block["rows"]:
        if text == "+---":
            while stack and stack[-1][0] >= depth:
                stack.pop()
            if depth == 0:
                vbase = None
            continue
        while stack and stack[-1][0] >= depth:
            stack.pop()
        within = stack[-1][1] if stack else (vbase or "")
        d = depth + (1 if vbase else 0)
        m = re.match(r"\+--- \((base class|virtual base) (.+)\)$", text)
        if m:
            if m.group(1) == "virtual base":
                vbase = m.group(2)
                rows.append({"kind": "vbase", "depth": 1, "offset": off, "name": vbase, "ctype": "", "within": ""})
                continue
            rows.append({"kind": "base", "depth": d, "offset": off, "name": m.group(2), "ctype": "", "within": within})
            stack.append((depth, m.group(2)))
            continue
        if text in ("{vfptr}", "{vbptr}"):
            rows.append({"kind": text[1:-1], "depth": d, "offset": off, "name": text, "ctype": "", "within": within})
            continue
        m = re.match(r"\(vtordisp for vbase (.+)\)$", text)
        if m:
            rows.append({"kind": "vtordisp", "depth": 1, "offset": off, "name": f"vtordisp for {m.group(1)}",
                         "ctype": "", "within": ""})
            continue
        if off is None:
            continue
        name = text.rsplit(" ", 1)
        rows.append({"kind": "member", "depth": d, "offset": off, "name": name[-1],
                     "ctype": name[0] if len(name) > 1 else "", "within": within})
    for i, r in enumerate(rows):  # a virtual base's header line has no offset: its first row's
        if r["offset"] is None and r["kind"] in ("vbase", "base"):
            r["offset"] = next((n["offset"] for n in rows[i + 1:] if n["offset"] is not None), None)
    return rows


def demangle(ctype):
    """A rough readable form of the report's type text: `?$vector@UcPortalLight@nTSSG@@V?$allocator@...` ->
    `std::vector<cPortalLight>`, `cLightSettingsBlock` unchanged."""
    if not ctype.startswith("?$"):
        return ctype
    outer, _, rest = ctype[2:].partition("@")
    m = re.match(r"[UVW]([A-Za-z_]\w*)@|([A-Z])", rest)
    arg = m.group(1) if m and m.group(1) else {"D": "char", "E": "uint8_t", "H": "int", "I": "uint32_t", "M": "float",
                                               "N": "double", "_N": "bool"}.get(m.group(2) if m else "", "...")
    return f"{outer}<{arg}>"


# ---------------------------------------------------------------- sizes

SCALARS = {"bool": 1, "char": 1, "int8_t": 1, "uint8_t": 1, "unsigned char": 1, "signed char": 1, "BYTE": 1,
           "int16_t": 2, "uint16_t": 2, "short": 2, "unsigned short": 2, "wchar_t": 2, "char16_t": 2, "WORD": 2,
           "int": 4, "unsigned": 4, "unsigned int": 4, "int32_t": 4, "uint32_t": 4, "long": 4, "unsigned long": 4,
           "float": 4, "DWORD": 4, "size_t": 4, "ptrdiff_t": 4, "intptr_t": 4, "uintptr_t": 4, "BOOL": 4,
           "HRESULT": 4, "double": 8, "int64_t": 8, "uint64_t": 8, "long long": 8, "unsigned long long": 8}
STD = {"vector": 12, "basic_string": 24, "string": 24, "wstring": 24, "map": 8, "set": 8, "multimap": 8,
       "multiset": 8, "list": 8, "unordered_map": 32, "unordered_set": 32, "deque": 20, "shared_ptr": 8,
       "unique_ptr": 4, "weak_ptr": 8, "function": 40, "optional": None}


def type_size(typ, layouts):
    """Bytes of a C++ member type, or None: pointers 4, scalars, arrays, MSVC x86 std containers, enums 4, and the
    classes of the same layout report."""
    t = re.sub(r"\b(const|volatile|mutable|static|struct|class|enum|typename)\b", " ", typ)
    t = " ".join(t.split())
    m = re.match(r"(.*?)((?:\[[^\]]*\])+)$", t)
    if m:
        count = 1
        for n in re.findall(r"\[([^\]]*)\]", m.group(2)):
            try:
                count *= int(n, 0)
            except ValueError:
                return None
        inner = type_size(m.group(1).strip(), layouts)
        return None if inner is None else inner * count
    if t.endswith("*") or t.endswith("&") or "(*" in t:
        return 4
    if t in SCALARS:
        return SCALARS[t]
    outer = re.sub(r"^(std::|::)", "", t).split("<")[0].strip()
    if outer in STD:
        return STD[outer]
    base = t.split("<")[0].split("::")[-1].strip()
    for name, block in layouts.items():
        if name == t or name.split("<")[0].split("::")[-1] == base and "<" not in name:
            return block["size"]
    return None


# ---------------------------------------------------------------- the class

def layout_key(layouts, qual):
    """The report's name for the header's class `qual`: itself, or one inside an anonymous namespace
    (`` `anonymous-namespace'::FakeObject ``), or the only one with that last name; else None."""
    if qual in layouts:
        return qual
    anon = [q for q in layouts if q.endswith("::" + qual) and "anonymous" in q]
    if len(anon) == 1:
        return anon[0]
    last = qual.split("::")[-1]
    cands = [q for q in layouts if q.split("::")[-1] == last]
    return cands[0] if len(cands) == 1 else None


def find_class(arg, index):
    """(header Path, qualified scope 'ns::cFoo', class name) for a class name or a header path."""
    p = Path(arg)
    if arg.endswith(".h"):
        path = p if p.is_absolute() else ROOT / arg
        rel = path.resolve().relative_to(ROOT).as_posix()
        types = [t for t in index.get(rel, ()) if t[0] == "type"]
        pick = [t for t in types if t[1] == path.stem] or sorted(types, key=lambda t: -t[3])[:1]
        if not pick:
            raise SystemExit(f"{rel}: no class or struct found")
        t = pick[0]
        return path, (t[2] + "::" + t[1]).strip(":"), t[1]
    parts = arg.split("::")
    name, want = parts[-1], "::".join(parts[:-1])
    hits = []
    for rel, decls in index.items():
        for t in decls:
            if t[0] == "type" and t[1] == name and (not want or t[2].endswith(want)):
                hits.append((rel, t))
    if not hits:
        raise SystemExit(f"{arg}: no class or struct of that name in src/ or tests/ headers (tools/symfind.py?)")
    # Prefer src/, then a header named after the class, then the longest body.
    hits.sort(key=lambda h: (not h[0].startswith("src/"), not h[0].endswith(f"/{name}.h"),
                             h[0].endswith(".cpp")))
    rel, t = hits[0]
    return ROOT / rel, (t[2] + "::" + t[1]).strip(":"), name


def header_decls(path):
    return symfind.parse(path.read_text(encoding="utf-8", errors="replace"), path.name)


def comment_offset(d):
    m = re.match(r"\s*\(?\+?(0x[0-9a-fA-F]+)\b|\s*\(?\+(\d+)\b", symfind.comment_of(d))
    if not m:
        return None
    return int(m.group(1), 16) if m.group(1) else int(m.group(2))


def member_rows(layouts, lkey, qual, decls, field_index, expand_all, want_off):
    """Printable rows (offset, text) for the members of `qual`, with `within` / nested expansion."""
    block = layouts[lkey]
    fields = {d["name"]: d for d in decls if d["kind"] == "field" and d["scope"] == qual}
    local = {(d["scope"].split("::")[-1], d["name"]): d for d in decls if d["kind"] == "field"}
    rows = []
    entries = layout_entries(block)
    own_end = block["size"]
    offs = sorted({e["offset"] for e in entries if e["offset"] is not None} | {own_end})

    def span(off):
        nxt = [o for o in offs if o > off]
        return nxt[0] - off if nxt else None

    for i, e in enumerate(entries):
        off = e["offset"]
        indent = "  " * (e["depth"] - 1)
        if e["kind"] in ("base", "vbase"):
            # The bytes it takes here (its virtual bases sit elsewhere): up to the next row not inside it.
            nxt = next((n["offset"] for n in entries[i + 1:] if n["depth"] <= e["depth"] and n["offset"] is not None),
                       own_end)
            size = nxt - off if off is not None else None
            label = "virtual base" if e["kind"] == "vbase" else "base"
            rows.append((off, f"{fmt_off(off)} {fmt_size(size, None)}  {indent}[{label} {short(e['name'])}]",
                         size))
            continue
        if e["kind"] != "member":
            rows.append((off, f"{fmt_off(off)} {fmt_size(4, None)}  {indent}{e['name']}", 4))
            continue
        d = fields.get(e["name"]) if not e["within"] else None
        if d is None and e["within"]:
            d = field_index.get((short_outer(e["within"]), e["name"]))
        typ = (d["type"] if d else "") or demangle(e["ctype"]) or "?"
        size = type_size(typ, layouts)
        if size is None and e["ctype"]:
            size = type_size(demangle(e["ctype"]), layouts)
        where = f"L{d['line']}" if d and "path" not in d else (f"{d['path']}:{d['line']}" if d else "")
        doc = member_doc(d, off)
        text = f"{fmt_off(off)} {fmt_size(size, span(off))}  {indent}{e['name']}  {typ}"
        if where:
            text += f"  {where}"
        if doc:
            text += f"  // {doc}"
        rows.append((off, text, size or span(off)))
        nested = nested_block(layouts, typ, e["ctype"])
        sz = size or span(off) or 0
        if nested and (expand_all or (want_off is not None and off <= want_off[1] and want_off[0] < off + sz)):
            for n in layout_entries(nested):
                if n["depth"] != 1 or n["offset"] is None:
                    continue
                noff = off + n["offset"]
                ntyp = demangle(n["ctype"]) or ""
                nd = local.get((short_outer(typ), n["name"])) or field_index.get((short_outer(typ), n["name"]))
                if nd:
                    ntyp = nd["type"] or ntyp
                nsize = type_size(ntyp, layouts) if ntyp else None
                label = n["name"] if n["kind"] == "member" else f"[{n['kind']} {short(n['name'])}]"
                line = f"{fmt_off(noff)} {fmt_size(nsize, None)}  {indent}  .{label}  {ntyp}".rstrip()
                ndoc = member_doc(nd, None)
                if ndoc:
                    line += f"  // {ndoc[:80]}"
                rows.append((noff, line, nsize or 0))
    return rows


_LEAD_OFF = re.compile(r"^\+?(0x[0-9a-fA-F]+)\b(?:\s*\.\.\s*\+?0x[0-9a-fA-F]+)?[:,.]?\s*")


def member_doc(d, off):
    """A member's comment without its leading offset; `(comment: 0x..)` first when that offset is not the
    compiler's (a stale comment, or the layout has drifted from the original's)."""
    if not d:
        return ""
    raw = symfind.comment_of(d) if "above" in d else d.get("doc", "")
    m = _LEAD_OFF.match(raw.strip())
    note = ""
    if m:
        if off is not None and "above" in d and int(m.group(1), 16) != off:  # a base's offsets are its own
            note = f"(comment: {m.group(1)}!) "
        raw = raw.strip()[m.end():]
    return note + symfind.first_sentence(raw, 90) if raw or note else ""


def nested_block(layouts, typ, ctype):
    name = re.sub(r"\b(const|struct|class)\b", "", typ).strip()
    if not name or name.endswith("*") or "[" in name or "<" in name:
        return None
    base = name.split("::")[-1]
    for q, block in layouts.items():
        if q == name or q.split("::")[-1] == base:
            return block
    return None


def short(qual):
    return re.sub(r"<.*>", "<...>", qual).split("::")[-1] if "<" not in qual else qual.split("<")[0].split("::")[-1] + "<...>"


def short_outer(qual):
    return qual.split("<")[0].split("::")[-1]


def fmt_off(off):
    return f"+0x{off:<4x}" if off is not None else "      "


def fmt_size(size, span):
    if size is not None:
        return f"{size:>5}"
    if span is not None:
        return f"{'~' + str(span):>5}"
    return "    ?"


def comment_member_rows(decls, qual):
    """Members from the header alone (no layout): the offsets their comments give, unchecked."""
    rows = []
    for d in decls:
        if d["kind"] == "field" and d["scope"] == qual:
            off = comment_offset(d)
            doc = symfind.first_sentence(symfind.comment_of(d), 90)
            label = f"?+0x{off:<4x}" if off is not None else "?      "
            rows.append((off, f"{label}{type_size(d['type'], {}) or '':>5}  {d['name']}  {d['type']}  L{d['line']}"
                              + (f"  // {doc}" if doc else ""), 0))
    return rows


# ---------------------------------------------------------------- methods

def fn_sizes():
    """{rva: size} from db/functions.jsonl (and its additions), cached in build/digest/ by the files' mtimes."""
    files = [ROOT / "db" / n for n in ("functions.jsonl", "static-initializers.jsonl", "missed-functions.jsonl")]
    key = [(f.name, f.stat().st_mtime_ns) for f in files if f.exists()]
    cache = OUT / "fnsizes.pickle"
    try:
        with open(cache, "rb") as fh:
            saved = pickle.load(fh)
        if saved["key"] == key:
            return saved["sizes"]
    except (OSError, EOFError, KeyError, pickle.UnpicklingError):
        pass
    import tsdb
    sizes = {f["rva"]: f["size"] for f in tsdb.functions()}
    OUT.mkdir(parents=True, exist_ok=True)
    with open(cache, "wb") as fh:
        pickle.dump({"key": key, "sizes": sizes}, fh)
    return sizes


def slot_key(slot):
    """Primary vtable slots first by number, then the other vtables' by offset and number, then the rest."""
    if not slot:
        return (2, 0, 0)
    if slot.isdigit():
        return (0, 0, int(slot))
    off, _, k = slot.partition(":")
    return (1, int(off.lstrip("+"), 16), int(k))


def strip_rva(doc, rva):
    """A comment without its leading RVA when that is the row's (`0xa98560`, `0x2dfc80 (ret 4): ...`): the row
    shows it already. Another RVA (a thunk's body) stays."""
    m = re.match(r"\(?(0x[0-9a-fA-F]+)\)?(?:\s*\([^)]*\))?[:,;.]?\s*", doc)
    return doc[m.end():] if m and int(m.group(1), 16) == int(rva, 16) else doc


def method_rows(name, qual, index):
    import tsdb
    import whereis
    db = whereis.Db()
    cname, _ = tsdb.resolve_class(qual, db.classes)
    if cname is None:
        cname, _ = tsdb.resolve_class(name, db.classes)
    rvas = whereis.class_rvas(db, cname, name)
    slots = {}
    c = db.classes.get(cname) if cname else None
    if c:
        for vt in c["vftables"]:
            for k, e in enumerate(vt["entries"]):
                tag = str(k) if vt["offset"] == 0 else f"+{vt['offset']:#x}:{k}"
                slots.setdefault(e, tag)
    hooks = whereis.hook_targets(db, rvas)
    sizes = fn_sizes()
    decl_cache = {}
    rows = []
    for r in rvas:
        if r in db.library:
            continue
        target = hooks.get(r, ("", ""))[1]
        if not re.fullmatch(r"[\w:<>, *]+", target or "") or "::" not in target:
            target = ""  # an entry by name and RVA (`{"ns_cFoo_Create", 0x..`), not a member pointer
        full = db.name(r)
        cls, member = whereis.split_name(target or full)
        state = db.state(r) or "-"
        where, doc = "", ""
        if cls and member:
            key = (cls, member)
            if key not in decl_cache:
                decl_cache[key] = symfind.find_qualified(index, f"{cls}::{member}")
            found = decl_cache[key]
            claimed = [pd for pd in found if int(r, 16) in pd[1]["rvas"]]
            heads = [pd for pd in found if pd[0].endswith(".h")]
            heads = [pd for pd in heads if pd[0].startswith("src/")] or heads
            heads = [pd for pd in heads if pd[1]["scope"] == qual] or heads
            if claimed or len(heads) == 1 or (found and not heads):
                path, d = (claimed or heads or found)[0]
                where, doc = f"{path}:{d['line']}", d["doc"]
            elif heads:  # overloads, none of whose comments names this RVA: all their lines, no comment
                where = heads[0][0] + ":" + ",".join(str(d["line"]) for p, d in heads if p == heads[0][0])
                doc = "(overloads)"
        cpp = f"{cls}::{member}" if cls and member else (full.split(" (")[0])
        if not doc:
            m = re.search(r"\((.*)\)\s*$", full)
            doc = m.group(1) if m else ""
        slot = slots.get(r, "")
        inherited = cls and cls != name and not where
        doc = strip_rva(doc, r)
        rows.append((slot_key(slot), int(r, 16),
                     f"{r:>9} {sizes.get(r, 0):>5} {slot or '-':>7} {state:9} {cpp}"
                     + (f"  {where}" if where else "") + (f"  // {doc[:90]}" if doc else "")
                     + ("  (inherited)" if inherited else "")))
    rows.sort(key=lambda x: (x[0], x[1]))
    return [r[2] for r in rows], c is not None


# ---------------------------------------------------------------- helpers

def helper_rows(decls, qual, layouts=None):
    """(rows, types): the header's free functions, constants, variables, aliases and macros outside the class, one
    line each; and its other classes as `L42 cLightVector (12)` (size when the layout report has it), which
    `digest.py <that class>` shows in full."""
    rows, types = [], []
    scopes = {(d["scope"] + "::" + d["name"]).strip(":") for d in decls if d["kind"] == "type"}
    for d in decls:
        q = (d["scope"] + "::" + d["name"]).strip(":")
        if d["kind"] == "type":
            if q != qual and not q.startswith(qual + "::"):
                size = (layouts or {}).get(layout_key(layouts or {}, q), {}).get("size")
                shown = q if d["name"] == qual.split("::")[-1] else d["name"]
                types.append(f"L{d['line']} {shown}" + (f" ({size})" if size else ""))
            continue
        if d["kind"] not in ("function", "const", "var", "alias", "macro"):
            continue
        if d["scope"] in scopes or "::" in d["name"]:
            continue  # a member of a class, or an out-of-class definition of one
        if d["kind"] == "macro" and d["name"].endswith("_H"):
            continue
        doc = symfind.first_sentence(symfind.comment_of(d), 80)
        sig = d["sig"] if len(d["sig"]) <= 100 else d["sig"][:97] + "..."
        row = f"L{d['line']:<5} {d['kind']:8} {sig}" + (f"  // {doc}" if doc else "")
        if not rows or rows[-1] != row:  # `int a, b;` is two declarations on one line
            rows.append(row)
    return rows, types


def wrap(items, width=116):
    lines, cur = [], ""
    for it in items:
        if cur and len(cur) + len(it) + 2 > width:
            lines.append(cur + ",")
            cur = it
        else:
            cur = f"{cur}, {it}" if cur else it
    return lines + [cur] if cur else lines


# ---------------------------------------------------------------- filters

def parse_filter(text):
    """('off', lo, hi) | ('slot', tag) | ('text', lower) | None."""
    if text is None:
        return None
    m = re.fullmatch(r"(?:\+)?(0x[0-9a-fA-F]+)(?:\s*-\s*(?:\+)?(0x[0-9a-fA-F]+))?", text)
    if m:
        lo = int(m.group(1), 16)
        hi = int(m.group(2), 16) if m.group(2) else lo
        return ("off", lo, hi)
    m = re.fullmatch(r"(?:slot|s)(\+0x[0-9a-fA-F]+:)?(\d+)", text)
    if m:
        return ("slot", (m.group(1) or "") + m.group(2))
    return ("text", text.lower())


def keep_member(row, flt):
    off, text, size = row
    if flt is None:
        return True
    if flt[0] == "off":
        if off is None:
            return False
        end = off + max(size or 1, 1) - 1
        return off <= flt[2] and flt[1] <= end
    if flt[0] == "text":
        return flt[1] in text.lower()
    return False


def keep_method(text, flt):
    if flt is None:
        return True
    if flt[0] == "off":
        rva = int(text.split()[0], 16)
        return flt[1] <= rva <= flt[2]
    if flt[0] == "slot":
        return text.split()[2] == flt[1]
    if flt[1] in STATUSES:
        return text.split()[3] == flt[1]
    return flt[1] in text.lower()


STATUSES = {"mapped", "reimplemented", "verified", "trace-verified", "replaced", "-"}


# ---------------------------------------------------------------- main

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cls", help="class name (cFoo, ns::cFoo) or header path")
    ap.add_argument("filter", nargs="?", help="0x22c, 0x200-0x260, slot13 / s13, or a substring")
    ap.add_argument("--members", action="store_true")
    ap.add_argument("--methods", action="store_true")
    ap.add_argument("--helpers", action="store_true")
    ap.add_argument("--expand", action="store_true")
    args = ap.parse_intermixed_args(argv)
    sections = {s for s in ("members", "methods", "helpers") if getattr(args, s)} or {"members", "methods", "helpers"}
    flt = parse_filter(args.filter)
    if flt and flt[0] == "slot" and not (args.members or args.helpers):
        sections = {"methods"}
    if flt and flt[0] == "off" and not (args.methods or args.helpers):
        # An offset below 0x1000 is a member's (no function lives there); a larger one an RVA.
        sections = {"members"} if flt[2] < 0x1000 else {"methods"}

    index = symfind.load_index()
    header, qual, name = find_class(args.cls, index)
    rel = header.resolve().relative_to(ROOT).as_posix()
    decls = header_decls(header)
    tdecl = next((d for d in decls if d["kind"] == "type" and (d["scope"] + "::" + d["name"]).strip(":") == qual),
                 None)
    print(f"{qual}  {rel}:{tdecl['line'] if tdecl else '?'}" + (f"  {tdecl['sig'][:120]}" if tdecl else ""))

    field_index = {}
    for p, ds in index.items():
        for t in ds:
            if t[0] == "field":
                d = symfind.expand(t)
                d["path"] = p
                d["type"] = d["sig"].rsplit(" ", 1)[0] if " " in d["sig"] else ""
                field_index.setdefault((t[2].split("::")[-1], t[1]), d)

    layouts, err = {}, None
    if sections & {"members", "helpers"}:
        text, err = compile_layouts(header)
        layouts = parse_layouts(text) if text else {}
    if "members" in sections:
        qual_l = layout_key(layouts, qual)
        if qual_l is None:
            why = err or ("a class template: no layout without an instantiation"
                          if tdecl and tdecl["sig"].startswith("template") else
                          "the compiler reported no layout for it (forward-declared only, or in a .cpp)")
            head = (f"members: {why}; offsets below are the header comments' (unchecked)")
            rows = comment_member_rows(decls, qual)
        else:
            size = layouts[qual_l]["size"]
            head = (f"members (compiler layout, MSVC 14.40 x86): size {size} (0x{size:x})")
            want = (flt[1], flt[2]) if flt and flt[0] == "off" else None
            rows = member_rows(layouts, qual_l, qual, decls, field_index, args.expand, want)
        shown = [r for r in rows if keep_member(r, flt)]
        if shown or not flt or flt[0] != "text" or args.members:
            print(head)
            for r in shown:
                print("  " + r[1])
            if flt and not shown:
                print("  (no member matches)")

    if "methods" in sections:
        rows, has_class = method_rows(name, qual, index)
        shown = [r for r in rows if keep_method(r, flt)]
        if shown or not flt or flt[0] != "text" or args.methods:
            print(f"methods: {len(rows)} (rva, size, slot, status, ours, declared, comment)"
                  + ("" if has_class else "; not an RTTI class: no slots"))
        for r in shown:
            print("  " + r)
        if flt and not shown and (flt[0] != "text" or args.methods):
            print("  (no function matches)")

    if "helpers" in sections:
        rows, types = helper_rows(decls, qual, layouts)
        if flt and flt[0] == "text":
            rows = [r for r in rows if flt[1] in r.lower()]
            types = [t for t in types if flt[1] in t.lower()]
        elif flt:
            rows, types = [], []
        if rows or types or not flt:
            print(f"helpers and constants in {rel}: {len(rows)}")
            for r in rows:
                print("  " + r)
            if types:
                print(f"other classes there ({len(types)}; line, name, size):")
                for line in wrap(types):
                    print("  " + line)


if __name__ == "__main__":
    main()
