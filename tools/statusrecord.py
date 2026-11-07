"""Drafts and lists a group's status entries: tools/status.py --record and --brief (lane PG).

--record drafts the mechanical fields of db/status/<group>.jsonl from what the tree already says, so a lane writes only
the judgement (the notes' function-specific part, and the status if the draft's is wrong):
  - which functions: the group's hook table rows (src/hook/tables/<group>.cpp), the DIFF_TEST RVAs of its test files,
    the group's existing entries, --add RVAs, and any other function its test files name (in a test block or the
    file's comments) that the callers check finds unreachable once the drafted functions are ours. Library code,
    another group's entries and the original functions the src files still call are never drafted. `--class C` keeps
    only C's own functions (class_own: its vtables' slots no class outside its family uses, functions named `C::`,
    and what only those reach), for a test file covering several classes; the rest are listed as not drafted.
  - src: the file defining the function (the hook's replacement, else a function of that class and method name in the
    group's src files), else the existing entry's, else the file of the hook its trap rests on, else the only src file.
  - test: the test file naming the RVA (its DIFF_TEST, then a test block, then a comment). status: the existing one;
    else verified when a test file names the RVA, else reimplemented.
  - original_calls: the OriginalFunction sites in the function's body and in the helpers it calls that are no entry's
    own (same file, followed transitively: tools/origcalls.py's scan, tools/cppblocks.py's bodies).
  - in_effect / callers_check / trap: for every drafted function without a hook row, from tools/callers.py's Checker
    on the plan (the drafted functions ours), as stagea.py does; open references are listed.
  - notes (new entries only): the test names, the mutant results of --mutate-log (the run's lines attributed to
    functions through --mutants, tools/testselect.py's targets_of), then TODO for the function-specific remainder.
Output: the new and changed lines on stdout (JSONL), the report on stderr; --out writes the whole merged file
elsewhere; --write merges into db/status/<group>.jsonl: new RVAs appended; on existing lines only missing fields are
added and in_effect / trap refreshed (callers_check with them), every other differing field is kept and listed. Notes
and status are never overwritten.

--brief prints one line per entry: rva, status, in_effect, number of original calls, test file, name.
"""
import json
import re
import sys
from datetime import date
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import callers  # noqa: E402
import cppblocks  # noqa: E402
import origcalls  # noqa: E402
import testselect  # noqa: E402
import traps  # noqa: E402
import tsdb  # noqa: E402

FIELDS = ("rva", "name", "status", "src", "test", "notes", "original_calls", "in_effect", "callers_check", "trap")
REFRESH = ("in_effect", "trap")  # existing values the merge replaces (callers_check follows them)
REPLACEMENT = re.compile(r"&\s*([A-Za-z_][\w:]*)\s*\)")
MUTANT_LINE = re.compile(r"^\s*(\d+)\. (?:\[json \d+\] )?(killed|SURVIVED|INVALID)\s+\(([^,]*), \d+s\) (.*?)(?: <- (.*))?$")


def _rel(p):
    return str(p).replace("\\", "/")


def split_name(name):
    """(class, method) of a function name: templates, parenthesised notes and ` / ` alternatives dropped."""
    n = re.split(r"\s+/\s+|\s+\(", name or "", maxsplit=1)[0].strip()
    while True:
        m = re.sub(r"<[^<>]*>", "", n)
        if m == n:
            break
        n = m
    parts = [p for p in n.split("::") if p]
    if not parts:
        return "", ""
    return (parts[-2] if len(parts) > 1 else ""), parts[-1]


def hook_rows(group, root=None):
    """[(name, rva int, replacement qual or None)] of src/hook/tables/<group>.cpp."""
    p = Path(root or tsdb.ROOT) / "src/hook/tables" / f"{group}.cpp"
    if not p.exists():
        return []
    text = p.read_text(encoding="utf-8", errors="replace")
    out = []
    for m in callers.HOOK_ROW.finditer(text):
        end = text.find("\n", m.end())
        line = text[m.start():end if end >= 0 else len(text)]
        reps = REPLACEMENT.findall(line)
        out.append((m.group(1), int(m.group(2), 16), reps[-1] if reps else None))
    return out


