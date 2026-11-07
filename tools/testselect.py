"""Which difftests a --filter selects, and whether any of them can reach the code a mutant edits (static: reads tests/
and db/status/, no build).

    tools/py -I tools/testselect.py --filter RZString --file src/rz/cRZString.cpp [--function cRZString::Strlen]
    tools/py -I tools/testselect.py --filter RZString --mutants mutants.json

The selection mirrors harness/main.cpp: a comma-separated filter selects a test when a part is a substring of its name
(case-sensitive) or equals the stem of the file that registered it (case-insensitive, `.cpp` optional); `@file` names
tests one per line. Tests come from `DIFF_TEST(Name, rva)` in tests/ (.cpp and .h); a file that registers tests at run
time (`difftest::Registrar` with a built name: com_directors.cpp) is one "dynamic" entry: its file stem selects it, a
name part only "may" (matched against the name-shaped string literals in the file).

What a mutant edits (its target): the src file, and the function around the edit (tools/cppblocks.py) when a status
entry with that src file (or one of the same stem: src/x/Foo.h pairs with src/x/Foo.cpp and Foo.size.cpp) is named
after it (`ns::cFoo::Bar (slot 3 ...)` matches `cFoo::Bar`).
  - The function has status entries: a selected test must test one of their RVAs: its DIFF_TEST rva, a hex literal (or
    a constant naming one) in its block, or a literal in its file outside every DIFF_TEST block (a shared table or
    helper). Lane KY's coordinator mutants in cShaderCommand::EndBlock "survived" under --filter MaterialCommands:
    those tests live in the same test file but never call EndBlock.
  - Otherwise (a helper with no entry of its own, a header, an edit outside any function), a selected test must live
    in a test file a status entry pairs with the src file (its `test`, or a tests/ header that file includes), or have
    the RVA of such an entry.
tools/mutate.py runs this before building and refuses (exit 2) when the filter selects nothing or a target is reached
by nothing; --force-filter overrides (loudly).
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cppblocks  # noqa: E402
import tsdb  # noqa: E402

INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.M)
NAME_LITERAL_RE = re.compile(r'"([A-Z][A-Za-z0-9]*_\w*)"')
HEX_RE = re.compile(r"\b0[xX]([0-9a-fA-F]{4,8})\b")
CONST_RE = re.compile(r"\b([A-Za-z_]\w*)\s*(?:=|\{)\s*0[xX]([0-9a-fA-F]+)\b")


def _posix(p):
    return str(p).replace("\\", "/")


def unit_of(path):
    """A source file's unit: folder + stem before the first dot (src/x/Foo.size.cpp -> src/x/Foo)."""
    p = Path(_posix(path))
    return f"{_posix(p.parent)}/{p.name.split('.')[0]}"


def _hexes(text):
    return {hex(int(h, 16)) for h in HEX_RE.findall(text)}


def read_tests(root=None):
    root = Path(root or tsdb.ROOT)
    texts = {}
    for p in sorted((root / "tests").rglob("*")):
        if p.suffix in (".cpp", ".h") and p.is_file():
            texts[_posix(p.relative_to(root))] = p.read_text(encoding="utf-8", errors="replace")
    return texts


