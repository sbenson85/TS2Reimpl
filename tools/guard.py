"""Pre-merge checks for a branch made by a parallel agent (run from the main checkout).

Usage: python -I tools/guard.py <branch> [--base main] [--tooling | --paths tools/,docs/lane/test.md] [--no-matchcheck]
Fails (exit 1) when the branch:
  - touches anything outside its lanes: new/changed files under src/ (not src/hook/*.cpp|h or
    src/game/), src/hook/tables/, tests/, db/status/; changes to files that already exist on the
    base under tests/ or src/hook/tables/ are flagged for a closer look rather than refused
  - edits standalone/owned_statics.cpp or owned_graphics.cpp beyond adding TS2_OWNED_STATIC definitions and
    /alternatename:_ts2_rva_<hex> aliases for RVAs its own changed src/ files own (those additions are a warning)
  - edits the harness, tools, build files, CLAUDE.md or docs, unless the task gave it those paths: a tooling lane is
    run with `--tooling` (tools/, docs/, harness/) or `--paths a/,b/c.md` (prefixes or files, comma-separated,
    repeatable). Changes under them are then listed as warnings to read; everything else outside the lanes stays an
    error. A branch whose commits carry `Lane-type: tooling` (or `Lane-paths: ...`) gets a reminder of the flag when
    it is run without one; the trailer itself grants nothing, on purpose: the lane writes it, so honouring it would let
    a lane turn its own out-of-lane errors into warnings. Only the coordinator grants paths, from the task it gave.
  - changes the source of a function recorded `"verify": "match"` (its `src` files, their header or the group's hook
    table) and that function no longer matches: tools/matchcheck.py is rerun on them in the branch's worktree (the
    lane's build) and anything but a verifiable MATCH is an error. No worktree for the branch: a warning with the
    command to run after merging. `--no-matchcheck` skips it (say so in the merge notes).
  - has _emit, or calls original code from src/ other than through OriginalFunction<Fn, 0xRVA>()
    (src/game/original.h): ctx.Original / Original< / GameAddress used as a function
  - breaks the original-callee rules (tools/origcalls.py) in its own files: undeclared or stale
    original_calls, library callees, wrapping its own original
  - declares, in a changed src/ file, a stack-argument count the original's `ret N` disagrees with (tools/arity.py:
    OriginalFunction forwarders, declarations whose comment names their original, hook replacements; lane JI's ten-
    for-eleven slot 23 crashed Create-a-Sim), or passes a RawSlot / VirtualCall / VirtualSlot call other bytes than the
    receiver's slot body pops (tools/slotarity.py; lane KS's nested-call split crashed lot load). A mismatch already at
    the merge base is a warning; slot calls the check can't resolve, on lines the branch adds or changes, are listed
    in a warning the lane must answer.
    A declaration in a changed file declared void whose original leaves a value in eax (arity's "return" check:
    lanes OR and OX crashed this way) is a warning. An OriginalFunction declared with a convention that leaves ecx
    unset (stdcall, cdecl, fastcall without a register argument) whose original reads ecx first is an error (a
    thiscall called as a function: lane RD's 0xacadd0 crashed the game). A bool (8-, 16-bit) declaration whose
    original leaves a full-width value in eax (arity's "width" check: lane TN's texture slot 7) is a warning.
    `--no-arity` skips them all.
  - adds a verified entry whose test file doesn't mention its RVA
  - adds an "in_effect": "callers" entry without its "callers_check" and "trap" (such entries need no hook)
  - removes a manual entry from db/missed-functions.jsonl (adding or changing one, `tools/missedfuncs.py --add`, is
    a warning to read, as are the scan entries that file's regeneration changes)
  - leaves a union-merged JSONL file (.gitattributes `merge=union`: db/annotations.jsonl, db/methods/*.jsonl, notes'
    lane records) broken in its own version: a line that isn't JSON, a conflict marker, an exact duplicate line, two
    entries with one key that disagree (tools/annotations_check.py). The same problems that only the union merge with
    the base would create are warnings: fix them after merging and rerun tools/annotations_check.py
Warnings (for the reviewer): inline asm (ABI shims only), GameAddress uses (fine for data/vtables, never for calling code),
each OriginalFunction callee (a binary dependency: is it worth replacing instead?), long runs of hex byte
literals (pasted bytes?), files over 1500 lines, new "in_effect": "callers" entries (one line per status file with a
count; `--callers-detail` for one per entry), and test-weakening candidates in tests/ files that exist on the base
(tools/testweaken.py: a removed test, fewer checks, a lower loop bound or count argument, a new skip / mask / zeroed
field; the lane answers each in its report, as with unknown slot calls), and tests that patch, or plant the private
statics of, a function the branch newly hooks or marks ours on the game's side only, without a both-sides
shape::StandIn (tools/guard_patches.py: once hooked, the game's side reaches ours and the stand-in no longer covers
it; one line per test file).
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import annotations_check  # noqa: E402
import arity  # noqa: E402
import callers  # noqa: E402
import gitblob  # noqa: E402
import guard_patches  # noqa: E402
import origcalls  # noqa: E402
import slotcalls  # noqa: E402
import testweaken  # noqa: E402
import tsdb  # noqa: E402

REFUSED_PREFIXES = ("harness/", "tools/", "docs/", "CMakeLists.txt", "CLAUDE.md", "README.md", "NOTES.md", "notes/",
                    "db/classes", "db/functions", "db/names", "db/library", "db/families", "db/community",
                    "db/interface", "target/", ".gitignore", "requirements.txt", "src/game/")
CORE_HOOK_FILES = ("src/hook/hooks.cpp", "src/hook/hooks.h", "src/hook/dllmain.cpp", "src/hook/log.h",
                   "src/hook/traps.cpp", "src/hook/traps.h")


def git(*a):
    if len(a) == 2 and a[0] == "show" and ":" in a[1]:  # thousands of these in a run: tools/gitblob.py
        return gitblob.show(*a[1].split(":", 1))
    return subprocess.run(["git", *a], capture_output=True, text=True, check=True, encoding="utf-8").stdout


TOOLING_PATHS = ("tools/", "docs/", "harness/")
SLOT_UNKNOWNS_SHOWN = 25  # slot calls with no verdict listed in the guard's warning (the rest: arity.py -v)


def allowed_by(path, allowed):
    """The --paths entry that covers `path` (a prefix ending in / or an exact file), or None."""
    return next((a for a in allowed if path == a or (a.endswith("/") and path.startswith(a))), None)


def declared_lane(mb, branch):
    """`Lane-type:` / `Lane-paths:` trailers in the branch's commit messages (a reminder only; they grant nothing)."""
    text = git("log", "--format=%B", f"{mb}..{branch}")
    kinds = sorted({m.group(1).strip() for m in re.finditer(r"(?im)^Lane-type:\s*(\S+)", text)})
    paths = sorted({p.strip() for m in re.finditer(r"(?im)^Lane-paths:\s*(.+)$", text) for p in m.group(1).split(",")
                    if p.strip()})
    return kinds, paths