def parse_mutate_log(text):
    """The last run's mutant lines of a tools/mutate.py log: [{index (0-based), verdict, outcome, label, by}]."""
    lines = text.splitlines()
    start = max((i for i, l in enumerate(lines) if l.startswith("clean run passed")), default=-1) + 1
    out = []
    for l in lines[start:]:
        m = MUTANT_LINE.match(l)
        if m:
            out.append({"index": int(m.group(1)) - 1, "verdict": m.group(2), "outcome": m.group(3),
                        "label": m.group(4), "by": m.group(5)})
    return out


def mutant_summary(results):
    """`Mutants: 4 killed, 1 survived (label)` for a list of parsed log lines."""
    if not results:
        return ""
    killed = sum(r["verdict"] == "killed" for r in results)
    parts = [f"{killed} killed"] if killed else []
    for verdict, word in (("SURVIVED", "survived"), ("INVALID", "invalid")):
        rs = [r for r in results if r["verdict"] == verdict]
        if rs:
            parts.append(f"{len(rs)} {word} ({'; '.join(r['label'] for r in rs)})")
    return "Mutants: " + ", ".join(parts) + "."


def class_own(cls, root=None):
    """(full class name, {rva int: why}) of the functions that are one class's own (status.py --record --class, lane
    QE): its vtable slots that no class outside its family uses (every class whose vtables hold the slot is the class
    or derives from it: inherited bodies stay with their base), the functions db/names.jsonl names `Class::...`, and
    the functions only those reach (every direct caller, tail calls included, is one of them; library code never).
    `cls` is the full RTTI name or its last `::` part when that is unique. Raises ValueError if it names no class."""
    rows = [json.loads(l) for l in (Path(root or tsdb.ROOT) / "db/classes.jsonl").read_text(encoding="utf-8").splitlines()
            if l.strip()]
    classes = {c["name"]: c for c in rows}
    if cls not in classes:
        hits = [n for n in classes if n.split("::")[-1] == cls]
        if len(hits) != 1:
            similar = hits or [n for n in classes if cls.lower() in n.lower()][:10]
            raise ValueError(f"--class {cls!r} names {'several classes' if hits else 'no class'}"
                             + (f"; give one of: {', '.join(similar)}" if similar else ""))
        cls = hits[0]
    me = classes[cls]
    holders = {}  # slot rva -> classes whose vtables hold it
    for c in rows:
        for vt in c.get("vftables", []):
            for e in vt["entries"]:
                holders.setdefault(int(e, 16), set()).add(c["name"])
    own = {}
    for vt in me.get("vftables", []):
        for i, e in enumerate(vt["entries"]):
            r = int(e, 16)
            if r not in own and all(h == cls or cls in classes[h]["bases"] for h in holders[r]):
                own[r] = f"slot {i} of vftable {vt['rva']}"
    prefix = cls + "::"
    named = [(n["rva"], n["name"]) for n in tsdb.jsonl("names.jsonl")]
    for a in tsdb.jsonl("annotations.jsonl"):  # Ghidra names lanes recorded (tools/ghidra.py annotate)
        if a.get("kind") in ("function", "name") and a.get("name") and "rva" in a:
            n = a["name"]
            named.append((a["rva"], f"{a['class']}::{n}" if a.get("class") and "::" not in n else n))
    for r, n in named:
        if n.startswith(prefix):
            own.setdefault(int(r, 16), "named after the class")
    callees = {int(f["rva"], 16): {int(c, 16) for c in f.get("callees", []) if "!" not in c} for f in tsdb.functions()}
    for e in tsdb.jsonl("tail-calls.jsonl"):
        callees.setdefault(int(e["rva"], 16), set()).update(int(t, 16) for t in e["targets"])
    callers_of = {}
    for r, cs in callees.items():
        for c in cs:
            callers_of.setdefault(c, set()).add(r)
    library = {int(r, 16) for r in tsdb.library_rvas()}
    while True:
        more = {c for r in own for c in callees.get(r, ()) if c not in own and c not in library
                and c not in holders and callers_of.get(c) and callers_of[c] <= own.keys()}
        if not more:
            return cls, own
        for c in more:
            own[c] = "reached only from the class's own functions"


