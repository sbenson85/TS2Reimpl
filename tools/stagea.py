"""Drafts the status entries of a stage A conversion (a class in the standalone shape, docs/standalone-shape.md): every
vtable slot body, the adjustor thunks, the constructors / destructors (the code storing a vtable) and the creators
calling them, each with what it becomes, then runs the callers check (tools/callers.py) on the plan and prints what is
still open. Drafts only: the lane fills the verification fields (status, test, notes); nothing is written without
--write.

  tools/py -I tools/stagea.py --class cGZSndMgr --hook 0xb22333=cGZSndMgr_Create
  tools/py -I tools/stagea.py --class cGZSndMgr --class cGZSndEaRenderer --hook 0xb22333 --hook 0xb12be6 --drafts
  tools/py -I tools/stagea.py --class cGZSndMgr --hook 0xb22333 --write [--into <group>]

  --class    a class in db/classes.jsonl (exact name, as tools/classinfo.py prints it); repeatable.
  --group    also every RVA of db/status/<group>.jsonl (members that no vtable holds).
  --hook     RVA[=Name]: a function that stays hooked in the plan (the factory, or the constructor's adapter); the
             drafts' traps rest on it. Name defaults to the hook's name in src/hook/tables, else <Class>_Create. Every
             other drafted function is planned as "in effect through callers": a hook it has now is dropped in the plan
             (the line says so: remove its row).
  --drafts   print the draft entries (JSONL) after the table.
  --write    add the drafts to db/status/<into>.jsonl (default the first --class): new RVAs appended, entries already
             in that file get only the "in_effect" / "callers_check" / "trap" fields (tools/traps.py's editor; other
             fields kept), entries in another group's file are left (listed).
  --no-binary  skip the scan of our build (callers.py --no-binary).

Kinds: slot (a vtable slot body: in effect through callers), thunk (`sub ecx, N; jmp` adjustor: an entry with no hook,
no trap; it exists only in our vtables once the class is ours), stores-vtable (constructor / destructor: through
callers), creator (calls a constructor: a creation site; hooked when it is a --hook, else it must be ours or
reported), member (from --group); no entry: library code, folded `shared_vf_*` bodies (entered once, in the group of
the class that defines them: docs/verification.md), leaves under 5 bytes, bodies named for another class (its group).
The check column: `ok` (every reference covered under the plan), or `OPEN n` with the first open references (sites as
callers.py prints them). The trap is what status.py --check will expect (callers.py Checker.hook_deps).
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import callers  # noqa: E402
import traps  # noqa: E402
import tsdb  # noqa: E402

CALLER_FIELDS = ("in_effect", "callers_check", "trap")


def adjustor_target(code):
    """The jump target offset decoder for an adjustor thunk's bytes (`sub ecx, imm8/imm32; jmp rel32`, or `add ecx`):
    (this-adjustment, target relative to the thunk) or None."""
    if len(code) >= 8 and code[0] == 0x83 and code[1] in (0xE9, 0xC1) and code[3] == 0xE9:
        adj = code[2] if code[1] == 0xE9 else -code[2]
        return adj, 8 + int.from_bytes(code[4:8], "little", signed=True)
    if len(code) >= 11 and code[0] == 0x81 and code[1] in (0xE9, 0xC1) and code[6] == 0xE9:
        adj = int.from_bytes(code[2:6], "little", signed=True)
        return (adj if code[1] == 0xE9 else -adj), 11 + int.from_bytes(code[7:11], "little", signed=True)
    return None


def owner_class(name):
    """'ns::cX::Foo (bar)' -> 'ns::cX' (the scope before the member), None for unscoped names."""
    base = re.sub(r"\(.*$", "", name.split(" (")[0]).strip()
    return base.rsplit("::", 1)[0] if "::" in base else None


def same_class(owner, classes):
    if owner is None:
        return True
    bare = owner.split("::")[-1]
    return any(bare == c.split("::")[-1] for c in classes)


class Plan:
    """The functions of a stage A conversion, their kinds and drafts, and the callers check under the plan."""

    def __init__(self, image, db, classes, hooks, group_rvas=(), library=frozenset()):
        self.image, self.db, self.classes = image, db, list(classes)
        self.rows = []  # dicts: rva, kind, name, size, draft (None: no entry), why
        self.hooks = dict(hooks)  # rva -> hook name
        seen = set()

        def add(rva, kind, why="", vt=None):
            if rva in seen:
                return
            seen.add(rva)
            f = db.funcs.get(rva, {})
            name = db.name(rva)
            row = {"rva": rva, "kind": kind, "name": name, "size": f.get("size", 0), "draft": None, "why": why}
            if kind == "creator" and rva in db.hooked and rva not in self.hooks:
                self.hooks[rva] = db.hook_names.get(rva, f"{rva:#x}")  # a creator hooked now: kept in the plan
            if rva in self.hooks:
                row.update(kind="hooked", draft="hook", why=f"stays hooked ({self.hooks[rva]}): the plan's hook")
            elif f"{rva:#x}" in library:
                row["why"] = "library code: no entry"
            elif name.startswith("shared_vf_") or db.names.get(rva, "").startswith("shared_vf_"):
                row.update(kind="folded", why="folded shared_vf_* body: no entry here (entered once in the defining "
                                               "class's group; its thunks need none)")
            elif 0 < row["size"] < 5:
                row["why"] = "under 5 bytes: no entry (no hook or trap fits)"
            elif kind == "slot" and adjustor_target(image.bytes_at(rva, 11)):
                adj, off = adjustor_target(image.bytes_at(rva, 11))
                if db.status.get(rva, {}).get("in_effect") == "callers":  # recorded that way already: kept
                    row.update(kind="thunk", draft="callers", why=f"adjustor thunk to {rva + off:#x}")
                else:
                    row.update(kind="thunk", draft="thunk",
                               why=f"adjustor thunk (this {'-' if adj >= 0 else '+'}= {abs(adj):#x}, to {rva + off:#x}): "
                                   "no hook, no trap")
            elif not same_class(owner_class(name), self.classes) and kind in ("slot", "member"):
                row.update(kind="other", why=f"named for {owner_class(name)}: its group's entry, not drafted here")
            elif kind == "creator":
                row["why"] = (why + "; a creation site if that is a constructor: hook it (--hook, the factory) or make "
                              "what reaches it ours, else the game's vtable stays live")
            else:
                row["draft"] = "callers"
            row["vt"] = vt
            self.rows.append(row)

        by_name = {c["name"]: c for c in tsdb.jsonl("classes.jsonl")} if not hasattr(db, "classes") else db.classes
        vtables = []
        for c in self.classes:
            if c not in by_name:
                raise SystemExit(f"stagea: {c} is not in db/classes.jsonl (exact name, as tools/classinfo.py prints it)")
            for vt in by_name[c].get("vftables", []):
                vtables.append((int(vt["rva"], 16), vt.get("offset", 0), [int(s, 16) for s in vt.get("entries", [])]))
        self.vtables = vtables
        for start, off, entries in vtables:
            for i, rva in enumerate(entries):
                add(rva, "slot", vt=f"{start:#x}+{off}[{i}]")
        for r in list(self.rows):  # a body only a thunk reaches (a deleting destructor behind the +4 face's thunk)
            if r["kind"] == "thunk":
                adj, off = adjustor_target(image.bytes_at(r["rva"], 11))
                add(r["rva"] + off, "slot", vt=f"behind thunk {r['rva']:#x}")
        for r in group_rvas:
            add(r, "member")
        # The code storing each vtable: constructors and destructors (and subclasses' inlined bases).
        stores = []
        for start, _, _ in vtables:
            for site in image.relocations().get(start, []):
                func = db.function_at(site)
                if func is None:
                    self.rows.append({"rva": site, "kind": "stores-vtable", "name": "outside any known function",
                                      "size": 0, "draft": None, "vt": None,
                                      "why": f"vtable {start:#x} stored outside a known function (missedfuncs.py --add?)"})
                    continue
                stores.append(func)
                add(func, "stores-vtable", f"stores vtable {start:#x}")
        for r in self.rows:
            if r["kind"] == "stores-vtable" and r["draft"] == "callers" and not same_class(owner_class(r["name"]), self.classes):
                r.update(draft=None, why=r["why"] + f"; named for {owner_class(r['name'])}: a creation site outside "
                                                    "the class (replace it or report it)")
        # Creators: whoever calls the vtable-storing code and doesn't store a vtable itself.
        storing = {r["rva"] for r in self.rows if r["kind"] in ("stores-vtable", "slot", "hooked")}
        for func in stores:
            for site, what, owner, kind in callers.Checker(image, db).references(func):
                if kind == "code" and owner is not None and owner not in storing and owner not in self.hooks:
                    add(owner, "creator", f"calls {func:#x} ({what.split(' in ')[0]})")
                elif kind == "code" and owner is None:
                    self.rows.append({"rva": int(site.split(":")[1], 16), "kind": "creator", "name": what, "size": 0,
                                      "draft": None, "vt": None, "why": f"calls {func:#x} from outside any known "
                                                                        "function: missedfuncs.py --add the creator"})
        for rva in self.hooks:
            add(rva, "hooked")

    def checker(self):
        """A Checker on the plan: the --hook functions hooked, every other drafted function's hook dropped and its entry
        "in_effect": "callers"."""
        db = self.db
        drafted = {r["rva"] for r in self.rows if r["draft"] == "callers"}
        thunks = {r["rva"] for r in self.rows if r["draft"] == "thunk"}
        self.dropped = sorted(r for r in drafted | thunks if r in db.hooked)
        db.hooked = (set(db.hooked) - drafted - thunks) | set(self.hooks)
        names = dict(db.hook_names)
        names.update(self.hooks)
        db.hook_names = names
        status = dict(db.status)
        for r in drafted:
            e = dict(status.get(r, {}))
            if e.get("status") not in tsdb.OURS:
                e["status"] = "reimplemented"
            e["in_effect"] = "callers"
            status[r] = e
        db.status = status  # thunks drafted "thunk" have no callers entry: original code the dead vtables alone hold
        return callers.Checker(self.image, db)

    def check(self):
        checker = self.checker()
        for r in self.rows:
            if r["draft"] == "thunk":  # no trap, but anything beyond the dead vtables reaching it must be ours too
                v = checker.verdict(r["rva"])
                r["open"] = [x for x in v["references"] if not x["covered"]] if v else []
            if r["draft"] != "callers":
                continue
            v = checker.verdict(r["rva"])
            r["open"] = [] if v is None else [x for x in v["references"] if not x["covered"]]
            if v is None:
                r["open"] = [{"site": "cycle", "what": "its check is part of a cycle", "why": ""}]
            deps = checker.trap_hooks(r["rva"])
            r["trap"] = "&".join(f"Hooks.{h}=1" for h in deps) or None
        return self.rows


def src_guess(classes):
    """The src/ file a class's code is in (its own .cpp / .size.cpp), or a placeholder."""
    for c in classes:
        bare = c.split("::")[-1]
        for p in sorted((tsdb.ROOT / "src").rglob(f"{bare}*.cpp")):
            if p.name.split(".")[0] == bare:
                return p.relative_to(tsdb.ROOT).as_posix()
    return "TODO"