def worktree_of(branch):
    """The checkout that has `branch` checked out (a lane's worktree), or None."""
    path = None
    for line in git("worktree", "list", "--porcelain").splitlines():
        if line.startswith("worktree "):
            path = line[len("worktree "):]
        elif line.strip() == f"branch refs/heads/{branch}":
            return Path(path)
    return None


def match_entries_touched(branch, mb, changed):
    """Status entries recorded `"verify": "match"` on the branch whose source the branch changes (a `src` file, the
    header or other files of the same stem, or the group's hook table) or that the branch adds. -> {rva: why}"""
    stems = {}
    for p in changed:
        if p.startswith("src/"):
            name = Path(p).name.split(".")[0]
            stems.setdefault((str(Path(p).parent).replace("\\", "/"), name), p)
    out = {}
    for name in git("ls-tree", "--name-only", branch, "db/status/").split():
        try:
            old = set(git("show", f"{mb}:{name}").splitlines())
        except subprocess.CalledProcessError:
            old = set()
        table = f"src/hook/tables/{Path(name).stem}.cpp"
        for line in git("show", f"{branch}:{name}").splitlines():
            if not line.strip():
                continue
            e = json.loads(line)
            if e.get("status") != "verified" or e.get("verify") != "match":
                continue
            why = None
            for s in re.split(r"[,;]", e.get("src") or ""):
                s = s.strip().replace("\\", "/")
                if not s:
                    continue
                key = (str(Path(s).parent).replace("\\", "/"), Path(s).name.split(".")[0])
                if key in stems:
                    why = f"{stems[key]} changed"
                    break
            if why is None and table in changed:
                why = f"{table} changed"
            if why is None and line not in old:
                why = "entry added or changed"
            if why:
                out[e["rva"].lower()] = why
    return out