class Recorder:
    """One draft of a group. db / image: callers.Db and callers.Image (built if not given; reuse them across drafts).
    use_existing=False ignores the group's current entries except as RVAs to draft (tests of this tool)."""

    def __init__(self, group, tests=(), srcs=(), add=(), mutate_logs=(), mutants=(), use_existing=True, db=None,
                 image=None, root=None, cls=None):
        self.group, self.root = group, Path(root or tsdb.ROOT)
        self.report = []
        self.db = db or callers.Db()
        if self.db.binary_note:
            self.report.append(f"note: {self.db.binary_note}")
        self.image = image or callers.Image()
        self.file = f"{group}.jsonl"
        entries = tsdb.status_entries()
        self.existing = {int(e["rva"], 16): e for f, e in entries if f == self.file}
        self.other = {int(e["rva"], 16): f for f, e in entries if f != self.file}
        self.use_existing = use_existing
        mine = self.existing if use_existing else {}
        self.hooks = hook_rows(group, self.root)
        self.sources = origcalls.source_files(self.root)
        self.texts = testselect.read_tests(self.root)
        self.tests = testselect.scan_tests(self.root, self.texts)
        hooked_here = {r for _, r, _ in self.hooks}
        # Test files: given, the existing entries', and those with a DIFF_TEST on a hooked function of the group.
        self.test_files = list(dict.fromkeys(
            [_rel(t) for t in tests]
            + [e["test"] for e in mine.values() if e.get("test", "").startswith("tests/")]
            + [t["file"] for t in self.tests if t["rva"] and int(t["rva"], 16) in hooked_here]))
        # Src files: given, the existing entries', and the files defining the hooks' replacements.
        self.src_files = list(dict.fromkeys(
            [_rel(s) for s in srcs]
            + [s.strip() for e in mine.values() for s in re.split(r"[,;]", e.get("src") or "") if s.strip()]))
        self.hook_src = {}
        self._blocks = {}
        for name, rva, qual in self.hooks:
            hit = qual and (self.find_block(qual, self.sources, exact=True) or self.find_block(qual, self.sources))
            if hit:
                self.hook_src[name] = hit[0]
                if hit[0] not in self.src_files:
                    self.src_files.append(hit[0])
        self.blocks = {f: self.blocks_of(f) for f in self.src_files if f in self.sources}
        self.sites = {f: origcalls.uses(self.sources[f]) for f in self.blocks}
        self.called_originals = {int(r, 16) for s in self.sites.values() for _, r in s if r}
        self.library = tsdb.library_rvas()
        self.annotated = {}  # rva -> [names], in file order (as tools/hookspec.py reads them)
        for a in tsdb.jsonl("annotations.jsonl"):
            if a.get("kind") in ("function", "name") and a.get("name") and "rva" in a:
                n = a["name"]
                n = f"{a['class']}::{n}" if a.get("class") and "::" not in n else n
                self.annotated.setdefault(int(a["rva"], 16), []).append(n)
        self.rvas = self.pick(add, mine)
        self.cls = None
        if cls:  # --class: only that class's own functions (class_own); the group's other entries stay as they are
            self.cls, own = class_own(cls, self.root)
            dropped = sorted(self.rvas - own.keys())
            if dropped:
                self.report.append(f"--class {self.cls}: {len(dropped)} not drafted (not its own vtables' slots, named "
                                   f"after it, or reached only from those): "
                                   + ", ".join(f"{r:#x} {self.name_of(r)}" for r in dropped[:12])
                                   + (" ..." if len(dropped) > 12 else ""))
            self.rvas &= own.keys()
            self.must &= own.keys()
        self.mutate_logs, self.mutants = list(mutate_logs), list(mutants)

    # --- where the code is -------------------------------------------------------------------------------------------
    def blocks_of(self, f):
        if f not in self._blocks:
            self._blocks[f] = cppblocks.blocks(self.sources.get(f, ""))
        return self._blocks[f]

    def find_block(self, qual, files, exact=False):
        """(file, Block) defining `qual` in `files` (paths under src/): the only function of the same class and method,
        else (unless exact) the only function of that method name."""
        cls, method = split_name(qual)
        if not method or (exact and not cls):
            return None
        pat = re.compile(rf"\b{re.escape(method)}\s*\(")
        same, any_ = [], []
        for f in files:
            text = self.sources.get(f, "")
            if not f.endswith(".cpp") or (exact and cls not in text) or not pat.search(text):
                continue
            for b in self.blocks_of(f):
                if b.kind == "test":
                    continue
                bc, bm = split_name(b.qual)
                if bm == method:
                    any_.append((f, b))
                    if bc == cls:
                        same.append((f, b))
        if len(same) == 1:
            return same[0]
        if not same and not exact and len(any_) == 1:
            return any_[0]
        return None

    def names_of(self, rva):
        """The function's names, best first: its entry's, the Ghidra annotations' (tools/ghidra.py annotate), db's."""
        e = self.existing.get(rva)
        out = [e["name"]] if e and self.use_existing else []
        out += self.annotated.get(rva, [])[::-1]  # the latest annotation first
        out += [n for n in (self.db.names.get(rva), self.db.funcs.get(rva, {}).get("name")) if n]
        return list(dict.fromkeys(out)) or ["?"]

    def name_of(self, rva):
        return self.names_of(rva)[0]

    # --- which functions ---------------------------------------------------------------------------------------------
    def pick(self, add, mine):
        must = {int(str(r), 16) if isinstance(r, str) else r for r in add} | set(mine) | {r for _, r, _ in self.hooks}
        for t in self.tests:
            if t["file"] in self.test_files and t["rva"]:
                must.add(int(t["rva"], 16))
        maybe = set()
        for f in self.test_files:
            maybe |= {int(r, 16) for r in testselect.named_rvas(self.tests, f)}
        keep = set()
        for r in must | maybe:
            why = self.excluded(r)
            if why and r in must and r not in mine:
                self.report.append(f"{r:#x} not drafted: {why}")
            elif not why or r in mine:
                keep.add(r)
        self.must = (must & keep)
        return keep

    def excluded(self, r):
        if r not in self.db.funcs:
            return "not a function start"
        if f"{r:#x}" in self.library:
            return "library code (our toolchain's)"
        if r in self.other:
            return f"listed in db/status/{self.other[r]}"
        if self.db.names.get(r, "").startswith("shared_vf_"):
            return "a body folded across classes (shared_vf_*: docs/verification.md)"
        if r in self.called_originals:
            return "an original the src files still call (OriginalFunction)"
        return None

    # --- in effect through callers -----------------------------------------------------------------------------------
    def checker(self, planned):
        status = dict(self._status)
        for r in planned:
            e = dict(status.get(r, {}))
            if e.get("status") not in tsdb.OURS:
                e["status"] = "reimplemented"
            e["in_effect"] = "callers"
            status[r] = e
        self.db.status = status
        return callers.Checker(self.image, self.db)

    def plan(self):
        """{rva: (open references, trap)} for the drafted functions without a hook; drops named-only functions the
        check leaves reachable (the original still runs: not ours yet, or not this group's)."""
        self._status = dict(self.db.status)
        try:
            unhooked = {r for r in self.rvas if r not in self.db.hooked}
            for _ in range(6):
                chk = self.checker(unhooked)
                drop = set()
                for r in sorted(unhooked - self.must):
                    v = chk.verdict(r)
                    if v is None or v["open"]:
                        drop.add(r)
                if not drop:
                    break
                for r in sorted(drop):
                    self.report.append(f"{r:#x} {self.name_of(r)[:60]}: named by a test but still reachable (not "
                                       "drafted; --add it to draft it anyway)")
                unhooked -= drop
                self.rvas -= drop
            out = {}
            for r in sorted(unhooked):
                v = chk.verdict(r)
                opened = ["cycle"] if v is None else v["open"]
                trap = "&".join(f"Hooks.{h}=1" for h in chk.trap_hooks(r))
                out[r] = (opened, trap)
            return out
        finally:
            self.db.status = self._status

    # --- original calls ----------------------------------------------------------------------------------------------
    def entry_blocks(self):
        """{rva: (file, Block)} for the drafted functions defined in the group's src files."""
        out = {}
        hooked_qual = {r: q for _, r, q in self.hooks if q}
        for r in self.rvas:
            names = ([hooked_qual[r]] if r in hooked_qual else []) + self.names_of(r)
            # The class and method in the group's files, then anywhere in src/ (a file the group hasn't named yet),
            # then the method alone in the group's files.
            for name, files, exact in ([(n, list(self.blocks), True) for n in names]
                                       + [(n, self.sources, True) for n in names]
                                       + [(n, list(self.blocks), False) for n in names]):
                hit = self.find_block(name, files, exact)
                if hit:
                    out[r] = hit
                    if hit[0] not in self.blocks:
                        self.src_files.append(hit[0])
                        self.blocks[hit[0]] = self.blocks_of(hit[0])
                        self.sites[hit[0]] = origcalls.uses(self.sources[hit[0]])
                    break
        return out

    def original_calls(self, where):
        """{rva: [original rva hex]} and the unattributed sites [(file, line, rva)]: a function's own sites, then those
        of the helpers it calls (same file, transitively) that are no drafted function's body."""
        owned = {(f, b.open) for f, b in where.values()}
        out, used = {}, set()
        for r, (f, b) in where.items():
            text = self.sources[f]
            blank = cppblocks.blank(text)
            helpers = [h for h in self.blocks[f] if (f, h.open) not in owned and h.kind != "test"]
            seen, queue, calls = {b.open}, [b], []
            while queue:
                cur = queue.pop(0)
                for line, rva in self.sites[f]:
                    at = _offset(text, line)
                    if rva and cur.open <= at <= cur.close and rva not in calls:
                        calls.append(rva)
                        used.add((f, line))
                body = blank[cur.open:cur.close]
                for h in helpers:
                    if h.open not in seen and re.search(rf"\b{re.escape(h.name.split('::')[-1])}\s*\(", body):
                        seen.add(h.open)
                        queue.append(h)
            if calls:
                out[r] = calls
        loose = [(f, line, rva) for f, sites in self.sites.items() for line, rva in sites
                 if rva and (f, line) not in used]
        return out, loose

    # --- tests and mutants -------------------------------------------------------------------------------------------
    def test_of(self, r):
        """(test file, rank 0 DIFF_TEST / 1 test block / 2 comment, [test names]) or (None, None, [])."""
        h = f"{r:#x}"
        best = None
        for t in self.tests:
            if not t["name"]:
                continue
            in_set = t["file"] in self.test_files
            rank = 0 if t["rva"] == h else 1 if h in t["mentions"] else 2 if (h in t["free"] and in_set) else None
            if rank is None:
                continue
            key = (rank, not in_set, self.test_files.index(t["file"]) if in_set else 0, t["file"])
            if best is None or key < best[0]:
                best = (key, t["file"], rank)
        if not best:
            return None, None, []
        f, rank = best[1], best[2]
        names = [t["name"] for t in self.tests if t["file"] == f and t["name"] and (t["rva"] == h or h in t["mentions"])]
        if not names:  # named in the file's comments: the tests whose name (or its last part) is on the same line
            mine = [t["name"] for t in self.tests if t["file"] == f and t["name"]]
            lines = [l for l in self.texts[f].splitlines() if h in l.lower()]
            names = [n for n in mine if any(re.search(rf"\b({re.escape(n)}|{re.escape(n.split('_')[-1])})\b", l)
                                            for l in lines)]
            if not names:  # a table of RVAs: the first test defined after it
                text = self.texts[f]
                at = {n: text.find("DIFF_TEST(" + n) for n in mine}
                for pos in (m.start() for m in re.finditer(re.escape(h), text.lower())):
                    after = sorted((p, n) for n, p in at.items() if p > pos)
                    if after and after[0][1] not in names:
                        names.append(after[0][1])
            names = names or mine
        return f, rank, names

    def mutant_notes(self, where):
        """{rva: summary} for mutants attributed to drafted functions, and the whole runs' summary."""
        per, whole = {}, []
        for i, log in enumerate(self.mutate_logs):
            results = parse_mutate_log(Path(log).read_text(encoding="utf-8", errors="replace"))
            whole += results
            if i >= len(self.mutants):
                continue
            ms = json.loads(Path(self.mutants[i]).read_text(encoding="utf-8"))
            for res in results:
                if res["index"] >= len(ms):
                    continue
                for f, qual in testselect.targets_of([ms[res["index"]]], self.root):
                    for r, (bf, b) in where.items():
                        if bf == f and qual and (b.qual == qual or split_name(b.qual) == split_name(qual)):
                            per.setdefault(r, []).append(res)
        return {r: mutant_summary(v) for r, v in per.items()}, mutant_summary(whole)

    # --- the draft ---------------------------------------------------------------------------------------------------
    def draft(self):
        """{rva: draft entry} (FIELDS order, notes only for new entries)."""
        effect = self.plan()
        where = self.entry_blocks()
        calls, loose = self.original_calls(where)
        for f, line, rva in loose:
            self.report.append(f"{f}:{line}: OriginalFunction {rva} is in no drafted function's body or helper: add it "
                               "to the entry whose code calls it")
        per_mut, whole = self.mutant_notes(where) if self.mutate_logs else ({}, "")
        hook_names = {r: n for n, r, _ in self.hooks}
        self.unsure = set()
        out = {}
        for r in sorted(self.rvas):
            old = self.existing.get(r, {}) if self.use_existing else {}
            test, rank, names = self.test_of(r)
            src = where[r][0] if r in where else old.get("src")
            if not src and r in effect:
                srcs = {self.hook_src.get(h.split("=")[0][6:]) for h in effect[r][1].split("&") if h}
                src = srcs.pop() if len(srcs) == 1 and None not in srcs else None
            if not src and len(self.src_files) == 1:
                src = self.src_files[0]
            if not test:
                test = old.get("test") or (self.test_files[0] if len(self.test_files) == 1 else "TODO")
            e = {"rva": f"{r:#x}", "name": self.name_of(r),
                 "status": old.get("status") or ("verified" if rank is not None else "reimplemented"),
                 "src": src or "TODO", "test": test}
            if not old:
                mut = per_mut.get(r) or (f"Mutants (whole run, not attributed: pass --mutants): {whole[9:]}"
                                         if whole else "")
                e["notes"] = " ".join(x for x in (f"{', '.join(names) or 'TODO test'}:", mut,
                                                  "TODO: what is specific to this function.") if x)
            if r in calls:
                e["original_calls"] = calls[r]
            if r in effect:
                opened, trap = effect[r]
                e["in_effect"] = "callers"
                e["callers_check"] = f"callers.py v{callers.VERSION} {date.today().isoformat()}"
                e["trap"] = trap or "TODO"
                if opened:
                    self.unsure.add(r)
                    self.report.append(f"{r:#x} {e['name'][:50]}: {len(opened)} open reference(s) (callers.py {r:#x}): "
                                       f"{', '.join(opened[:4])}")
            if r in hook_names and old.get("in_effect"):
                self.report.append(f"{r:#x}: hooked ({hook_names[r]}) but its entry says in_effect "
                                   f"{old['in_effect']!r}")
            out[r] = e
        return out


