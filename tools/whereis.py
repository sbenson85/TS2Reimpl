"""Where our side has something: a class, interface, function or RVA -> our headers, src, tests, fakes, docs, methods.

classinfo.py answers for the original (vtables, slots, status, callees); this answers for our code, so a lane needn't
grep src/, tests/ and docs/ to find where a thing already lives. One line per hit, each section capped at --max.

Usage: tools/py -I tools/whereis.py cIViewerRefNodeBase          # a class or interface (unqualified works)
       tools/py -I tools/whereis.py 0xa6aec0                     # a function by RVA
       tools/py -I tools/whereis.py cShaderInit::Init            # a function by name (suffix of the db/ name)
       tools/py -I tools/whereis.py cIDisplayList --methods      # plus one line per RVA: our member, decl and def
  --max N    hits per section (default 6)
  --methods  for a class: `fn` lines, each of its RVAs with our function (hook entry or status name), its header and
             .cpp lines; and every db/methods fact, one per slot

Lines, by tag:
  decl   where our C++ declares the class/struct (or `using`), path:line and the line
  fns    the class's RVAs (its vtables, functions named after it, status entries under it) by status
  src    a src file implementing its RVAs (db/status `src`), with status counts
  test   a test file covering them (db/status `test`), with the number of functions
  fx     another tests/ file naming it (fixtures .h first): mentions, and tags: `derives` (a fake deriving from it),
         `kIid`, `StandIn`/`CodePatch` over its RVAs, `rva` (its RVAs as literals), `fakes` (a `// fakes: A, B` comment
         naming it, for fixtures whose fakes don't derive from or name it; those files list first)
  hook   (RVA) the hook entry in src/hook/tables and the function it points at
  slot   (RVA) the class vtables holding it
  ref    (RVA) other src/tests lines naming the RVA (OriginalFunction callers, comments, DIFF_TESTs)
  docs   a docs/subsystems or docs/contracts section mentioning it: path:line of the heading, the heading, mentions
  meth   db/methods facts for it (interface, implementing class, slots)
  impl   (interface) a class deriving from it, with its done/total
  users  src files naming it outside its own files, by mentions
"""
import argparse
import json
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DONE = ("verified", "trace-verified", "replaced", "library")
DOC_GLOBS = ("docs/subsystems/*.md", "docs/contracts/*.md")
GENERATED = ":!src/hook/trap_table.cpp"  # lists every RVA; never where anything lives
HOOK_ENTRY = r'^\s*\{\s*"[^"]*",\s*0[xX]([0-9a-fA-F]+)\s*,'  # {"FUN_a0aa7b90", 0xaa7b90, 32, 0x..., target}


def git_grep(patterns, paths, flags=("-E",)):
    """[(path, line, text)] of the lines matching any pattern, tracked and new files (git grep: fast, no cache)."""
    if not patterns:
        return []
    cmd = ["git", "-C", str(ROOT), "grep", "-n", "-I", "--untracked", "--no-color", *flags]
    for p in patterns:
        cmd += ["-e", p]
    out = subprocess.run(cmd + ["--", *paths], capture_output=True, text=True, encoding="utf-8", errors="replace")
    hits = []
    for line in out.stdout.splitlines():
        path, num, text = line.split(":", 2)
        hits.append((path, int(num), text))
    return hits


def rva_regex(rvas):
    """An ERE matching any of `rvas` as a hex literal (any case, leading zeros)."""
    return r"0[xX]0*(" + "|".join(sorted(r[2:].lstrip("0") for r in rvas)) + r")\b"


# ---- parsing (tested by tools/tests/test_whereis.py) ----