def rerun_matchcheck(branch, touched, errors, warnings):
    """tools/matchcheck.py --brief over `touched` in the branch's worktree; anything but a verifiable MATCH is an error."""
    rvas = sorted(touched, key=lambda r: int(r, 16))
    cmd_text = f"tools/py -I tools/matchcheck.py --rva {' '.join(rvas)} --brief"
    wt = worktree_of(branch)
    if wt is None or not (wt / "tools/matchcheck.py").exists():
        warnings.append(f"match-verified functions whose source the branch changes: {', '.join(rvas)}; no worktree has "
                        f"the branch checked out, so matchcheck was not rerun: run `{cmd_text}` on main after merging "
                        "(anything but MATCH: the entry is no longer verified)")
        return
    head = git("rev-parse", branch).strip()
    wt_head = subprocess.run(["git", "-C", str(wt), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "-C", str(wt), "status", "--porcelain", "--untracked-files=no"], capture_output=True,
                           text=True).stdout.strip()
    if wt_head != head or dirty:
        warnings.append(f"matchcheck rerun in {wt}: its checkout is not the branch tip"
                        f"{' (uncommitted changes)' if dirty else ''}; the verdicts below are for what is there")
    r = subprocess.run([sys.executable, "-I", str(wt / "tools/matchcheck.py"), "--rva", *rvas, "--brief"], cwd=wt,
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    seen = {}
    for line in r.stdout.splitlines():
        m = re.match(r"(0x[0-9a-fA-F]+)\s+(\S+)(.*)", line)
        if m and m.group(1).lower() in touched:
            seen[m.group(1).lower()] = (m.group(2), line.strip())
    for rva in rvas:
        verdict, line = seen.get(rva, (None, None))
        if verdict == "MATCH":
            warnings.append(f"{rva} ({touched[rva]}): matchcheck still MATCH")
        elif verdict == "MATCH*":
            errors.append(f"{rva} recorded \"verify\": \"match\" ({touched[rva]}): matched but not verifiable in {wt} "
                          f"(stale build objects: rebuild the worktree and rerun guard): {line[:160]}")
        elif verdict is None:
            tail = (r.stderr.strip().splitlines() or ["no output"])[-1]
            errors.append(f"{rva} recorded \"verify\": \"match\" ({touched[rva]}): matchcheck gave no verdict "
                          f"(exit {r.returncode}: {tail[:160]}); run `{cmd_text}` in {wt}")
        else:
            errors.append(f"{rva} recorded \"verify\": \"match\" ({touched[rva]}) no longer matches: {line[:200]} "
                          "(the entry must drop to reimplemented or get a difftest)")


def check_arity(mb, branch_sources, errors, warnings, rets=None, root=None, classes=None, eax=None, ecx=None,
                width=None):
    """tools/arity.py over the branch's changed src/ files (with the rest of src/ from this checkout): a declared
    stack-argument count that disagrees with the original's `ret N` (an OriginalFunction forwarder, a declaration
    naming its original, a hook's replacement, an interface's slot against what the image's vtables hold there) is an
    error, unless the same mismatch is already there at the merge base (then a warning: not this branch's doing).
    Unknowns are counted in a warning. A void declaration whose original leaves a value in eax (arity's "return"
    check, strong verdicts) is a warning listing each. An OriginalFunction declared stdcall/cdecl/fastcall-without-
    a-register-argument whose original reads ecx first (a thiscall called as a plain function; lane RD, 0xacadd0) is an
    error the same way. A bool (8-, 16-bit) declaration whose original leaves a full-width value in eax (arity's
    "width" check, strong verdicts: lane TN's texture slot 7) is a warning listing each. eax, ecx, width:
    arity.original_eax() / original_ecx() / original_width() when rets isn't given either."""
    try:
        if eax is None and rets is None:
            eax = arity.original_eax()
        if ecx is None and rets is None:
            ecx = arity.original_ecx()
        if width is None and rets is None:
            width = arity.original_width()
        rets = rets or arity.original_rets()
        funcs = arity.function_starts()
        classes = arity.load_classes() if classes is None else classes
    except Exception as e:  # no unpacked exe (local/), or the db is missing
        warnings.append(f"arity check skipped ({type(e).__name__}: {e}); run `tools/py -I tools/arity.py "
                        f"{' '.join(sorted(branch_sources))}` on main after merging")
        return
    files = dict(arity.source_files(root or tsdb.ROOT))

    def run(overrides):
        merged = dict(files)
        merged.update(overrides)
        return arity.check(list(merged.items()), rets, funcs, list(branch_sources), classes, eax=eax, ecx=ecx,
                           width=width)

    def key(f):  # line numbers move between the base and the branch (and a slot call's inferred fit with the file)
        detail = f["detail"]
        if f["kind"] == "slot call" and "original ret" in detail:
            detail = detail[detail.index("original ret"):]
        return f["kind"], f["where"].split(":")[0], f["where"].split(" ", 1)[-1] if " " in f["where"] else "", \
            f["rva"], detail

    found = run(branch_sources)
    voids = [f for f in found if f["kind"] == "return" and f["verdict"] == "strong"]
    if voids:
        warnings.append(
            f"arity: {len(voids)} declaration(s) in the changed files declared void whose original leaves a value in "
            "eax (lanes OR, OX: game callers read it and crashed). Declare the real return, or add `eax-ignored: "
            "<reason>` to the declaration's comment when no caller reads it (docs/lane/write.md, \"Void that "
            "isn't\"):\n    " + "\n    ".join(f"{f['where']}  {f['rva']}: {f['detail']}" for f in voids))
    wides = [f for f in found if f["kind"] == "width" and f["verdict"] == "strong"]
    if wides:
        warnings.append(
            f"arity: {len(wides)} declaration(s) in the changed files declared bool (or 8/16-bit) whose original "
            "leaves a full-width value in eax (lane TN's texture slot 7: ours set only al, the image looped over the "
            "whole eax and crashed). Declare the real width (uint32_t or the real type), or add `eax-width: <reason>` "
            "to the declaration's comment when every caller reads only al (docs/lane/write.md, \"Void that "
            "isn't\"):\n    " + "\n    ".join(f"{f['where']}  {f['rva']}: {f['detail']}" for f in wides))
    bad = [f for f in found if f["verdict"] == "MISMATCH"]
    before = set()
    if bad:
        base = {}
        for p in branch_sources:
            try:
                base[p] = git("show", f"{mb}:{p}")
            except subprocess.CalledProcessError:
                base[p] = ""  # new on the branch
        before = {key(f) for f in run(base) if f["verdict"] == "MISMATCH"}
    for f in bad:
        text = f"{f['where']}: {f['kind']} {f['rva']}: {f['detail']}"
        if key(f) in before:
            warnings.append(f"arity mismatch already on the base (not this branch's): {text}")
        elif f["kind"] == "ecx":
            errors.append(f"arity: {text} (ecx holds whatever the caller left there: `tools/py -I tools/arity.py "
                          "--ecx RVA` shows the read; docs/lane/write.md, \"Thiscall called as a function\")")
        else:
            errors.append(f"arity: {text} (the stack is off by the difference after every call: declare the "
                          "original's arguments; `tools/py -I tools/arity.py --rets RVA` shows its rets)")
    unknown = [f for f in found if f["verdict"] == "unknown" and f["kind"] != "slot call"]
    ok = sum(f["verdict"] == "ok" for f in found if f["kind"] != "slot call")
    if unknown:
        warnings.append(f"arity: {ok} sites agree, {len(unknown)} unknown in the changed files (`tools/py -I "
                        f"tools/arity.py <file> -v` lists them: no ret found, a type's size, an unresolved hook)")
    slots = [f for f in found if f["kind"] == "slot call"]
    unknown_slots = [f for f in slots if f["verdict"] == "unknown"]
    if unknown_slots:
        # the calls on lines the branch adds or changes are the lane's to answer; the rest were there at the base
        old = {}
        for p in branch_sources:
            try:
                old[p] = {ln.strip() for ln in git("show", f"{mb}:{p}").splitlines()}
            except subprocess.CalledProcessError:
                old[p] = set()  # new on the branch

        def line_of(f):
            path, rest = f["where"].split(":", 1)
            lines = branch_sources.get(path, "").splitlines()
            n = int(rest.split(" ", 1)[0])
            return path, lines[n - 1].strip() if 0 < n <= len(lines) else None

        new = [f for f in unknown_slots if line_of(f)[1] is None or line_of(f)[1] not in old.get(line_of(f)[0], set())]
        if new:
            shown = new[:SLOT_UNKNOWNS_SHOWN]
            more = len(new) - len(shown)
            warnings.append(
                f"arity: {len(new)} slot calls on lines this branch adds or changes not checked (the receiver's class "
                f"or an argument's size not known; {len(unknown_slots) - len(new)} more unchanged, of {len(slots)} in "
                "the changed files). The lane must answer each in its report (what the slot's body pops, from `tools/py "
                "-I tools/arity.py --rets RVA`), or type the receiver / add `// receiver: Class` so the check resolves "
                "it:\n    " + "\n    ".join(f"{f['where']}: {f['detail']}" for f in shown)
                + (f"\n    ... {more} more (`tools/py -I tools/arity.py <file> -v`)" if more else ""))


_SLOT_FACTS = None


def new_slot_calls(path, text, mb, branch):
    """Warnings for the vtable calls by slot number `path` gains on the branch (tools/slotcalls.py's forms, matched by
    slot, receiver and argument count, so renaming a helper adds none): file:line, slot, receiver, and the method fact
    that could name it where the receiver is known and one fits."""
    global _SLOT_FACTS
    if _SLOT_FACTS is None:
        _SLOT_FACTS = slotcalls.load_facts(slotcalls.ROOT / "db")

    def reader(rev):
        def read(p):
            try:
                return git("show", f"{rev}:{Path(p).as_posix()}")
            except subprocess.CalledProcessError:
                return None
        return read
    try:
        old_text = git("show", f"{mb}:{path}")
    except subprocess.CalledProcessError:
        old_text = ""
    # template names in the receiver ignored, so `V<6>(m)` and `RawSlot<6>(m)` as a receiver are the same
    key = lambda c: (c["slot"], re.sub(r"\b\w+(?=\s*<)", "@", c["obj"]), c["nargs"])  # noqa: E731
    left = {}
    for c in slotcalls.analyse(Path(path), _SLOT_FACTS, old_text, reader(mb)):
        left[key(c)] = left.get(key(c), 0) + 1
    out, unnamed = [], []
    for c in slotcalls.analyse(Path(path), _SLOT_FACTS, text, reader(branch)):
        if left.get(key(c), 0) > 0:
            left[key(c)] -= 1
            continue
        name = slotcalls.summary(c)
        if name:
            out.append(f"{path}:{c['line']}: new vtable call by slot number {c['form']}<{c['slot']}>({c['obj'][:40]}): "
                       f"a method fact names it: {name} (ifaces/ classes; docs/lane/write.md)")
        else:
            unnamed.append(f"{c['line']}")
    if unnamed:
        out.append(f"{path}: {len(unnamed)} new vtable call(s) by slot number with no method fact for their receiver, "
                   f"lines {','.join(unnamed)}: name those whose purpose is known (tools/slotcalls.py {path}; "
                   "docs/lane/write.md)")
    return out


MISSED = "db/missed-functions.jsonl"
# The standalone programs' definitions of owned statics and owned tables (docs/lane/hook-and-record.md, owned tables):
# a lane that owns a table or cell adds its line here, outside its own files.
OWNED_STANDALONE = ("standalone/owned_statics.cpp", "standalone/owned_graphics.cpp")
OWNED_LINE = re.compile(r'\s*(?:TS2_OWNED_STATIC\([^,]+,\s*([0-9a-fA-F]+)\)[^;]*;'
                        r'|#pragma\s+comment\(\s*linker\s*,\s*"/alternatename:_ts2_rva_([0-9a-fA-F]+)=[A-Za-z0-9_?@$]+"\s*\))'
                        r'\s*(?://.*)?$')


def owned_standalone_change(old_text, new_text, branch_src):
    """Errors for a branch's change to standalone/owned_statics.cpp or owned_graphics.cpp, accepted without --paths
    when it only adds TS2_OWNED_STATIC definitions, `/alternatename:_ts2_rva_<hex>=...` aliases, comments and blank
    lines, each for an RVA the branch's own src/ files own (they name ts2_rva_<hex> or ts2_own_vt_<hex>, or declare
    TS2_OWNED_STATIC(..., <hex>)). branch_src: the text of the src/ files the branch adds or changes."""
    import difflib
    errors = []
    old, new = old_text.splitlines(), new_text.splitlines()
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, old, new, autojunk=False).get_opcodes():
        if tag in ("delete", "replace"):
            for line in old[i1:i2]:
                errors.append(f"removes or changes a line: {line.strip()[:100]}")
        if tag not in ("insert", "replace"):
            continue
        for line in new[j1:j2]:
            if not line.strip() or line.strip().startswith("//"):
                continue
            m = OWNED_LINE.match(line)
            if not m:
                errors.append(f"adds a line other than an owned static or alias: {line.strip()[:100]}")
                continue
            hexrva = (m.group(1) or m.group(2)).lower().lstrip("0") or "0"
            owns = re.compile(r"ts2_(?:rva|own_vt)_0*" + hexrva + r"\b|TS2_OWNED_STATIC\([^)]*,\s*0*" + hexrva
                              + r"\s*\)", re.I)
            if not owns.search(branch_src):
                errors.append(f"adds 0x{hexrva}, which no src/ file the branch changes owns: {line.strip()[:100]}")
    return errors


