"""Checks the append-only JSONL files git merges with the union driver (.gitattributes `merge=union`).

    tools/py -I tools/annotations_check.py                    # the checkout's files (after a merge: run this)
    tools/py -I tools/annotations_check.py --rev main         # the files as committed at a revision
    tools/py -I tools/annotations_check.py --merge <branch> [--base main]   # the union merge of <branch> into <base>,
                                                              # computed (git merge-file --union), nothing written
    tools/py -I tools/annotations_check.py --fix              # drop exact duplicate lines (keeps the first)

The union driver resolves two branches that both append to a file by keeping both sides' lines, so lane merges no longer
stop on db/annotations.jsonl. What it can't do is notice that both sides changed the same entry: then both versions
stay. Per file (the list is .gitattributes' `merge=union` entries):
  error    a line that isn't a JSON object; a conflict marker (<<<<<<<, =======, >>>>>>>); an exact duplicate line; a
           missing final newline (the next union would glue two lines)
  error    two entries with the same key that disagree (db/annotations.jsonl and db/methods/*.jsonl: the key
           tools/ghidra_ledger.py replaces entries by: kind + rva for names and functions, the structure's name, the
           interface + slot): keep the right one by hand, then the coordinator's `ghidra.py reload --force`
  warning  the same for keys already duplicated on main before 2026-10-23 (KNOWN: 0x1e3c70, 0x1c48da names), and for
           notes/lane-records.jsonl entries of one lane that disagree (a corrected record kept twice)
Exit 1 on any error. tools/guard.py runs check_merge() on a branch: problems the union merge with main would add.
"""
import argparse
import json
import re
import subprocess
import sys
import tempfile
from fnmatch import fnmatch
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ghidra_ledger  # noqa: E402
import tsdb  # noqa: E402

ROOT = tsdb.ROOT
# Disagreeing duplicates already in db/annotations.jsonl when this check was written (warnings, not errors).
KNOWN = {("db/annotations.jsonl", ("name", "0x1e3c70")), ("db/annotations.jsonl", ("name", "0x1c48da"))}
MARKER = re.compile(r"^(<{7}|={7}|>{7}|\|{7})( |$)")


def union_patterns(text=None):
    """The `merge=union` patterns of .gitattributes (the checkout's unless `text` is given)."""
    if text is None:
        path = ROOT / ".gitattributes"
        text = path.read_text(encoding="utf-8") if path.exists() else ""
    out = []
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 2 and not parts[0].startswith("#") and "merge=union" in parts[1:]:
            out.append(parts[0])
    return out


def is_union(path, patterns):
    return any(fnmatch(path, p) for p in patterns)


def entry_key(path, rec):
    """The key two entries of `path` must not share with different content, or None (no key for this file)."""
    if path == "db/annotations.jsonl":
        return ghidra_ledger.key("annotations.jsonl", rec)
    if path.startswith("db/methods/"):
        return ghidra_ledger.key("methods/" + Path(path).name, rec)
    if path == "notes/lane-records.jsonl":
        return ("lane", rec.get("lane"))
    return None


def problems(path, text):
    """[(severity, ident, message)] for one file's text; ident leaves line numbers out (to compare two versions)."""
    out = []
    if text and not text.endswith("\n"):
        out.append(("error", ("no final newline",), "no final newline (a union merge would glue two lines)"))
    seen_line = {}
    by_key = {}
    for n, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        if MARKER.match(line):
            out.append(("error", ("marker", line[:7]), f"line {n}: conflict marker {line[:20]!r}"))
            continue
        if line in seen_line:
            out.append(("error", ("duplicate", line), f"line {n} repeats line {seen_line[line]} exactly: {line[:100]}"))
            continue
        seen_line[line] = n
        try:
            rec = json.loads(line)
        except ValueError as e:
            out.append(("error", ("json", line), f"line {n}: not JSON ({e.msg}): {line[:100]}"))
            continue
        if not isinstance(rec, dict):
            out.append(("error", ("json", line), f"line {n}: not a JSON object: {line[:100]}"))
            continue
        try:
            key = entry_key(path, rec)
        except (KeyError, TypeError):
            out.append(("error", ("key", line), f"line {n}: entry without its key fields: {line[:100]}"))
            continue
        if key is None:
            continue
        if key in by_key:
            first = by_key[key]
            known = (path, key) in KNOWN or path == "notes/lane-records.jsonl"
            out.append(("warning" if known else "error", ("key", key),
                        f"lines {first} and {n} share the key {key} and disagree"
                        + (" (known before the union driver)" if (path, key) in KNOWN else "")
                        + ("" if known else ": keep the right one (both branches changed this entry)")))
        else:
            by_key[key] = n
    return out


def git(*a, cwd=None):
    return subprocess.run(["git", *a], cwd=cwd or ROOT, capture_output=True, text=True, encoding="utf-8",
                          check=True).stdout


