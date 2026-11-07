"""Edits the "trap" field of status entries replaced through their callers ("in_effect": "callers"), line by line and
JSON-aware: only the trap value changes (every other byte of the line, the key order and the notes stay as they are).

  tools/py -I tools/traps.py --group cTSAudioEvent --add cTSAudioListener_Create [--where cTSAudioEvent_ConstructDefault]
  tools/py -I tools/traps.py --group cTSAudioEvent --remove cTSAudioListener_Create
  tools/py -I tools/traps.py --sync [--group G ...]      # every Hooks. trap = exactly the hooks its check rests on
  options: --rva 0x1,0x2 (only these entries)  --dry-run (print, don't write)  --write-traps (regenerate
           src/hook/trap_table.cpp afterwards, as callers.py --write-traps)  --force (--add a hook src/hook/tables
           doesn't have yet)

--add / --remove: one hook term (`X`, `Hooks.X` or `Hooks.X=1`) added to or removed from the trap of every selected
  entry (all "in_effect": "callers" entries of the --group files, narrowed by --rva and by --where: entries whose trap
  already names that hook; repeatable, all must hold). The Hooks. terms are kept sorted by name after any other terms
  (Startup.*), as callers.py --vtable-only prints them. An entry that already has (or lacks) the term is left alone.
--sync: for every selected entry whose trap has Hooks. terms, sets them to the hooks status.py --check says the check
  rests on (callers.py Checker.trap_hooks, the function status.py --check uses, on the same Db: build ts2host
  first, --sync refuses a missing or stale build; other terms kept). Entries resting on no hook are reported and left
  alone. Without --group: every status file.
Afterwards: tools/py -I tools/callers.py --write-traps (or pass --write-traps) and tools/py -I tools/status.py --check.
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import callers  # noqa: E402
import tsdb  # noqa: E402

_DEC = json.JSONDecoder()
_WS = re.compile(r"[ \t\r\n]*")


def set_field(line, key, value):
    """`line` (one JSON object) with its top-level `key` set to `value`: the old value's text replaced in place, every
    other byte kept; a missing key is appended before the closing brace (", " / ": " separators). A key inside a string
    (a note quoting `"trap": "..."`) is never touched: the object is walked token by token."""
    body = line.rstrip("\r\n")
    tail = line[len(body):]
    i = _WS.match(body, 0).end()
    if body[i:i + 1] != "{":
        raise ValueError("not a JSON object")
    i += 1
    first, out = True, None
    while out is None:
        i = _WS.match(body, i).end()
        if body[i:i + 1] == "}":  # not found: append it
            out = body[:i].rstrip() + ("" if first else ", ") + f"{json.dumps(key)}: {json.dumps(value)}" + body[i:] + tail
            break
        if not first:
            if body[i:i + 1] != ",":
                raise ValueError(f"expected , at {i}")
            i = _WS.match(body, i + 1).end()
        if body[i:i + 1] != '"':
            raise ValueError(f"expected a key at {i}")
        k, i = json.decoder.scanstring(body, i + 1)
        i = _WS.match(body, i).end()
        if body[i:i + 1] != ":":
            raise ValueError(f"expected : at {i}")
        start = _WS.match(body, i + 1).end()
        _, end = _DEC.raw_decode(body, start)
        if k == key:
            out = body[:start] + json.dumps(value) + body[end:] + tail
        i, first = end, False
    new, old = json.loads(out), json.loads(line)
    old[key] = value
    if new != old or list(new) != list(old):
        raise ValueError("edit changed more than the field")  # a safeguard; never expected
    return out


def hook_term(text):
    """'X' / 'Hooks.X' / 'Hooks.X=1' -> 'X'."""
    m = re.fullmatch(r"(?:Hooks\.)?([A-Za-z0-9_]+)(?:=1)?", text.strip())
    if not m:
        raise SystemExit(f"traps: {text!r} is not a hook name (X, Hooks.X or Hooks.X=1)")
    return m.group(1)


def split(trap):
    """(other terms' texts in order, hook names) of a trap text."""
    parts = [p for p in (trap or "").split("&") if p]
    hooks = [p[len("Hooks."):].split("=")[0] for p in parts if p.startswith("Hooks.")]
    return [p for p in parts if not p.startswith("Hooks.")], hooks


def join(others, hooks):
    """The trap text: other terms first, in their order, then the Hooks. terms sorted by name, each =1."""
    return "&".join(list(others) + [f"Hooks.{h}=1" for h in sorted(set(hooks))])


def add_hook(trap, hook):
    others, hooks = split(trap)
    return trap if hook in hooks else join(others, hooks + [hook])


def remove_hook(trap, hook):
    others, hooks = split(trap)
    if hook not in hooks:
        return trap
    rest = [h for h in hooks if h != hook]
    return join(others, rest) if others or rest else None  # None: nothing left to arm on


def sync_trap(trap, dep_names):
    """The trap with its Hooks. terms set to exactly `dep_names` (None when it has no Hooks. terms or none to set)."""
    others, hooks = split(trap)
    if not hooks or not dep_names:
        return None
    return join(others, dep_names)


def edit_file(path, pick, change, dry_run=False):
    """Applies change(entry) -> new trap text (or the same / None: no change) to the lines pick(entry) selects.
    Returns [(rva, name, old trap, new trap)]; writes the file only if something changed and not dry_run."""
    path = Path(path)
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    out, changes = [], []
    for line in lines:
        if not line.strip():
            out.append(line)
            continue
        e = json.loads(line)
        if e.get("in_effect") == "callers" and pick(e):
            new = change(e)
            if new and new != e.get("trap"):
                if not callers.parse_trap(new):
                    raise SystemExit(f"traps: {path.name} {e['rva']}: {new!r} is not a valid trap")
                line = set_field(line, "trap", new)
                changes.append((e["rva"], e.get("name", ""), e.get("trap"), new))
        out.append(line)
    if changes and not dry_run:
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write("".join(out))
    return changes


def hook_names():
    """Every hook name in src/hook/tables (the host's [Hooks] keys)."""
    names = set()
    for p in (tsdb.ROOT / "src/hook/tables").glob("*.cpp"):
        names.update(n for n, _ in callers.HOOK_ROW.findall(p.read_text(encoding="utf-8", errors="replace")))
    return names


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--group", action="append", default=[], help="status file stem (db/status/<group>.jsonl)")
    what = ap.add_mutually_exclusive_group(required=True)
    what.add_argument("--add", metavar="HOOK")
    what.add_argument("--remove", metavar="HOOK")
    what.add_argument("--sync", action="store_true")
    ap.add_argument("--rva", default="", help="comma-separated RVAs: only these entries")
    ap.add_argument("--where", action="append", default=[], metavar="HOOK", help="only entries whose trap names it")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--write-traps", action="store_true", help="regenerate src/hook/trap_table.cpp afterwards")
    ap.add_argument("--force", action="store_true", help="--add a hook name src/hook/tables doesn't have")
    a = ap.parse_args(argv)
    if (a.add or a.remove) and not a.group:
        ap.error("--add / --remove need --group (the status files to edit)")
    files = [tsdb.ROOT / "db/status" / (g if g.endswith(".jsonl") else g + ".jsonl") for g in a.group]
    for f in files:
        if not f.exists():
            ap.error(f"no {f}")
    if not files:
        files = sorted((tsdb.ROOT / "db/status").glob("*.jsonl"))
    rvas = {int(r, 16) for r in a.rva.split(",") if r.strip()}
    where = [hook_term(w) for w in a.where]

    def pick(e):
        return (not rvas or int(e["rva"], 16) in rvas) and all(w in split(e.get("trap"))[1] for w in where)

    skipped = []
    if a.add:
        hook = hook_term(a.add)
        if hook not in hook_names() and not a.force:
            sys.exit(f"traps: no hook named {hook} in src/hook/tables (merge the lane that adds it first, or --force)")
        change = lambda e: add_hook(e.get("trap"), hook)  # noqa: E731
    elif a.remove:
        hook = hook_term(a.remove)

        def change(e):
            new = remove_hook(e.get("trap"), hook)
            if new is None and hook in split(e.get("trap"))[1]:
                skipped.append(f"{e['rva']}: {hook} is its only term (a trap needs one): left as is")
            return new
    else:
        # The same Db and Checker status.py --check builds (callers.check_status): the hooks depend on which vtables our
        # build holds (a vtable it holds stays live, so a hook reached through it is the cut), so a stale build would give
        # other traps than the check expects.
        image, db = callers.Image(), callers.Db()
        if db.binary_note:
            sys.exit(f"traps: {db.binary_note} (status.py --check would disagree with what --sync writes)")
        checker = callers.Checker(image, db)

        def change(e):
            others, hooks = split(e.get("trap"))
            if not hooks:
                return None
            deps = checker.trap_hooks(int(e["rva"], 16))
            if not deps:
                skipped.append(f"{e['rva']} {e.get('name', '')[:50]}: its check rests on no hook (trap {e['trap']} "
                               "left as is; callers.py " + e["rva"] + ")")
            return sync_trap(e.get("trap"), deps)
    total = 0
    for f in files:
        for rva, name, old, new in edit_file(f, pick, change, a.dry_run):
            total += 1
            print(f"{f.stem}: {rva} {name[:50]}: {old} -> {new}")
    for s in skipped:
        print(f"note: {s}")
    print(f"{total} trap(s) {'would change' if a.dry_run else 'changed'}")
    if a.write_traps and total and not a.dry_run:
        db = callers.Db(binary=False)
        image = callers.Image()
        path = tsdb.ROOT / callers.TRAP_TABLE
        text = callers.trap_table_text(callers.trap_rows(db, image))
        if not path.exists() or path.read_text(encoding="utf-8") != text:
            path.write_text(text, encoding="utf-8", newline="\n")
        print(f"{path}: regenerated; now tools/py -I tools/status.py --check")
    elif total and not a.dry_run:
        print("next: tools/py -I tools/callers.py --write-traps, then tools/py -I tools/status.py --check")
    return 0


if __name__ == "__main__":
    sys.exit(main())