def class_extent(lines, start):
    """(first, last) 1-based line numbers of the class body declared at line `start` (1-based): from the declaration to
    the line closing its brace. Comments and string/char literals are skipped. last is start if no body follows."""
    depth, opened, i = 0, False, start - 1
    in_block = False
    while i < len(lines):
        text = lines[i]
        j = 0
        while j < len(text):
            two = text[j:j + 2]
            if in_block:
                if two == "*/":
                    in_block, j = False, j + 2
                    continue
                j += 1
                continue
            if two == "//":
                break
            if two == "/*":
                in_block, j = True, j + 2
                continue
            ch = text[j]
            if ch in "\"'":
                k = j + 1
                while k < len(text) and text[k] != ch:
                    k += 2 if text[k] == "\\" else 1
                j = k + 1
                continue
            if ch == ";" and not opened:
                return start, start  # a forward declaration
            if ch == "{":
                depth, opened = depth + 1, True
            elif ch == "}":
                depth -= 1
                if opened and depth == 0:
                    return start, i + 1
            j += 1
        i += 1
    return start, start


def member_line(lines, extent, member):
    """The first line (1-based) in `extent` declaring `member` (`member(`, also a destructor `~X(`), or None."""
    pat = re.compile(r"(?<![\w.>])" + re.escape(member) + r"\s*\(")
    for n in range(extent[0], extent[1] + 1):
        code = lines[n - 1].split("//")[0]
        if pat.search(code):
            return n
    return None


def doc_sections(text, pattern):
    """[(line, heading, mentions)] of the markdown sections (any heading level) whose own text matches `pattern`
    (a compiled regex), or whose heading names it (listed even with no mentions in its own text: a per-class section's
    facts often sit under its subsections); text before the first heading counts under line 1, heading '(top)'."""
    out, line, heading, count, fence = [], 1, "(top)", 0, False
    for n, l in enumerate(text.splitlines(), 1):
        if l.startswith("```"):
            fence = not fence
        if not fence and re.match(r"#{1,6} ", l):
            if count or pattern.search(heading):
                out.append((line, heading, count))
            line, heading, count = n, l.strip(), 0
            continue
        count += len(pattern.findall(l))
    if count or pattern.search(heading):
        out.append((line, heading, count))
    return out


def fx_tags(text, name, rvas_re):
    """Tags for one tests/ line naming `name` or one of its RVAs (see the module doc, `fx`)."""
    tags = set()
    if re.search(r"//\s*fakes:.*\b" + re.escape(name) + r"\b", text):
        tags.add("fakes")
    if re.search(r"\b(struct|class)\s+\w+[^;{]*:\s*[^;{]*\b" + re.escape(name) + r"\b", text):
        tags.add("derives")
    if re.search(r"\b" + re.escape(name) + r"::kIid\b", text):
        tags.add("kIid")
    if rvas_re and rvas_re.search(text):
        for kind in ("StandIn", "CodePatch"):
            if kind in text:
                tags.add(kind)
        tags.add("rva")
    return tags


def split_name(name):
    """(class, member) of a status/db name `ns::cFoo<T>::Bar extra` -> ('cFoo', 'Bar'); class '' if unqualified."""
    head = name.split(" ")[0]
    depth, parts, cur = 0, [], ""
    for ch in head:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif ch == ":" and depth == 0:
            if cur:
                parts.append(cur)
            cur = ""
            continue
        if depth == 0 and ch != ">":
            cur += ch
    parts.append(cur)
    return (parts[-2] if len(parts) > 1 else ""), parts[-1]


# ---- the lookup ----

def index_names(names):
    """class component -> RVAs of the names under it (`ns::cFoo::Bar` -> cFoo; template arguments dropped)."""
    index = defaultdict(set)
    for rva, name in names:
        cls, _ = split_name(name)
        if cls:
            index[cls].add(rva)
    return index


CACHE = ROOT / "build/whereis-cache.pickle"


