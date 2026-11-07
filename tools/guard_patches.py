"""Tests that stand in for, or plant state for, a function the branch makes ours, on the game's side only.

When a branch hooks a function (a line added to src/hook/tables/*.cpp) or records it reimplemented / verified (its
db/status diff), the game's callers reach ours from then on. A test elsewhere that patched the image's function alone
(shape::CodePatch, a Patches.Image, a local jmp-writing helper) or planted the image function's private statics (a
registry map, a once byte) no longer covers what it did, and fails only in the full suite (lane SM, 2026-11-01: 11
sg_ tests). This lists them as warnings (tools/guard.py runs it; docs/lane/test.md, Pitfalls):

  - patch: a statement naming the RVA (a hex literal, or a constant defined in tests/ to it) that patches code
    (Patch / Redirect / Detour / Jmp / Stub in a name, `.Image(`, `CodeOf(`) and is not a both-sides stand-in
    (`shape::StandIn`, `.Both(`);
  - plant: a mention of one of the function's private statics (a writable cell only it and at most a few other
    functions reference, from the image's relocations; a hex literal or ts2_rva_<hex>) in a test file other than the
    function's own status test, when neither that file nor a header it includes stands in for the RVA on both sides
    and our source doesn't use the same cell (then both sides share it).

Usage (standalone, for calibration):
    .venv/Scripts/python -I tools/guard_patches.py <branch> [--base main]
    .venv/Scripts/python -I tools/guard_patches.py --merge <merge commit>   (its second parent against its first)
"""
import argparse
import bisect
import json
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gitblob  # noqa: E402
import tsdb  # noqa: E402

HINT = "make it a both-sides StandIn, or drop it if ours is now covered"
CELL_MAX_FUNCS = 4    # a static referenced by more functions than this is shared state, not one function's own
STATEMENT_CHARS = 400  # how far back a statement is followed for its patch helper's name

HOOK_LINE = re.compile(r'\{\s*"[^"]*"\s*,\s*(0x[0-9a-fA-F]+)\b')
HEX = re.compile(r"\b0x([0-9a-fA-F]{5,8})\b")
TS2_RVA = re.compile(r"\bts2_rva_([0-9a-fA-F]{5,8})\b")
CONST_DEF = re.compile(r"\b(?:constexpr|const)\b[^;{}()=]*?\b([A-Za-z_]\w*)\s*(?:=\s*|\{\s*)(0x[0-9a-fA-F]{5,8})u?\s*[;,}]"
                       r"|#define\s+(\w+)\s+(0x[0-9a-fA-F]{5,8})\b")
PATCHY = re.compile(r"(?i)(?<!dis)patch|redirect|detour|\bjmp|jump|stub|\.\s*Image\s*\(|\bCodeOf\s*\(")
BOTH_SIDES = re.compile(r"\bStandIn\b|\.\s*Both\s*\(|->\s*Both\s*\(")
ORIGINAL = re.compile(r"\bOriginal(?:Function)?\s*<")
INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def git(*a):
    return subprocess.run(["git", *a], capture_output=True, text=True, check=True, encoding="utf-8").stdout


def show(rev, path):
    try:
        return gitblob.show(rev, path)
    except subprocess.CalledProcessError:
        return ""


def read_tree(rev, prefixes):
    """{path: text} of the files under `prefixes` at `rev`, read in one git cat-file call."""
    entries = []
    for line in git("ls-tree", "-r", rev, "--", *prefixes).splitlines():
        meta, path = line.split("\t", 1)
        _, kind, sha = meta.split()
        if kind == "blob":
            entries.append((path, sha))
    if not entries:
        return {}
    out = subprocess.run(["git", "cat-file", "--batch"], input="".join(f"{s}\n" for _, s in entries).encode(),
                         capture_output=True, check=True).stdout
    files, pos = {}, 0
    for path, _ in entries:
        nl = out.index(b"\n", pos)
        size = int(out[pos:nl].split()[2])
        files[path] = out[nl + 1:nl + 1 + size].decode("utf-8", "replace")
        pos = nl + 1 + size + 1
    return files


# --- what the branch makes ours ---------------------------------------------------------------------------------

def hook_rvas(text):
    return {int(m.group(1), 16) for line in text.splitlines() if not line.lstrip().startswith("//")
            for m in [HOOK_LINE.search(line)] if m}