def _offset(text, line):
    i = 0
    for _ in range(line - 1):
        i = text.index("\n", i) + 1
    return i


def merge(lines, drafts, unsure=()):
    """Merges drafts ({rva: entry}) into a status file's lines. Returns (lines, added, updated {rva: [fields]},
    kept [(rva, field, existing, draft)]): new RVAs appended; on existing lines missing fields are added and in_effect /
    trap refreshed (callers_check with them) unless the RVA is in `unsure` (its check found open references here) or
    the draft says TODO; other differing fields are kept; notes and status never change."""
    lines = list(lines)
    if lines and not lines[-1].endswith("\n"):
        lines[-1] += "\n"
    here = {int(json.loads(l)["rva"], 16): i for i, l in enumerate(lines) if l.strip()}
    added, updated, kept = [], {}, []
    for r, d in sorted(drafts.items()):
        if r not in here:
            lines.append(json.dumps(d) + "\n")
            added.append(r)
            continue
        i = here[r]
        old = json.loads(lines[i])
        line, changed = lines[i], []
        sure = r not in unsure
        refresh = sure and any(k in d and d[k] != "TODO" and old.get(k) != d[k] for k in REFRESH)
        for k in ("src", "test", "original_calls", "in_effect", "callers_check", "trap"):
            if k not in d:
                if k == "original_calls" and old.get(k):
                    kept.append((r, k, old[k], None))
                continue
            if old.get(k) == d[k]:
                continue
            if k == "callers_check" and k in old and not refresh:
                continue  # same plan (or an open check): the date of the last check stays
            if k in old and (k not in REFRESH and k != "callers_check" or not sure or d[k] == "TODO"):
                kept.append((r, k, old[k], d[k]))
                continue
            line = traps.set_field(line, k, d[k])
            changed.append(k)
        if changed:
            lines[i] = line
            updated[r] = changed
    return lines, added, updated, kept