def draft_entry(row, existing, src):
    """The status line for a row: an existing entry with the plan's fields, or a new one to fill."""
    if existing:
        e = dict(existing)
    else:
        e = {"rva": f"{row['rva']:#x}", "name": row["name"], "status": "reimplemented", "src": src, "test": "TODO",
             "notes": f"TODO (tools/stagea.py draft, {row['kind']}): the evidence, mutants, hook or no hook."}
        if row["draft"] == "thunk":
            e["notes"] = ("TODO (tools/stagea.py draft): compiler-made adjustor thunk that exists only in our vtables: "
                          f"unhooked ({row['why']}); the test calls it through our vtable and names this RVA.")
    if row["draft"] == "callers":
        e["in_effect"] = "callers"
        e["callers_check"] = callers.check_version()
        e["trap"] = row.get("trap") or "TODO"
    return e


def write(group_path, drafts, other_files):
    """Adds drafts to a status file: new RVAs appended, existing lines of this file get the callers fields only.
    Returns (added, updated, left)."""
    group_path = Path(group_path)
    lines = group_path.read_text(encoding="utf-8").splitlines(keepends=True) if group_path.exists() else []
    if lines and not lines[-1].endswith("\n"):
        lines[-1] += "\n"
    here = {json.loads(l)["rva"]: i for i, l in enumerate(lines) if l.strip()}
    added, updated, left = [], [], []
    for d in drafts:
        if d["rva"] in here:
            i = here[d["rva"]]
            line = lines[i]
            for k in CALLER_FIELDS:
                if k in d and json.loads(line).get(k) != d[k]:
                    line = traps.set_field(line, k, d[k])
            if line != lines[i]:
                lines[i] = line
                updated.append(d["rva"])
        elif d["rva"] in other_files:
            left.append((d["rva"], other_files[d["rva"]]))
        else:
            lines.append(json.dumps(d) + "\n")
            added.append(d["rva"])
    with open(group_path, "w", encoding="utf-8", newline="") as f:
        f.write("".join(lines))
    return added, updated, left