def status_map(texts):
    """{rva: entry} over status file texts."""
    out = {}
    for text in texts:
        for line in text.splitlines():
            if line.strip():
                try:
                    e = json.loads(line)
                    out[int(e["rva"], 16)] = e
                except (ValueError, KeyError):
                    pass
    return out


def newly_ours(base_files, branch_files):
    """{rva: {"why": ..., "tests": set, "name": str}}: RVAs hooked on the branch and not on the base, and RVAs whose
    status is ours on the branch and wasn't on the base. Both arguments: {path: text} of src/hook/tables + db/status."""
    def split(files):
        hooks = set().union(*[hook_rvas(t) for p, t in files.items() if p.startswith("src/hook/tables/")] or [set()])
        return hooks, status_map(t for p, t in files.items() if p.startswith("db/status/"))
    old_hooks, old_status = split(base_files)
    new_hooks, new_status = split(branch_files)
    out = {}
    for rva in new_hooks - old_hooks:
        out[rva] = {"why": "hooked"}
    for rva, e in new_status.items():
        if e.get("status") in tsdb.OURS and old_status.get(rva, {}).get("status") not in tsdb.OURS:
            out.setdefault(rva, {"why": e["status"]})
    for rva, info in out.items():
        e = new_status.get(rva, {})
        info["tests"] = {e["test"]} if e.get("test") else set()
        info["name"] = e.get("name", "")
        info["callers_only"] = info["why"] != "hooked" and e.get("in_effect") == "callers"
    return out


# --- the image: each function's private statics -----------------------------------------------------------------

def private_statics(rvas, exe=None, funcs=None):
    """{cell rva: set(function rvas)} for the writable cells referenced (relocated absolute dwords) from the functions in
    `rvas` and from at most CELL_MAX_FUNCS functions in all. None when the image isn't there."""
    exe = Path(exe or tsdb.unpacked_exe())
    if not exe.exists() or not rvas:
        return None
    data = exe.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    nsec, optsize = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    base = struct.unpack_from("<I", data, opt + 28)[0]
    reloc_rva, reloc_size = struct.unpack_from("<II", data, opt + 96 + 5 * 8)
    sections = []
    for i in range(nsec):
        s = opt + optsize + 40 * i
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, s + 8)
        flags = struct.unpack_from("<I", data, s + 36)[0]
        sections.append((va, max(vsize, rawsize), raw, rawsize, flags))

    def offset(rva):
        for va, size, raw, rawsize, _ in sections:
            if va <= rva < va + size:
                return raw + (rva - va) if rva - va < rawsize else None
        return None

    def writable_data(rva):
        return any(va <= rva < va + size and flags & 0x80000000 and not flags & 0x20000000
                   for va, size, _, _, flags in sections)

    funcs = sorted((int(f["rva"], 16), max(f["size"], 1)) for f in (funcs or tsdb.shared_functions()))
    starts = [f[0] for f in funcs]

    def containing(site):
        i = bisect.bisect_right(starts, site) - 1
        return funcs[i][0] if i >= 0 and site < funcs[i][0] + funcs[i][1] else None

    refs = {}  # cell -> set(functions)
    pos, end = offset(reloc_rva), offset(reloc_rva) + reloc_size
    while pos < end:
        page, block = struct.unpack_from("<II", data, pos)
        if block < 8:
            break
        for (e,) in struct.iter_unpack("<H", data[pos + 8:pos + block]):
            if e >> 12 != 3:
                continue
            site = page + (e & 0xfff)
            o = offset(site)
            if o is None:
                continue
            cell = struct.unpack_from("<I", data, o)[0] - base
            if 0 <= cell < 0x10000000 and writable_data(cell):
                f = containing(site)
                if f is not None:
                    refs.setdefault(cell, set()).add(f)
        pos += block
    return {c: fs for c, fs in refs.items() if fs & rvas and len(fs) <= CELL_MAX_FUNCS}


# --- tests/ ------------------------------------------------------------------------------------------------------

def strip_comments(text):
    """The text with comments and string literals blanked (newlines kept, so offsets and line numbers hold)."""
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"', lambda m: re.sub(r"[^\n]", " ", m.group(0)), text,
                  flags=re.S)