def load_cached():
    """The parsed db/ (classes, names, status, library RVAs, methods, name index), cached in build/ while no db/ jsonl
    file changes (size and mtime of each: ~600 stats instead of ~2 s of JSON)."""
    import pickle
    files = sorted(ROOT.glob("db/*.jsonl")) + sorted(ROOT.glob("db/status/*.jsonl")) + sorted(ROOT.glob("db/methods/*.jsonl"))
    key = [(f.name, f.stat().st_size, f.stat().st_mtime_ns) for f in files]
    try:
        with open(CACHE, "rb") as fh:
            cached = pickle.load(fh)
        if cached["key"] == key:
            return cached
    except (OSError, EOFError, KeyError, pickle.UnpicklingError):
        pass
    d = {"key": key}
    d["classes"] = {c["name"]: c for c in tsdb.jsonl("classes.jsonl")}
    d["names"] = {n["rva"]: n["name"] for n in tsdb.jsonl("names.jsonl")}
    d["status"] = tsdb.load_status()
    d["library"] = tsdb.library_rvas()
    d["methods"] = []
    for f in sorted((ROOT / "db/methods").glob("*.jsonl")):
        for e in map(json.loads, filter(str.strip, open(f, encoding="utf-8"))):
            d["methods"].append(dict(e, _file=f"db/methods/{f.name}"))
    d["index"] = index_names(list(d["names"].items()) + [(r, e["name"]) for r, e in d["status"].items()])
    try:
        CACHE.parent.mkdir(exist_ok=True)
        with open(CACHE, "wb") as fh:
            pickle.dump(d, fh, protocol=pickle.HIGHEST_PROTOCOL)
    except OSError:
        pass
    return d