def scan_tests(root=None, texts=None):
    """Every test registered in tests/: [{name, file, rva (hex str or None), dynamic, literals, mentions, free}]:
    `mentions` the RVAs its block names (hex literals, constants), `free` the RVAs its file names outside every
    DIFF_TEST block. `texts` ({rel path: text}) replaces reading tests/ (tests of this tool)."""
    texts = read_tests(root) if texts is None else texts
    out = []
    for rel, raw in sorted(texts.items()):
        headers = [texts.get(_posix(Path(rel).parent / inc), "") for inc in INCLUDE_RE.findall(raw)]
        defs = "\n".join(h for h in headers if "DIFF_TEST" in h)
        if "DIFF_TEST" not in raw and "Registrar" not in raw and not defs:
            continue
        consts = {}
        for t in [raw] + headers:
            for c in CONST_RE.finditer(cppblocks.blank(t)):
                consts.setdefault(c.group(1), hex(int(c.group(2), 16)))
        b = cppblocks.blank(raw)
        tests = cppblocks.test_blocks(raw, defs)
        # RVAs count in comments too (a test of a whole class often lists what it covers in a comment); the comment
        # lines right above a DIFF_TEST belong to it.
        free_text = list(raw)
        spans = []
        for blk in tests:
            start = cppblocks.leading_comment_start(raw, blk.start)
            spans.append((start, blk.end))
            free_text[start:blk.end] = " " * (blk.end - start)
        free = _hexes("".join(free_text))
        for blk, (start, end) in zip(tests, spans):
            body = b[blk.start:blk.end]
            arg = blk.rva.strip()
            rva = None
            if re.fullmatch(r"0[xX][0-9a-fA-F]+[uU]?", arg):
                rva = hex(int(arg.rstrip("uU"), 16))
            elif re.fullmatch(r"[A-Za-z_]\w*", arg):
                rva = consts.get(arg)
            mentions = _hexes(raw[start:end]) | {consts[w] for w in set(re.findall(r"\b[A-Za-z_]\w*\b", body)) if w in consts}
            out.append({"name": blk.name, "file": rel, "rva": rva, "dynamic": False, "literals": [],
                        "mentions": mentions, "free": free})
        if re.search(r"\bRegistrar\s*\(", b) and "define DIFF_TEST" not in raw:
            out.append({"name": None, "file": rel, "rva": None, "dynamic": True,
                        "literals": sorted(set(NAME_LITERAL_RE.findall(raw))), "mentions": set(), "free": _hexes(raw)})
    return out


def file_stem(rel):
    name = Path(rel).name
    return name[:-4] if name.lower().endswith(".cpp") else name


def select(tests, filt, read=None):
    """[(test, how)] for the tests `filt` selects; how is "name contains 'X'", "file stem 'X'", "named in @file" or
    "may match 'X' (name built at run time)". `read` reads an @file (default: the file system)."""
    if filt.startswith("@"):
        names = {l.strip() for l in (read or (lambda p: Path(p).read_text(encoding="utf-8")))(filt[1:]).splitlines()
                 if l.strip()}
        return [(t, f"named in {filt}") for t in tests if t["name"] in names]
    parts = [p for p in filt.split(",") if p]
    out = []
    for t in tests:
        stem = file_stem(t["file"])
        how = None
        for part in parts:
            want = part[:-4] if part.lower().endswith(".cpp") and len(part) > 4 else part
            if stem.lower() == want.lower():
                how = f"file stem '{part}'"
                break
            if t["name"] is not None and part in t["name"]:
                how = f"name contains '{part}'"
                break
            if t["dynamic"] and any(part in lit or lit.startswith(part) or part.startswith(lit) for lit in t["literals"]):
                how = f"may match '{part}' (name built at run time)"
                break
        if how:
            out.append((t, how))
    return out


def load_entries(root=None):
    root = Path(root or tsdb.ROOT)
    entries = []
    for p in sorted((root / "db/status").glob("*.jsonl")):
        for line in p.read_text(encoding="utf-8").splitlines():
            if line.strip():
                entries.append(json.loads(line))
    return entries


_BY_UNIT = {}  # id(entries) -> (entries, {unit: [entry]})


def entries_of(entries, unit):
    """Status entries whose `src` (any of them, comma-separated) is in `unit`."""
    cached = _BY_UNIT.get(id(entries))
    if cached is None or cached[0] is not entries or cached[2] != len(entries):
        index = {}
        for e in entries:
            for u in {unit_of(s.strip()) for s in re.split(r"[,;]", e.get("src") or "") if s.strip()}:
                index.setdefault(u, []).append(e)
        cached = _BY_UNIT[id(entries)] = (entries, index, len(entries))
    return cached[1].get(unit, [])


def entry_qual(e):
    """A status entry's name without its parenthesised note: `ns::cFoo::Bar (slot 3: ...)` -> `ns::cFoo::Bar`."""
    return re.split(r"\s*[(\[]", e.get("name") or "", maxsplit=1)[0].strip()