def record(args, out=sys.stdout, err=sys.stderr):
    """tools/status.py --record."""
    rec = Recorder(args.group, tests=args.test or (), srcs=args.src or (), add=[r for a in args.add or () for r in a.split(",") if r],
                   mutate_logs=args.mutate_log or (), mutants=args.mutants or (),
                   db=callers.Db(binary=False if args.no_binary else None), cls=getattr(args, "cls", None))
    drafts = rec.draft()
    path = tsdb.ROOT / "db/status" / f"{args.group}.jsonl"
    old = path.read_text(encoding="utf-8").splitlines(keepends=True) if path.exists() else []
    lines, added, updated, kept = merge(old, drafts, rec.unsure)
    for r in added:
        out.write(json.dumps(drafts[r]) + "\n")
    for r in updated:
        out.write(next(l for l in lines if l.strip() and int(json.loads(l)["rva"], 16) == r).rstrip("\n") + "\n")
    for line in rec.report:
        err.write(f"# {line}\n")
    for r, k, a, b in kept:
        err.write(f"# {r:#x}: {k} kept {json.dumps(a)} (draft {json.dumps(b)})\n")
    changes = "; ".join(f"{r:#x} " + ",".join(v) for r, v in list(updated.items())[:8])
    err.write(f"# {args.group}: {len(drafts)} drafted, {len(added)} new, {len(updated)} changed"
              f"{' (' + changes + ')' if changes else ''}, {len(kept)} differing fields kept\n")
    text = "".join(lines)
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8", newline="")
        err.write(f"# wrote the merged file to {args.out}\n")
    if args.write:
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        err.write(f"# wrote {_rel(path.relative_to(tsdb.ROOT))}: fill the TODOs in the new notes\n")
    return 0