class Db:
    def __init__(self):
        d = load_cached()
        self.classes, self.names, self.status = d["classes"], d["names"], d["status"]
        self.library, self.methods, self.index = d["library"], d["methods"], d["index"]
        self._decls = {}
        self._hooks = None

    def hooks(self):
        """rva -> (path:line, entry text) of every hook table entry (one git grep)."""
        if self._hooks is None:
            self._hooks, self.hook_index = {}, defaultdict(set)
            for path, line, text in git_grep([HOOK_ENTRY], ["src/hook/tables"]):
                rva = hex(int(re.search(HOOK_ENTRY, text).group(1), 16))
                self._hooks[rva] = (f"{path}:{line}", text)
                for cls in re.findall(r"[&:](\w+)::\w+\b(?!::)", text):  # &ns::cFoo::Bar, MemberAddress<&cFoo::Bar>
                    self.hook_index[cls].add(rva)
        return self._hooks

    def state(self, rva):
        return self.status.get(rva, {}).get("status") or ("library" if rva in self.library else "")

    def name(self, rva):
        return self.status.get(rva, {}).get("name") or self.names.get(rva, "?")

    def decls(self, outer):
        """[(path, line, text)] of our declarations of `outer` (class/struct with a body, or a using/typedef)."""
        if outer not in self._decls:
            w = re.escape(outer)
            hits = git_grep([r"^\s*(template\s*<.*>\s*)?(class|struct)\s+([A-Za-z_]\w*\s+)*" + w + r"\b[^;]*$",
                             r"^\s*using\s+" + w + r"\s*=", r"^\s*typedef\b.*\b" + w + r"\s*;"], ["src", GENERATED])
            self._decls[outer] = [h for h in hits if not h[2].rstrip().endswith(";") or "using" in h[2]
                                  or "typedef" in h[2]]
        return self._decls[outer]

    def member_decl(self, cls_outer, member):
        """'path:line' of `member` in our declaration of class `cls_outer`, or None."""
        for path, line, text in self.decls(cls_outer):
            lines = (ROOT / path).read_text(encoding="utf-8", errors="replace").splitlines()
            ext = class_extent(lines, line)
            n = member_line(lines, ext, member) if ext[1] > ext[0] else None
            if n:
                return f"{path}:{n}"
        return None

    def member_def(self, cls_outer, member, path):
        """'path:line' of `cls_outer::member(` defined in `path` (a .cpp), or None."""
        if not path or not (ROOT / path).exists():
            return None
        pat = re.compile(r"\b" + re.escape(cls_outer) + r"::" + re.escape(member) + r"\s*\(")
        for n, l in enumerate((ROOT / path).read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if pat.search(l) and not l.rstrip().endswith(";"):
                return f"{path}:{n}"
        return None


def capped(lines, limit, more_hint="--max"):
    for l in lines[:limit]:
        print(l)
    if len(lines) > limit:
        print(f"       ... {len(lines) - limit} more ({more_hint})")


def class_rvas(db, cname, outer):
    """The class's own RVAs: vtable entries, functions named after it, status entries named under it."""
    rvas = set()
    c = db.classes.get(cname)
    if c:
        rvas |= {e for vt in c["vftables"] for e in vt["entries"]}
    if not (cname or "").startswith(".?A"):  # one template instantiation: its vtables only (names don't tell them apart)
        rvas |= db.index.get(outer, set())  # db/names and db/status names `...::outer::member`
    # Hook entries pointing at one of our members (status names can be FUN_...).
    db.hooks()
    rvas |= db.hook_index.get(outer, set())
    return rvas


def show_docs(pattern, limit):
    rows = []
    for g in DOC_GLOBS:
        for f in sorted(ROOT.glob(g)):
            rel = f.relative_to(ROOT).as_posix()
            for line, heading, n in doc_sections(f.read_text(encoding="utf-8", errors="replace"), pattern):
                named = bool(pattern.search(heading))
                rows.append((named, heading.count("#", 0, 4), n, f"docs   {rel}:{line}  {heading[:70]}  ({n}x)"))
    # sections whose heading names it first (higher-level headings before their subsections), then by mentions
    rows.sort(key=lambda r: (not r[0], r[1] if r[0] else 0, -r[2]))
    capped([r[3] for r in rows], limit)


def show_methods(db, keys, limit, full):
    groups = defaultdict(list)
    for m in db.methods:
        if m.get("iface") in keys or m.get("class") in keys:
            groups[(m["_file"], m.get("iface"), m.get("class"))].append(m)
    rows = []
    for (f, iface, cls), ms in groups.items():
        if "alias_of" in ms[0]:
            rows.append(f"meth   {f}  {iface} = alias of {ms[0]['alias_of']}")
            continue
        slots = sorted(m["slot"] for m in ms if "slot" in m)
        lanes = ",".join(sorted({m.get("lane", "?") for m in ms}))
        rng = f"{slots[0]}-{slots[-1]}" if slots else "-"
        rows.append(f"meth   {f}  {iface} (class {cls}): {len(ms)} slots {rng}, lanes {lanes}")
        if full:
            for m in sorted(ms, key=lambda m: m.get("slot", 0)):
                ps = ", ".join(p.get("cpp", p.get("type", "?")) + " " + p.get("name", "") for p in m.get("params", []))
                t = " t" if m.get("tentative") else ""
                rows.append(f"         [{m.get('slot')}] {m.get('ret', '?')} {m.get('name')}({ps}){t}")
    capped(rows, limit if not full else 10 ** 6)


def show_status_files(db, rvas, limit):
    by_src, by_test = defaultdict(Counter), Counter()
    for r in rvas:
        e = db.status.get(r)
        if not e:
            continue
        by_src[e.get("src") or "(no src)"][e["status"]] += 1
        for t in (e.get("test") or "").replace(",", " ").split():
            if t.startswith("tests/"):
                by_test[t] += 1
    rows = sorted(by_src.items(), key=lambda kv: -sum(kv[1].values()))
    capped([f"src    {p}  " + ", ".join(f"{k} {v}" for k, v in sorted(c.items())) for p, c in rows], limit)
    capped([f"test   {t}  {n} fns" for t, n in by_test.most_common()], limit)
    return set(by_src), set(by_test)


def show_fx(name, rvas, skip, limit):
    """tests/ files naming `name` or its RVAs, other than `skip` (the status test files)."""
    rv = [r for r in rvas]
    pats = [r"\b" + re.escape(name) + r"\b"] + ([rva_regex(rv)] if rv else [])
    rvas_re = re.compile(rva_regex(rv), re.I) if rv else None
    files = defaultdict(lambda: [0, None, set()])
    for path, line, text in git_grep(pats, ["tests"], ("-E", "-i") if rv else ("-E",)):
        if path in skip and not path.endswith(".h"):
            continue
        f = files[path]
        if not re.search(r"\b" + re.escape(name) + r"\b", text) and not (rvas_re and rvas_re.search(text)):
            continue  # -i matched a different case of the name
        f[0] += 1
        f[1] = f[1] or line
        f[2] |= fx_tags(text, name, rvas_re)
    rows = sorted(files.items(), key=lambda kv: ("fakes" not in kv[1][2], not kv[0].endswith(".h"), -len(kv[1][2]), -kv[1][0]))
    capped([f"fx     {p}:{f[1]}  {f[0]}x  {','.join(sorted(f[2]))}" for p, f in rows if f[0]], limit)


def show_users(name, own, limit):
    counts = Counter(p for p, _, _ in git_grep([r"\b" + re.escape(name) + r"\b"], ["src", GENERATED, ":!src/hook/tables"]))
    for p in own:
        counts.pop(p, None)
        counts.pop(p[:-4] + ".h" if p.endswith(".cpp") else p, None)
    if counts:
        top = " ".join(f"{p}({n})" for p, n in counts.most_common(limit))
        print(f"users  {len(counts)} src files: {top}" + (" ..." if len(counts) > limit else ""))


def report_class(db, cname, query, args):
    outer = tsdb.class_outer(cname or query)
    c = db.classes.get(cname)
    head = tsdb.pretty_class(cname) if cname else query
    if c:
        print(f"{head}  RTTI {c['td']}, {len(c['vftables'])} vftables, bases: "
              + ", ".join(tsdb.pretty_class(b) for b in c["bases"][:4]) + (" ..." if len(c["bases"]) > 4 else ""))
    else:
        print(f"{head}  (not an RTTI class: our side only)")
    capped([f"decl   {p}:{n}  {t.strip()[:90]}" for p, n, t in db.decls(outer)], args.max)

    rvas = class_rvas(db, cname, outer) if (c or cname) else class_rvas(db, None, outer)
    if rvas:
        counts = Counter(db.state(r) or "none" for r in rvas)
        todo = sum(v for k, v in counts.items() if k not in DONE)
        print(f"fns    {len(rvas)} RVAs, {todo} to do: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    srcs, tests = show_status_files(db, rvas, args.max)
    if args.methods:
        show_members(db, rvas, args)
    # Its RVAs as literals in tests: only the ones named under it (inherited and folded bodies are other classes').
    under = re.compile(r"(^|::)" + re.escape(outer) + r"(::|<)")
    show_fx(outer, [r for r in rvas if r not in db.library and under.search(db.name(r).split(" ")[0])], tests,
            args.max)
    show_docs(re.compile(r"\b" + re.escape(outer) + r"\b"), args.max)
    show_methods(db, {outer, cname or outer}, args.max, args.methods)
    if c is not None or not cname:
        impls = [n for n, k in db.classes.items() if n != cname and any(tsdb.class_outer(b) == outer and
                                                                         (b == cname or not cname) for b in k["bases"])]
        rows = []
        for n in impls:
            own = class_rvas(db, n, tsdb.class_outer(n))
            done = sum(db.state(r) in DONE for r in own)
            rows.append((len(own) - done, f"impl   {tsdb.pretty_class(n)}  {done}/{len(own)} done"))
        rows.sort(key=lambda r: -r[0])
        capped([r[1] for r in rows], args.max)
    show_users(outer, {p for p, _, _ in db.decls(outer)} | srcs, args.max)


def show_members(db, rvas, args):
    hooks = hook_targets(db, rvas)
    rows = []
    for r in sorted(rvas, key=lambda r: int(r, 16)):
        if r in db.library:
            continue
        e = db.status.get(r, {})
        target = hooks.get(r, ("", ""))[1]
        cls, member = split_name(target or db.name(r))
        where = []
        if cls:
            d = db.member_decl(cls, member.lstrip("~"))
            if d:
                where.append("decl " + d)
            d = db.member_def(cls, member, e.get("src"))
            if d:
                where.append("def " + d)
        rows.append(f"fn     {r:>9} {e.get('status', db.state(r)) or '-':9} {(target or db.name(r))[:60]}  {'  '.join(where)}")
    capped(rows, 10 ** 6)


def hook_targets(db, rvas):
    """rva -> (path:line, our function) from the hook tables."""
    out = {}
    for r in rvas:
        if r in db.hooks():
            where, text = db.hooks()[r]
            t = re.search(r"&([\w:]+(?:<[^>]*>)?[\w:]*)", text)
            out[r] = (where, t.group(1) if t else text.strip()[:60])
    return out


def hooked_as(db, q):
    """RVAs whose hook entry points at our function `q` (`Class::Member`, any namespace prefix)."""
    if "::" not in q:
        return set()
    pat = re.compile(r"[&:]" + re.escape(q) + r"\b")
    return {r for r, (_, text) in db.hooks().items() if pat.search(text)}


def report_rva(db, rva, args):
    e = db.status.get(rva, {})
    print(f"{rva}  {db.name(rva)[:90]}  {db.state(rva) or 'no status'}" + (f"  ({e['_file']})" if e else ""))
    hook = hook_targets(db, [rva]).get(rva)
    cls, member = split_name((hook[1] if hook else "") or db.name(rva))
    if e.get("src"):
        d = db.member_def(cls, member, e["src"]) if cls else None
        print(f"src    {d or e['src']}" + (f"  test {e['test']}" if e.get("test") else ""))
    if e.get("original_calls"):
        oc = e["original_calls"]
        print(f"orig   {len(oc)} original calls: " + " ".join(str(o.get('rva', o) if isinstance(o, dict) else o)
                                                           for o in oc[:8]))
    print(f"hook   {hook[0]} -> {hook[1]}" if hook else "hook   none in src/hook/tables")
    if cls:
        d = db.member_decl(cls, member.lstrip("~"))
        if d:
            print(f"decl   {d}  {cls}::{member}")
    slots = [f"{tsdb.pretty_class(n)}[{i}]" for n, c in db.classes.items() for vt in c["vftables"]
             for i, x in enumerate(vt["entries"]) if x == rva]
    if slots:
        print(f"slot   {len(slots)}: " + " ".join(slots[:args.max]) + (" ..." if len(slots) > args.max else ""))
    rows = []
    for path, line, text in git_grep([rva_regex([rva])], ["src", "tests", GENERATED, ":!src/hook/tables"], ("-E", "-i")):
        rows.append(f"ref    {path}:{line}  {text.strip()[:100]}")
    rows.sort(key=lambda r: ("OriginalFunction" not in r, "src/" not in r[:12]))
    capped(rows, args.max)
    show_docs(re.compile(r"0x0*" + rva[2:] + r"\b", re.I), args.max)
    keys = {(tsdb.class_outer(n), i) for n, c in db.classes.items() for vt in c["vftables"] if vt["offset"] == 0
            for i, x in enumerate(vt["entries"]) if x == rva}
    facts = [m for m in db.methods if (tsdb.class_outer(m.get("class") or ""), m.get("slot")) in keys]
    capped([f"meth   {m['_file']}  {m.get('iface')}[{m.get('slot')}] {m.get('ret', '?')} {m.get('name')}"
            f" (class {m.get('class')})" for m in facts], args.max)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("what", help="class, interface, Class::function, function name or 0xRVA")
    ap.add_argument("--max", type=int, default=6)
    ap.add_argument("--methods", action="store_true")
    args = ap.parse_args()
    db = Db()
    q = args.what
    if re.fullmatch(r"0[xX][0-9a-fA-F]+", q):
        return report_rva(db, hex(int(q, 16)), args)
    cname, similar = tsdb.resolve_class(q, db.classes)
    if cname:
        return report_class(db, cname, q, args)
    if "::" in q or not similar:
        # A function by name: the db/ names ending in the query.
        hits = sorted({r for r, e in db.status.items() if e["name"].split(" ")[0].endswith(q)}
                      | {r for r, n in db.names.items() if n.endswith(q) and ("::" in q or n == q)}
                      | hooked_as(db, q), key=lambda r: int(r, 16))
        if len(hits) == 1:
            return report_rva(db, hits[0], args)
        if hits:
            print(f"{len(hits)} functions named *{q}:")
            capped([f"  {r:>9} {db.state(r) or '-':9} {db.name(r)[:90]}" for r in hits], args.max * 3)
            return
        if "::" in q:
            print(f"no function or class named {q}")
            return
    if similar:
        print(f"no single class named {q}; candidates (showing our side by the bare name):")
        capped(["  " + tsdb.pretty_class(n) for n in similar], args.max)
    report_class(db, None, q, args)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    main()