def function_entries(entries, unit, qual):
    """Status entries (src in `unit`) named after function `qual` (cFoo::Bar matches ns::cFoo::Bar and cFoo::Bar)."""
    if not qual:
        return []
    out = []
    for e in entries_of(entries, unit):
        q = entry_qual(e)
        names = {q} | {n.strip() for n in q.split("/")}  # `cRZString::ToChar/Data`
        if any(n == qual or n.endswith("::" + qual) for n in names):
            out.append(e)
    return out


def targets_of(mutants, root=None, read=None):
    """[(file, function qual or None)] that the mutants edit, in order, without repeats. `read(rel)` gives a file's
    text (default: from disk under the root)."""
    root = Path(root or tsdb.ROOT)
    read = read or (lambda rel: (root / rel).read_bytes().decode("utf-8"))
    out = []
    for m in mutants:
        rel = rel_to_root(m["file"], root)
        try:
            text = read(rel)
        except OSError:
            continue
        for e in (m["edits"] if "edits" in m else [{"old": m["old"], "new": m["new"]}]):
            at = text.find(e["old"])
            qual = None
            if at >= 0:
                blk = cppblocks.enclosing(cppblocks.blocks(text), at)
                qual = blk.qual if blk else None
                text = text.replace(e["old"], e["new"], 1)
            if (rel, qual) not in out:
                out.append((rel, qual))
    return out


def includes_of(root, rel):
    """tests/ headers a test file includes (one level, by relative path)."""
    try:
        text = (Path(root) / rel).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return set()
    return {_posix(Path(rel).parent / inc) for inc in INCLUDE_RE.findall(text)}


_NAMED = {}  # id(tests) -> (tests, every RVA some test names)


def named_rvas(tests, file=None):
    """Every RVA some test names (in `file` only, if given)."""
    cached = _NAMED.get(id(tests))
    if cached is None or cached[0] is not tests:
        names, by_file = set(), {}
        for t in tests:
            n = t["mentions"] | t["free"] | ({t["rva"]} if t["rva"] else set())
            names |= n
            by_file.setdefault(t["file"], set()).update(n)
        cached = _NAMED[id(tests)] = (tests, names, by_file)
    return cached[1] if file is None else cached[2].get(file, set())


def reach_target(selected, target, entries, includes, tests=None):
    """(rule, [(test, why)]) for one (file, function) target; rule is "function ..." or "file ...". The function rule
    applies when the function has status entries and some test in `tests` (all of them) names one of their RVAs;
    otherwise the file rule."""
    f, qual = target
    unit = unit_of(f)
    fn = function_entries(entries, unit, qual)
    rvas = {e["rva"].lower() for e in fn}
    if fn and tests is not None and not rvas & named_rvas(tests):
        fn = []  # no test names it (tests that reach it through our vtable): only the file rule can say
        qual = f"{qual} ({', '.join(sorted(rvas))}, which no test names)"
    if fn:
        # The function's own test files that never name its RVAs (they reach it through our vtable or a caller):
        # any of their tests counts, as nothing says which one calls it.
        silent = {_posix(e["test"]) for e in fn if e.get("test")}
        silent = {f for f in silent if tests is not None and not rvas & named_rvas(tests, f)}
        hits = []
        for t, _ in selected:
            if t["file"] in silent:
                hits.append((t, f"{t['file']} is the test of {qual}'s status entry and names none of its RVAs"))
            elif t["rva"] in rvas:
                hits.append((t, f"its rva {t['rva']} is {qual}"))
            elif rvas & t["mentions"]:
                hits.append((t, f"its block names {', '.join(sorted(rvas & t['mentions']))} ({qual})"))
            elif rvas & t["free"]:
                hits.append((t, f"{t['file']} names {', '.join(sorted(rvas & t['free']))} ({qual}) outside its DIFF_TEST "
                                "blocks (a shared table or helper)"))
        return f"function {qual} ({', '.join(sorted(rvas))})", hits
    paired = entries_of(entries, unit)
    by_test = {}
    for e in paired:
        if e.get("test"):
            by_test.setdefault(_posix(e["test"]), []).append(e)
    by_rva = {e["rva"].lower(): e for e in paired}
    header_of = {}  # tests/ header -> a paired test file including it
    for tf in by_test:
        for h in includes(tf):
            header_of.setdefault(h, tf)
    hits = []
    for t, _ in selected:
        tf = t["file"]
        if t["rva"] and t["rva"] in by_rva:
            e = by_rva[t["rva"]]
            hits.append((t, f"its rva {t['rva']} is {entry_qual(e)} (src {e.get('src')})"))
        elif tf in by_test:
            es = by_test[tf]
            hits.append((t, f"{tf} is the test of {len(es)} status entr{'y' if len(es) == 1 else 'ies'} with src {unit}.*"))
        elif tf in header_of:
            hits.append((t, f"{tf} is included by {header_of[tf]}, the test of status entries with src {unit}.*"))
    why = (f" ({qual})" if "which no test names" in (qual or "") else f" (no status entry is named {qual})" if qual
           else " (the edit is outside any function)")
    return "file" + why, hits