def missed_functions_change(old_text, new_text):
    """(errors, warnings) for a branch's change to db/missed-functions.jsonl: manual entries (`"source": "manual: ..."`,
    tools/missedfuncs.py --add) it adds or changes are warnings to read; removing one (another lane's fact) is an error;
    a change to the scan's own entries (the file is regenerated on every --add) is a warning: rerun missedfuncs.py on
    main after merging."""
    errors, warnings = [], []

    def parse(text, which):
        out = {}
        for n, line in enumerate(text.splitlines(), 1):
            if not line.strip():
                continue
            try:
                e = json.loads(line)
                out[(e["rva"], e["kind"])] = e
            except (ValueError, KeyError, TypeError):
                errors.append(f"line {n} of the {which} file is not a missed-function entry: {line[:80]}")
        return out

    old, new = parse(old_text, "merge base's"), parse(new_text, "branch's")

    def manual(e):
        return str(e.get("source", "")).startswith("manual")

    for key, e in new.items():
        if manual(e) and old.get(key) != e:
            what = "adds" if key not in old else "changes"
            warnings.append(f"{what} manual entry {e['rva']} ({e['kind']}, {e.get('size')} bytes): "
                            f"{str(e.get('source'))[8:100]} (read it; check the size against the disassembly)")
    for key, e in old.items():
        if manual(e) and key not in new:
            errors.append(f"removes manual entry {e['rva']} ({str(e.get('source'))[8:80]}): another lane's fact")
    scan = sorted({k[0] for k in set(old) | set(new) if not manual(old.get(k) or new.get(k)) and old.get(k) != new.get(k)},
                  key=lambda r: int(r, 16))
    if scan:
        warnings.append(f"{len(scan)} scan entries differ ({', '.join(scan[:6])}{' ...' if len(scan) > 6 else ''}; "
                        "missedfuncs.py regenerates them on --add): rerun tools/missedfuncs.py on main after merging")
    return errors, warnings


