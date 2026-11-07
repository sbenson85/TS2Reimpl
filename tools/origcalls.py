"""Original functions our code still calls (OriginalFunction<Fn, 0xRVA>() in src/; src/game/original.h).

Usage: python -I tools/origcalls.py [--check] [-v]
  --check  only validate (exit 1 on problems)
  -v       list every call site

Rules (CLAUDE.md "Original callees"), checked for every use in src/:
  - the RVA is a hex literal and a function start (db/functions.jsonl and friends);
  - not library code (db/library-matches.jsonl, crt-equivalents): call the toolchain's instead;
  - not the calling function itself (no wrapping);
  - declared: some db/status entry whose "src" is that file lists the RVA in "original_calls", and
    every declared RVA is used in that file.
A callee that has a replacement by now is reported (warning): switch the call to ours.
--stale lists every status entry, in any group, whose original_calls still name an RVA that has a replacement by now
(status.py --check prints each as a note).
Without --check, prints each original callee with its size and the replacements that call it: the
binary dependencies left in code we own.
"""
import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

USE = re.compile(r"\bOriginal(?:Function|Address)\s*<")


def norm(rva):
    return hex(int(rva, 16))


def uses(text):
    """[(line, rva or None)] for every OriginalFunction<Fn, 0xRVA>() in text. The template arguments may
    themselves contain angle brackets and commas, so the matching '>' and the top-level comma are found
    by counting. rva is None when the last argument isn't a hex literal or the call has no '()'
    (including the old OriginalFunction<Fn>(0xRVA) form)."""
    out = []
    for m in USE.finditer(text):
        i, depth, last_comma = m.end(), 1, None
        nest = 0  # (), [] nesting
        while i < len(text) and depth:
            c = text[i]
            if c in "([":
                nest += 1
            elif c in ")]":
                nest -= 1
            elif nest == 0:
                if c == "<":
                    depth += 1
                elif c == ">":
                    depth -= 1
                elif c == "," and depth == 1:
                    last_comma = i
            i += 1
        rva = None
        if last_comma is not None and depth == 0:
            arg = re.fullmatch(r"\s*(0x[0-9a-fA-F]+)\s*", text[last_comma + 1:i - 1])
            if arg and re.match(r"\s*\(\s*\)", text[i:]):
                rva = norm(arg.group(1))
        out.append((text.count("\n", 0, m.start()) + 1, rva))
    return out


def scan(files):
    """files: {path: text}. Returns {path: [(line, rva or None)]} for files that use OriginalFunction."""
    found = {}
    for path, text in files.items():
        if path.replace("\\", "/") == "src/game/original.h":
            continue
        u = uses(text)
        if u:
            found[path.replace("\\", "/")] = u
    return found


def check(found, entries, funcs=None, library=None):
    """Problems and warnings for call sites `found` against status entries [(file, entry)]."""
    funcs = funcs if funcs is not None else {f["rva"]: f for f in tsdb.shared_functions()}
    library = library if library is not None else tsdb.library_rvas() - tsdb.third_party_rvas()
    problems, warnings = [], []
    declared = defaultdict(set)    # src file -> declared rvas
    replaced = {}
    for f, e in entries:
        src = e.get("src", "").replace("\\", "/")
        if e.get("status") in tsdb.OURS:
            replaced[norm(e["rva"])] = e
        calls = e.get("original_calls", [])
        if not isinstance(calls, list):
            problems.append(f"{f}: {e['rva']}: original_calls must be a list of RVAs")
            continue
        for c in calls:
            c = norm(c)
            declared[src].add(c)
            if c == norm(e["rva"]):
                problems.append(f"{f}: {e['rva']} {e.get('name')}: declares a call to its own original (wrapping)")
    for path, sites in sorted(found.items()):
        used = set()
        for line, rva in sites:
            where = f"{path}:{line}"
            if rva is None:
                problems.append(f"{where}: OriginalFunction needs a hex literal RVA")
                continue
            used.add(rva)
            if rva not in funcs:
                problems.append(f"{where}: {rva} is not a function start")
            if rva in library:
                problems.append(f"{where}: {rva} is library code: call the toolchain's function instead")
            if rva in replaced:
                if replaced[rva].get("src", "").replace("\\", "/") == path:
                    problems.append(f"{where}: {rva} is implemented in this file: call ours (no wrapping)")
                else:
                    warnings.append(f"{where}: {rva} {replaced[rva]['name']} is {replaced[rva]['status']} now "
                                    f"({replaced[rva]['src']}): switch to it")
            if rva not in declared[path]:
                problems.append(f"{where}: {rva} not declared in original_calls of a status entry with src {path}")
        for rva in sorted(declared[path] - used):
            problems.append(f"{path}: original_calls declares {rva} but the file never calls it")
    for path in sorted(set(declared) - set(found)):
        for rva in sorted(declared[path]):
            problems.append(f"{path}: original_calls declares {rva} but the file never calls it")
    return problems, warnings


def stale_declarations(entries):
    """Declared original_calls (in any group) naming an RVA that has a replacement by now (status reimplemented,
    replaced, trace-verified or verified): [(file, entry, rva, replacement entry)]. The entry should call ours."""
    replaced = {norm(e["rva"]): e for _, e in entries if e.get("status") in tsdb.OURS and e.get("rva")}
    out = []
    for f, e in entries:
        calls = e.get("original_calls", [])
        if not isinstance(calls, list):
            continue
        for c in calls:
            r = replaced.get(norm(c))
            if r is not None and norm(c) != norm(e["rva"]):
                out.append((f, e, norm(c), r))
    return out


def stale_lines(entries):
    return [f"{f}: {e['rva']} {e.get('name')} declares original_calls {c}, now {r['status']} as {r.get('name')} "
            f"({r.get('src')}): call ours and drop the declaration" for f, e, c, r in stale_declarations(entries)]


def source_files(root=None):
    root = Path(root or tsdb.ROOT)
    return {str(p.relative_to(root)).replace("\\", "/"): p.read_text(encoding="utf-8", errors="replace")
            for p in (root / "src").rglob("*") if p.suffix in (".cpp", ".h")}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--stale", action="store_true",
                    help="list status entries (any group) whose original_calls name an RVA that has a replacement")
    args = ap.parse_args()
    entries = tsdb.status_entries()
    if args.stale:
        lines = stale_lines(entries)
        for line in lines:
            print(line)
        print(f"{len(lines)} stale original_calls declaration(s)")
        sys.exit(1 if lines else 0)
    funcs = {f["rva"]: f for f in tsdb.functions()}
    found = scan(source_files())
    problems, warnings = check(found, entries, funcs)
    for w in warnings:
        print("warning: " + w, file=sys.stderr)
    for p in problems:
        print("problem: " + p, file=sys.stderr)
    if not args.check:
        names = {n["rva"]: n["name"] for n in tsdb.jsonl("names.jsonl")}
        callers = defaultdict(set)
        for path, sites in found.items():
            for line, rva in sites:
                if rva:
                    callers[rva].add(path if not args.v else f"{path}:{line}")
        total = sum(funcs.get(r, {}).get("size", 0) for r in callers)
        print(f"{len(callers)} original functions called from src/ ({total:,} bytes of original code)")
        for rva in sorted(callers, key=lambda r: -funcs.get(r, {}).get("size", 0)):
            f = funcs.get(rva, {})
            print(f"  {rva:>9}  {f.get('size', 0):6}B  {names.get(rva, f.get('name', '?'))[:50]:50}  "
                  f"<- {', '.join(sorted(callers[rva]))[:90]}")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