def rel_to_root(path, root=None):
    root = Path(root or tsdb.ROOT).resolve()
    p = Path(path)
    p = p if p.is_absolute() else root / p
    try:
        return _posix(p.resolve().relative_to(root))
    except ValueError:
        return _posix(p)


SHOWN = 8


def check(filt, targets, root=None, tests=None, entries=None, includes=None, read=None):
    """(ok, lines, info) for a mutation run's --filter and targets [(file, function or None)]: ok is False when the
    filter selects no test or a target is reached by none. info: {"selected": [(test, how)], "reaching": {target:
    [(test, why)]}}."""
    root = Path(root or tsdb.ROOT)
    tests = scan_tests(root) if tests is None else tests
    entries = load_entries(root) if entries is None else entries
    includes = includes or (lambda rel: includes_of(root, rel))
    selected = select(tests, filt, read)
    info = {"selected": selected, "reaching": {}}
    if not selected:
        return False, [f"--filter {filt!r} selects no test in tests/ (a test-name substring, a tests/ file stem or "
                       "@file of names; names are case-sensitive)"], info
    static = [t for t, _ in selected if not t["dynamic"]]
    dynamic = [t["file"] for t, _ in selected if t["dynamic"]]
    lines = [f"--filter {filt!r} selects {len(static)} test(s) in {len({t['file'] for t, _ in selected})} file(s)"
             + (f" plus run-time registered tests in {', '.join(dynamic)}" if dynamic else "") + ":"]
    for t, how in selected[:SHOWN]:
        lines.append(f"  {t['name'] or '(dynamic)'} ({t['file']}, rva {t['rva'] or '?'}): {how}")
    if len(selected) > SHOWN:
        lines.append(f"  ... {len(selected) - SHOWN} more")
    ok = True
    for target in targets:
        rule, hits = reach_target(selected, target, entries, includes, tests)
        info["reaching"][target] = hits
        where = f"{target[0]}" + (f" {target[1]}" if target[1] else "")
        if hits:
            lines.append(f"{where}: reached by {len(hits)} selected test(s) [{rule}]:")
            lines += [f"  {t['name'] or '(dynamic) ' + t['file']}: {why}" for t, why in hits[:SHOWN]]
            if len(hits) > SHOWN:
                lines.append(f"  ... {len(hits) - SHOWN} more")
            continue
        ok = False
        others = [t for t, _ in reach_target([(t, "") for t in tests], target, entries, includes, tests)[1]]
        hint = (f"tests that would: {', '.join(t['name'] or t['file'] for t in others[:6])}"
                f"{' ...' if len(others) > 6 else ''}" if others else
                "no test in tests/ reaches it by these rules (a file no status entry records needs --force-filter, "
                "said in the report)")
        lines.append(f"{where}: no selected test reaches it [{rule}]; {hint}")
    return ok, lines, info


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--filter", required=True)
    ap.add_argument("--file", action="append", default=[], help="a src file a mutation would change (repeatable)")
    ap.add_argument("--function", help="with one --file: the function in it (cFoo::Bar)")
    ap.add_argument("--mutants", help="a tools/mutate.py mutants.json: its targets")
    args = ap.parse_args()
    targets = [(rel_to_root(f), args.function) for f in args.file]
    if args.mutants:
        targets += targets_of(json.loads(Path(args.mutants).read_text(encoding="utf-8")))
    ok, lines, _ = check(args.filter, targets)
    print("\n".join(lines))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