def callers_warnings(through_callers, detail=False):
    """The reminder for new "in_effect": "callers" entries: one line per status file with the count and its traps
    (detail: one line per entry, as before lane PG). Malformed entries are errors elsewhere, not counted here."""
    out = []
    for path, items in sorted(through_callers.items()):
        if detail:
            out += [f"{path}: {rva} in effect through its callers (not hooked; trap {trap}): rerun tools/status.py "
                    "--check on main after merging" for rva, trap in items]
            continue
        traps = sorted({t for _, t in items})
        out.append(f"{path}: {len(items)} new entr{'y' if len(items) == 1 else 'ies'} in effect through their callers "
                   f"(not hooked; trap{'s' if len(traps) > 1 else ''} {', '.join(traps)}): rerun tools/status.py --check "
                   "on main after merging (--callers-detail lists them)")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("branch")
    ap.add_argument("--base", default="main")
    ap.add_argument("--paths", action="append", default=[],
                    help="paths the task gave the branch beyond the lane files (prefixes ending in / or files, "
                         "comma-separated, repeatable): changes there are warnings to read, not errors")
    ap.add_argument("--tooling", action="store_true", help=f"a tooling lane: --paths {','.join(TOOLING_PATHS)}")
    ap.add_argument("--no-matchcheck", action="store_true",
                    help="don't rerun tools/matchcheck.py on match-verified functions whose source the branch changes")
    ap.add_argument("--no-arity", action="store_true",
                    help="don't check the stack bytes the branch's declarations pop against the originals' ret N")
    ap.add_argument("--callers-detail", action="store_true",
                    help="one warning per new \"in_effect\": \"callers\" entry (default: one per status file, counted)")
    args = ap.parse_args()
    through_callers = {}  # status file -> [(rva, trap)] of new, well-formed callers entries
    allowed = [p.strip().replace("\\", "/") for a in args.paths for p in a.split(",") if p.strip()]
    if args.tooling:
        allowed += TOOLING_PATHS
    mb = git("merge-base", args.base, args.branch).strip()
    changes = [l.split("\t") for l in git("diff", "--name-status", mb, args.branch).splitlines()]
    errors, warnings = [], []
    branch_sources = {}
    kinds, declared = declared_lane(mb, args.branch)
    if (kinds or declared) and not allowed:
        # the narrower grant when the branch names its paths
        flag = f"--paths {','.join(declared)}" if declared else "--tooling"
        warnings.append(f"the branch declares {'Lane-type: ' + ', '.join(kinds) if kinds else ''}"
                        f"{'; ' if kinds and declared else ''}{'Lane-paths: ' + ', '.join(declared) if declared else ''}"
                        f" (not honoured: a trailer grants nothing, so its paths are errors below): if its task gave "
                        f"it those paths, rerun with {flag}")
    for st, *paths in changes:
        path = paths[-1].replace("\\", "/")
        grant = allowed_by(path, allowed)
        if grant and (path.startswith(REFUSED_PREFIXES) or path in CORE_HOOK_FILES
                      or not path.startswith(("src/", "tests/", "db/status/", "db/methods/"))):
            # A tooling lane's own paths (--paths / --tooling): expected, listed for the reviewer. Code under src/
            # still gets the checks below.
            warnings.append(f"{st} {path}: allowed by --paths {grant} (read the change)")
            if not path.startswith("src/") or st.startswith("D"):
                continue
        # A subsystem lane keeps its subsystem's notes file (docs/subsystems/<name>.md) up to date.
        if path.startswith("docs/subsystems/") and path.endswith(".md") and not st.startswith("D"):
            warnings.append(f"{st} {path}: subsystem notes (read the change)")
            continue
        # Every lane writes its tooling feedback (docs/lane/brief.md, "Finishing"): one file per lane, no conflicts.
        if re.fullmatch(r"notes/feedback/[A-Za-z0-9]+\.md", path) and not st.startswith("D"):
            continue
        if not grant and path in OWNED_STANDALONE and st == "M":
            # Table or cell ownership (lane FT, after FD): the standalone definition goes with the src/ that owns it.
            src_text = "\n".join(git("show", f"{args.branch}:{p[-1]}") for s_, *p in changes
                                  if p[-1].startswith("src/") and not s_.startswith("D"))
            bad = owned_standalone_change(git("show", f"{mb}:{path}"), git("show", f"{args.branch}:{path}"), src_text)
            if bad:
                errors += [f"{st} {path}: {b} (outside the agent's lanes: --paths {path} if the task gave it)"
                           for b in bad]
            else:
                warnings.append(f"{st} {path}: owned statics or aliases for tables the branch owns (read the change)")
            continue
        if not grant and (path.startswith(REFUSED_PREFIXES) or path in CORE_HOOK_FILES):
            errors.append(f"{st} {path}: outside the agent's lanes"
                          + ("" if allowed else " (a tooling lane's task paths: --tooling or --paths)"))
            continue
        if path == callers.TRAP_TABLE:
            warnings.append(f"{st} {path}: the generated trap table: after merging, `tools/callers.py --check-traps` and "
                            "`tools/status.py --check` (which rerun the callers check)")
            continue
        # `tools/ghidra.py export --lane X` writes a lane's recorded facts here (docs/lane/understand.md): read the diff.
        if path == "db/annotations.jsonl" and st == "M":
            warnings.append(f"{st} {path}: exported Ghidra annotations (lane/tentative per entry): read the diff")
            continue
        # Functions Ghidra has no record for, added by `tools/missedfuncs.py --add` (hook-and-record.md: a factory only
        # referenced by address): manual entries are facts a lane is told to add; the rest of the file is the scan's.
        if path == MISSED and st in ("M", "A"):
            try:
                old = git("show", f"{mb}:{path}")
            except subprocess.CalledProcessError:
                old = ""
            e, w = missed_functions_change(old, git("show", f"{args.branch}:{path}"))
            errors += [f"{path}: {x}" for x in e]
            warnings += [f"{path}: {x}" for x in w]
            continue
        if not grant and not path.startswith(("src/", "tests/", "db/status/", "db/methods/")):
            errors.append(f"{st} {path}: unexpected location")
            continue
        if st.startswith("D"):
            errors.append(f"deletes {path}")
            continue
        if st == "M" and path.startswith(("tests/", "src/hook/tables/", "db/status/")):
            warnings.append(f"modifies existing {path}: check the change is intended")
        text = git("show", f"{args.branch}:{path}")
        if path.endswith((".cpp", ".h")):
            if path.startswith("src/"):
                branch_sources[path] = text
            if re.search(r"_emit\b", text):
                errors.append(f"{path}: _emit (raw bytes)")
            if re.search(r"__asm|\basm\s*\(", text):
                warnings.append(f"{path}: inline asm: allowed only as a short ABI shim of our own "
                                "(a register C++ can't name), never original code")
            if path.startswith("src/") and re.search(r"\bOriginal\s*<|ctx\.Original|ctx\.image", text):
                errors.append(f"{path}: calls or reads original code from src/")
            if "GameAddress" in text and path.startswith("src/"):
                # A game address used as code: cast to a function pointer, called, or registered.
                for m in re.finditer(r".*(\(\s*\*\s*\)\s*\([^)]*\)\s*>\s*\(\s*GameAddress|GameAddress<[^>]*>\([^)]*\)\s*\(|atexit\s*\([^;]*GameAddress).*", text):
                    errors.append(f"{path}: game code used as a function (call, callback or atexit): {m.group(0).strip()[:120]}")
                for m in re.finditer(r".*GameAddress.*", text):
                    warnings.append(f"{path}: GameAddress use (data/vtables only): {m.group(0).strip()[:120]}")
            if path.startswith("src/"):
                # Vtable calls by slot number (docs/lane/write.md "Readable code": call understood slots through the
                # generated ifaces/ classes). Counted against the merge base, so only the lane's new ones show. Every
                # form tools/slotcalls.py finds, local slot-call helpers (`V<29, R>(obj)`) and RawSlot included.
                for line in new_slot_calls(path, text, mb, args.branch):
                    warnings.append(line)
            if re.search(r"(0x[0-9a-fA-F]{2}\s*,\s*){24,}", text):
                warnings.append(f"{path}: long hex byte run (pasted original bytes?)")
            if text.count("\n") > 1500:
                warnings.append(f"{path}: {text.count(chr(10))} lines")
        if path.startswith("db/status/"):
            try:
                old = set(git("show", f"{mb}:{path}").splitlines())
            except subprocess.CalledProcessError:
                old = set()
            for line in text.splitlines():
                if not line.strip() or line in old:
                    continue
                e = json.loads(line)
                if e.get("in_effect", "hook") != "hook":
                    # In effect through its callers (CLAUDE.md): no hook; the entry names its check and trap, and the
                    # coordinator reruns the check on main (tools/status.py --check runs tools/callers.py).
                    if e["in_effect"] != "callers":
                        errors.append(f"{path}: {e['rva']} unknown \"in_effect\" {e['in_effect']!r}")
                    elif not callers.CHECK_FIELD.fullmatch(str(e.get("callers_check", ""))) or not callers.parse_trap(e.get("trap")):
                        errors.append(f"{path}: {e['rva']} \"in_effect\": \"callers\" without \"callers_check\" "
                                      "(callers.py vN date) or a \"trap\" (Section.Key=values)")
                    else:
                        through_callers.setdefault(path, []).append((e["rva"], e["trap"]))
                if e.get("status") == "verified" and e.get("verify") == "match":
                    if not e.get("matchcheck"):
                        errors.append(f"{path}: {e['rva']} verified by match without a matchcheck version")
                    continue  # byte match (CLAUDE.md): no difftest; the coordinator reruns matchcheck after merging
                if e.get("status") == "verified":
                    try:
                        test = git("show", f"{args.branch}:{e['test']}")
                    except subprocess.CalledProcessError:
                        errors.append(f"{path}: {e['rva']} verified but test {e['test']} missing")
                        continue
                    if e["rva"].lower() not in test.lower():
                        errors.append(f"{path}: {e['rva']} verified but {e['test']} never mentions that RVA")
    warnings += callers_warnings(through_callers, args.callers_detail)
    # Original callees: the branch's changed src files against the branch's status entries.
    if branch_sources:
        entries = []
        for name in git("ls-tree", "--name-only", args.branch, "db/status/").split():
            for line in git("show", f"{args.branch}:{name}").splitlines():
                if line.strip():
                    entries.append((name, json.loads(line)))
        found = origcalls.scan(branch_sources)
        problems, notes = origcalls.check(found, entries)
        # Stale declarations for files the branch didn't touch are not its doing.
        errors += [p for p in problems if any(f in p for f in branch_sources)]
        warnings += notes
        funcs = {f["rva"]: f for f in tsdb.shared_functions()}
        for path, sites in found.items():
            for line, rva in sites:
                if rva:
                    f = funcs.get(rva, {})
                    warnings.append(f"{path}:{line}: calls original {rva} ({f.get('size', '?')} bytes, {f.get('name', '?')})")
        if args.no_arity:
            warnings.append("--no-arity: forwarder arity not checked (run tools/arity.py on the changed files)")
        else:
            check_arity(mb, branch_sources, errors, warnings)
    # Byte-match drift (0x144913: a later lane rewrote a match-verified function and it stopped matching).
    touched = match_entries_touched(args.branch, mb, {p[-1].replace("\\", "/") for _, *p in changes})
    if touched and args.no_matchcheck:
        warnings.append(f"--no-matchcheck: match-verified functions whose source the branch changes, not rechecked: "
                        f"{', '.join(sorted(touched))}")
    elif touched:
        rerun_matchcheck(args.branch, touched, errors, warnings)
    # Union-merged JSONL (.gitattributes): what the branch's file, or the union merge with the base, would break.
    try:
        e, w = annotations_check.check_merge(args.branch, args.base)
        errors += e
        warnings += w
    except (subprocess.CalledProcessError, OSError, ValueError) as ex:
        warnings.append(f"union-merge check skipped ({type(ex).__name__}: {ex}); run tools/annotations_check.py after "
                        "merging")
    # Existing difftests the branch may have loosened (tools/testweaken.py): warnings the lane must answer.
    try:
        weakened = testweaken.branch_check(args.branch, args.base, mb)
    except (subprocess.CalledProcessError, OSError, ValueError) as ex:
        weakened = []
        warnings.append(f"test-weakening check skipped ({type(ex).__name__}: {ex}); run tools/testweaken.py {args.branch}")
    if weakened:
        warnings.append(f"{len(weakened)} test-weakening candidate(s) below: the lane answers each in its report "
                        "(why it isn't a weakening, or what replaced the check)")
        warnings += weakened
    # Tests that patch, or plant the statics of, a function the branch hooks or verifies on the game's side only
    # (tools/guard_patches.py): they stop covering what they did once the game's side reaches ours.
    try:
        warnings += guard_patches.branch_check(args.branch, args.base, mb)
    except (subprocess.CalledProcessError, OSError, ValueError) as ex:
        warnings.append(f"stand-in check skipped ({type(ex).__name__}: {ex}); run tools/guard_patches.py {args.branch}")
    for w in warnings:
        print("warning: " + w)
    for e in errors:
        print("ERROR: " + e)
    print(f"{len(changes)} files changed, {len(errors)} errors, {len(warnings)} warnings")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