def parse_hook(text, classes, db):
    rva, _, name = text.partition("=")
    rva = int(rva, 16)
    if not name:
        name = db.hook_names.get(rva) or f"{classes[0].split('::')[-1]}_Create"
    return rva, name


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--class", dest="classes", action="append", default=[], required=True)
    ap.add_argument("--group", action="append", default=[])
    ap.add_argument("--hook", action="append", default=[], metavar="RVA[=Name]")
    ap.add_argument("--drafts", action="store_true")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--into")
    ap.add_argument("--no-binary", action="store_true")
    ap.add_argument("-v", action="store_true", help="also list the folded, library and other classes' slots one by one")
    a = ap.parse_args(argv)
    image = callers.Image()
    db = callers.Db(binary=False if a.no_binary else None)
    if db.binary_note:
        print(f"warning: {db.binary_note}", file=sys.stderr)
    hooks = dict(parse_hook(h, a.classes, db) for h in a.hook)
    group_rvas = []
    for g in a.group:
        p = tsdb.ROOT / "db/status" / (g if g.endswith(".jsonl") else g + ".jsonl")
        group_rvas += [int(json.loads(l)["rva"], 16) for l in open(p, encoding="utf-8") if l.strip()]
    status_files = {e["rva"]: f for f, e in tsdb.status_entries()}
    status_now = {int(e["rva"], 16): dict(e) for _, e in tsdb.status_entries()}
    plan = Plan(image, db, a.classes, hooks, group_rvas, tsdb.library_rvas())
    rows = plan.check()
    hooks = plan.hooks  # the --hook ones and creators hooked now
    if not hooks:
        print("warning: no --hook: nothing in the plan keeps the game's vtables off objects (name the factory or the "
              "constructor hook), so slot verdicts stay open", file=sys.stderr)
    into = a.into or a.classes[0].split("::")[-1]
    src = src_guess(a.classes)
    print(f"{', '.join(a.classes)}: {len(plan.vtables)} vtables; plan: hooked "
          f"{', '.join(f'{r:#x} {n}' for r, n in hooks.items()) or 'nothing'}; everything drafted 'callers' unhooked")
    drafts, open_entries, traps_seen, quiet = [], 0, {}, {}
    for r in rows:
        where = status_files.get(f"{r['rva']:#x}", "")
        state = status_now.get(r["rva"], {}).get("status", "-")
        head = f"{r['kind']:13} {r['rva']:#9x} {r['size']:5} {r['name'][:44]:44} {state:13}"
        if r["draft"] == "callers":
            op = r.get("open", [])
            open_entries += bool(op)
            traps_seen[r.get("trap")] = traps_seen.get(r.get("trap"), 0) + 1
            check = "ok" if not op else f"OPEN {len(op)}"
            print(f"{head} callers  {check:7} trap {r.get('trap') or 'NONE (rests on no hook)'}"
                  + (f"  [now hooked: drop its row]" if r["rva"] in plan.dropped else "")
                  + (f"  [in {where}]" if where and where != into + ".jsonl" else ""))
            for o in op[:4]:
                print(f"{'':16}OPEN {o['site']} {o['what'][:70]}: {o['why'][:90]}")
            if len(op) > 4:
                print(f"{'':16}... {len(op) - 4} more (tools/callers.py {r['rva']:#x})")
        elif r["draft"] in ("thunk", "hook"):
            op = r.get("open", [])
            open_entries += bool(op)
            print(f"{head} {r['draft']:8} {'OPEN ' + str(len(op)) + ' ' if op else ''}{r['why']}"
                  + (f"  [now hooked: drop its row]" if r["rva"] in plan.dropped else "")
                  + (f"  [in {where}]" if where else ""))
            for o in op[:4]:
                print(f"{'':16}OPEN {o['site']} {o['what'][:70]}: {o['why'][:90]}")
        elif a.v or r["kind"] not in ("folded", "other", "slot"):
            print(f"{head} -        {r['why']}")
        else:
            quiet.setdefault(r["kind"] if r["kind"] != "slot" else "library / small", []).append(r)
        if r["draft"] in ("callers", "thunk", "hook"):
            drafts.append(draft_entry(r, status_now.get(r["rva"]), src))
    for kind, rs in quiet.items():
        owners = sorted({owner_class(r["name"]) or "" for r in rs} - {""}) if kind == "other" else []
        print(f"no entry here: {len(rs)} {kind}" + (f" ({', '.join(owners)})" if owners else "")
              + ": " + ", ".join(f"{r['rva']:#x}" for r in rs[:12]) + (" ..." if len(rs) > 12 else "")
              + " (-v lists them)")
    n_callers = sum(r["draft"] == "callers" for r in rows)
    print(f"summary: {len(drafts)} drafts ({n_callers} through callers, {sum(r['draft'] == 'thunk' for r in rows)} "
          f"thunks), {open_entries} with open references; traps: "
          + "; ".join(f"{t or 'none'} x{n}" for t, n in traps_seen.items()))
    if plan.dropped:
        print(f"hooks the plan drops (remove their rows from src/hook/tables): {', '.join(f'{r:#x}' for r in plan.dropped)}")
    if a.drafts:
        for d in drafts:
            print(json.dumps(d))
    if a.write:
        path = tsdb.ROOT / "db/status" / f"{into}.jsonl"
        others = {k: v for k, v in status_files.items() if v != path.name}
        added, updated, left = write(path, drafts, others)
        print(f"{path}: {len(added)} added, {len(updated)} updated (callers fields only)"
              + (f"; left in other groups: {', '.join(f'{r} ({f})' for r, f in left)}" if left else ""))
        print("next: fill status / test / notes (the TODOs), then tools/py -I tools/callers.py --write-traps and "
              "tools/py -I tools/status.py --check")
    return 1 if open_entries else 0


if __name__ == "__main__":
    sys.exit(main())