def brief(args, out=sys.stdout):
    """tools/status.py --group G --brief: one line per entry, filtered."""
    path = tsdb.ROOT / "db/status" / f"{args.group}.jsonl"
    if not path.exists():
        out.write(f"no db/status/{args.group}.jsonl\n")
        return 1
    want_rva = {int(r, 16) for r in re.split(r"[,\s]+", args.rva or "") if r}
    statuses = {s for s in (args.status or "").split(",") if s}
    rows, counts = [], {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        e = json.loads(line)
        effect = e.get("in_effect", "hook")
        if want_rva and int(e["rva"], 16) not in want_rva:
            continue
        if statuses and e.get("status") not in statuses:
            continue
        if args.in_effect and effect != args.in_effect:
            continue
        if args.test_has and args.test_has not in (e.get("test") or ""):
            continue
        if args.name_has and args.name_has.lower() not in e.get("name", "").lower():
            continue
        if args.no_test and e.get("test"):
            continue
        counts[e.get("status")] = counts.get(e.get("status"), 0) + 1
        test = Path(e.get("test") or "-").name if not e.get("verify") == "match" else "match"
        rows.append(f"{e['rva']:<9} {e.get('status', '?'):<13} {effect:<7} {len(e.get('original_calls', [])):>2} "
                    f"{test:<28} {e.get('name', '')[:70]}")
    out.write("rva       status        effect  oc test                         name\n")
    for r in rows:
        out.write(r + "\n")
    out.write(f"{len(rows)} entries: {', '.join(f'{v} {k}' for k, v in sorted(counts.items()))}\n")
    return 0