def statement(code, at):
    """The statement around offset `at`: back to the previous `;` (or a brace ending a line), on to the next `;`."""
    lo = max(0, at - STATEMENT_CHARS)
    start = max(code.rfind(";", lo, at), code.rfind("{\n", lo, at), code.rfind("}\n", lo, at),
                code.rfind("{\r\n", lo, at), code.rfind("}\r\n", lo, at))
    stop = code.find(";", at)
    return code[(start + 1 if start >= 0 else lo):(stop if stop >= 0 else len(code))]


def includes_closure(files):
    """{path: set of tests/ paths it includes, transitively, itself included}."""
    direct = {}
    for path, text in files.items():
        d = str(Path(path).parent).replace("\\", "/")
        direct[path] = {f"{d}/{m}" for m in INCLUDE.findall(text) if f"{d}/{m}" in files}
    out = {}
    for path in files:
        seen, todo = {path}, [path]
        while todo:
            for n in direct.get(todo.pop(), ()):
                if n not in seen:
                    seen.add(n)
                    todo.append(n)
        out[path] = seen
    return out


def scan(files, targets, cells=None, ours_cells=frozenset(), callees=None):
    """Hits in tests/ files {path: text}: [(path, line, rva, kind, value)] with kind "patch" or "plant".
    targets: {rva: {"tests": set of own test paths}}; cells: {cell: set(function rvas)} (private_statics);
    ours_cells: cells our source names itself (both sides then share them); callees: {rva: set(rvas)}, so a stand-in
    over a function also covers the statics of what it calls (a registry's holder, run only from inside it)."""
    callees = callees or {}
    cells = {c: fs & set(targets) for c, fs in (cells or {}).items() if c not in ours_cells}
    codes = {p: strip_comments(t) for p, t in files.items()}
    # Constants naming a target RVA, per defining file.
    defs = {}
    for path, code in codes.items():
        for m in CONST_DEF.finditer(code):
            name, value = (m.group(1), m.group(2)) if m.group(1) else (m.group(3), m.group(4))
            defs.setdefault(name, {})[path] = int(value, 16)
    # Names of a target in some file; each use resolves to the definition the file sees (its own or an included
    # header's), else to the one value tests/ gives the name.
    defs = {n: d for n, d in defs.items() if any(v in targets for v in d.values())}
    closure = includes_closure(files)

    def const_value(name, path):
        seen = {p: v for p, v in defs.get(name, {}).items() if p in closure[path]}
        values = set((seen or defs.get(name, {})).values())
        return values.pop() if len(values) == 1 else None

    name_re = re.compile(r"\b(" + "|".join(map(re.escape, sorted(defs))) + r")\b") if defs else None
    stood = {}  # path -> RVAs stood in on both sides in that file

    def mentions(path, code):
        """[(offset, value)] of every hex literal, ts2_rva_ name and target constant in `code`."""
        out = [(m.start(), int(m.group(1), 16)) for m in HEX.finditer(code)]
        out += [(m.start(), int(m.group(1), 16)) for m in TS2_RVA.finditer(code)]
        if name_re:
            for m in name_re.finditer(code):
                v = const_value(m.group(1), path)
                if v is not None and not re.match(r"\s*(?:=|\{)\s*0x", code[m.end():m.end() + 40]):  # not its definition
                    out.append((m.start(), v))
        return out

    found = {p: mentions(p, c) for p, c in codes.items()}
    for path, code in codes.items():
        stood[path] = {v for at, v in found[path] if v in targets and BOTH_SIDES.search(statement(code, at))}
    hits = []
    for path, code in codes.items():
        covered = set().union(*(stood[p] for p in closure[path]))
        covered |= {c for r in covered for c in callees.get(r, ())}
        lines = None
        for at, v in found[path]:
            kind = None
            if v in targets:
                s = statement(code, at)
                if PATCHY.search(s) and not BOTH_SIDES.search(s) and not ORIGINAL.search(s):
                    kind, rva = "patch", v
            elif v in cells:
                owners = [f for f in cells[v] if f not in covered and path not in targets[f].get("tests", ())]
                if owners and not BOTH_SIDES.search(statement(code, at)):
                    kind, rva = "plant", min(owners)
            if kind:
                if lines is None:
                    lines = [0] + [i + 1 for i, ch in enumerate(code) if ch == "\n"]
                hits.append((path, bisect.bisect_right(lines, at), rva, kind, v))
    # A function in effect through its callers runs only from inside a hooked caller: planting its statics alone (a
    # once byte inside a saved range) is not planting for it; with its caller's statics the file is reported.
    real = {p for p, _, r, k, _ in hits if k == "patch" or not targets[r].get("callers_only")}
    return [h for h in hits if h[0] in real]