def show(rev, path):
    try:
        return git("show", f"{rev}:{path}")
    except subprocess.CalledProcessError:
        return None


def union_merge(base, ours, theirs):
    """What git's union driver produces for three versions of a file (git merge-file --union)."""
    with tempfile.TemporaryDirectory() as d:
        files = []
        for name, text in (("ours", ours), ("base", base), ("theirs", theirs)):
            p = Path(d) / name
            p.write_bytes((text or "").encode("utf-8"))
            files.append(str(p))
        r = subprocess.run(["git", "merge-file", "-p", "--union", *files], capture_output=True)
        return r.stdout.decode("utf-8")


def tracked(rev, patterns):
    names = git("ls-tree", "-r", "--name-only", rev, "db/", "notes/").split()
    return [n for n in names if is_union(n, patterns)]


def check_merge(branch, base="main", paths=None, patterns=None):
    """(errors, warnings) the union merge of `branch` into `base` adds to the union files the branch changes (`paths`:
    those files; default: every union file that differs from the merge base). A problem the branch's own file has is
    an error (fix it on the branch); one only the merge makes is a warning (fix it after merging, then rerun this)."""
    if patterns is None:  # either side's .gitattributes (the branch that adds the file counts too)
        patterns = sorted(set(union_patterns(show(base, ".gitattributes") or "")
                              + union_patterns(show(branch, ".gitattributes") or ""))) or union_patterns()
    mb =git("merge-base", base, branch).strip()
    if paths is None:
        changed = git("diff", "--name-only", mb, branch).split()
        paths = [p for p in changed if is_union(p, patterns)]
    errors, warnings = [], []
    for path in paths:
        old, ours, theirs = show(mb, path), show(base, path), show(branch, path)
        if theirs is None:
            continue
        merged = union_merge(old, ours, theirs) if ours is not None else theirs
        # A line one side deleted next to lines the other side added lands in a conflict hunk, and union keeps it.
        base_lines, merged_lines = set((old or "").splitlines()), set(merged.splitlines())
        for side, text in ((base, ours), (branch, theirs)):
            back = sorted((base_lines - set((text or "").splitlines())) & merged_lines)
            for line in back[:10]:
                warnings.append(f"{path}: {side} deleted this line and the union merge keeps it: delete it again after "
                                f"merging: {line[:120]}")
        before = {p[1] for p in problems(path, ours or "")}
        mine = {p[1] for p in problems(path, theirs)}
        for severity, ident, msg in problems(path, merged):
            if ident in before:
                continue
            if severity == "warning":
                warnings.append(f"{path}: {msg} (after merging)")
            elif ident in mine:
                errors.append(f"{path}: {msg} (in the branch's own file)")
            else:
                warnings.append(f"{path}: the union merge with {base} will have: {msg}; fix it after merging and rerun "
                                "tools/annotations_check.py")
    return errors, warnings


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--rev", help="check the files as committed at this revision")
    g.add_argument("--merge", metavar="BRANCH", help="check the union merge of BRANCH into --base")
    g.add_argument("--fix", action="store_true", help="drop exact duplicate lines in the checkout's files")
    ap.add_argument("--base", default="main")
    args = ap.parse_args()
    if args.merge:
        errors, warnings = check_merge(args.merge, args.base)
        for w in warnings:
            print("warning: " + w)
        for e in errors:
            print("ERROR: " + e)
        print(f"union merge of {args.merge} into {args.base}: {len(errors)} errors, {len(warnings)} warnings")
        sys.exit(1 if errors else 0)
    patterns = union_patterns(show(args.rev, ".gitattributes")) if args.rev else union_patterns()
    if args.rev:
        files = {p: show(args.rev, p) for p in tracked(args.rev, patterns)}
    else:
        files = {}
        for pat in patterns:
            for p in sorted(ROOT.glob(pat)):
                files[p.relative_to(ROOT).as_posix()] = p.read_bytes().decode("utf-8")
    if args.fix:
        for path, text in files.items():
            seen, kept = set(), []
            for line in text.splitlines(keepends=True):
                if line.strip() and line in seen:
                    continue
                seen.add(line)
                kept.append(line)
            if len(kept) != len(text.splitlines(keepends=True)):
                (ROOT / path).write_bytes("".join(kept).encode("utf-8"))
                print(f"{path}: dropped {len(text.splitlines()) - len(kept)} duplicate line(s)")
        files = {p: (ROOT / p).read_bytes().decode("utf-8") for p in files}
    n_err = n_warn = 0
    for path, text in files.items():
        for severity, _, msg in problems(path, text):
            print(f"{'ERROR' if severity == 'error' else 'warning'}: {path}: {msg}")
            n_err += severity == "error"
            n_warn += severity == "warning"
    print(f"{len(files)} union-merged files checked: {n_err} errors, {n_warn} warnings")
    sys.exit(1 if n_err else 0)


if __name__ == "__main__":
    main()