def ours_cell_names(src_files):
    """Cells our source names itself (GameAddress literals, ts2_rva_ names): state both sides share."""
    out = set()
    for text in src_files.values():
        code = strip_comments(text)
        out |= {int(m.group(1), 16) for m in TS2_RVA.finditer(code)}
        out |= {int(m.group(1), 16) for m in re.finditer(r"GameAddress\s*<[^;]*?>\s*\(\s*0x([0-9a-fA-F]{5,8})", code)}
    return out


def report(hits, targets, names=None):
    """Warning lines: one per (file, kind), at its first line, naming the RVAs and counting the lines."""
    if names is None:
        names = {int(n["rva"], 16): n.get("name", "") for n in tsdb.jsonl("names.jsonl")}
    groups = {}
    for path, line, rva, kind, value in hits:
        groups.setdefault((path, kind), []).append((line, rva, value))
    out = []
    for (path, kind), sites in sorted(groups.items()):
        rvas = sorted({r for _, r, _ in sites})
        rvas = [r for r in rvas if not targets[r].get("callers_only")] or rvas  # the hooked caller, not its helpers
        shown = ", ".join(f"{r:#x} {(targets[r].get('name') or names.get(r, '?'))[:50]} ({targets[r]['why']})"
                          for r in rvas[:3]) + (f", +{len(rvas) - 3} more" if len(rvas) > 3 else "")
        what = "patches on the game's side only" if kind == "patch" else "plants the statics of"
        more = f" (+{len(sites) - 1} more lines)" if len(sites) > 1 else ""
        out.append(f"{path}:{min(sites)[0]}: {what} {shown}{more}: {HINT}")
    return out


def branch_check(branch, base="main", mb=None, tests_at=None):
    """Warning lines for `branch` against its merge base with `base`."""
    mb = mb or git("merge-base", base, branch).strip()
    lanes = ("src/hook/tables/", "db/status/")
    targets = newly_ours(read_tree(mb, lanes), read_tree(branch, lanes))
    if not targets:
        return []
    tests = {p: t for p, t in read_tree(tests_at or branch, ("tests/",)).items() if p.endswith((".cpp", ".h"))}
    funcs = tsdb.shared_functions()
    cells = private_statics(set(targets), funcs=funcs)
    out = []
    if cells is None:
        out.append(f"stand-in check: no unpacked image ({tsdb.unpacked_exe()}): planted statics not checked")
        cells = {}
    callees = {int(f["rva"], 16): {int(c, 16) for c in f.get("callees", ()) if c.startswith("0x")}
               for f in funcs if int(f["rva"], 16) in targets}
    ours = ours_cell_names({p: t for p, t in read_tree(branch, ("src/",)).items() if p.endswith((".cpp", ".h"))})
    hits = scan(tests, targets, cells, ours, callees)
    lines = report(hits, targets)
    if lines:
        out.append(f"{len(lines)} test file(s) stand in for or plant state of functions the branch makes ours on the "
                   "game's side only (docs/lane/test.md, Pitfalls); the full suite must pass:")
        out += ["  " + l for l in lines]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("branch", nargs="?")
    ap.add_argument("--base", default="main")
    ap.add_argument("--merge", help="a merge commit: check its second parent against its first")
    ap.add_argument("--tests-at", help="read tests/ from this revision instead (calibration: the fix that came later)")
    args = ap.parse_args()
    t0 = time.time()
    if args.merge:
        branch, base = f"{args.merge}^2", f"{args.merge}^1"
    elif args.branch:
        branch, base = args.branch, args.base
    else:
        ap.error("give a branch or --merge")
    for w in branch_check(branch, base, tests_at=args.tests_at):
        print("warning: " + w)
    print(f"({time.time() - t0:.1f} s)")


if __name__ == "__main__":
    main()
