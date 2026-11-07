#!/usr/bin/env python3
"""Which lines of a src/ file did a difftest filter never run? Line coverage of our code under the differential tests.
(tools/coverage.py is a different thing: in-game coverage of the original.)

  tools/difftest.ps1 -Config RelWithDebInfo -BuildOnly         # once; 12 minutes cold, then incremental
  tools/py -I tools/coverage_lines.py --filter ts_effects_manager src/ts/cTSEffectsManager.cpp
  ... --all                                                    # also the ranges hidden as destructors

Prints one line per missed range, with the range's first source line (`fn` before it: from a function's opening brace,
so read it as "inlined or byte-matched?" first); a clean file prints only its totals. Runs
OpenCppCoverage (local/tools, checked against the pinned hash) over a RelWithDebInfo difftest.exe, whose code is Release's.
It reads missed lines, not missed branches: docs/lane/test.md, "Line coverage", has what a hit and a miss mean.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path, PurePosixPath

OCC_RELATIVE = Path("local/tools/opencppcoverage-0.9.9.0/OpenCppCoverage.exe")
OCC_SHA256 = "81b901fd01bf8bcdc93b8622e2c2032225e465aeb30d33e8977e417388957acf"
BELOW_NORMAL_PRIORITY_CLASS = 0x4000


def main_root(here):
    out = subprocess.run(["git", "-C", str(here), "rev-parse", "--path-format=absolute", "--git-common-dir"],
                         capture_output=True, text=True, check=True).stdout.strip()
    return Path(out).parent


def parse_lines(xml_text, source):
    """{line: hits} for one source file (a path ending in `source`) of a cobertura report, the most hits of each line:
    a file can appear once per module. Cobertura's paths have no drive letter, so the match is on the path's end."""
    want = PurePosixPath(source.replace("\\", "/")).parts
    lines = {}
    for c in ET.fromstring(xml_text).iter("class"):
        parts = PurePosixPath(c.get("filename", "").replace("\\", "/")).parts
        if parts[-len(want):] != want:
            continue
        for line in c.iter("line"):
            n = int(line.get("number"))
            lines[n] = max(lines.get(n, 0), int(line.get("hits")))
    return lines


def missed_ranges(lines):
    """(first, last) of each run of missed lines; a line with no code between two missed lines doesn't end a run."""
    ranges = []
    for n in sorted(lines):
        if lines[n]:
            continue
        if ranges and not any(k in lines for k in range(ranges[-1][1] + 1, n)):
            ranges[-1] = (ranges[-1][0], n)
        else:
            ranges.append((n, n))
    return ranges


def is_destructor(source_lines, first, last):
    """A range inside a destructor's definition: the compiler's exception-unwind copy of an inlined destructor has line
    entries of its own and runs only if something throws, so these read as missed even when the destructor ran."""
    return last - first < 12 and any("~" in text for text in source_lines[max(first - 3, 0):first])


def is_whole_function(source_lines, first):
    """A range from a function's opening brace: nothing of that function's own copy ran. Often the code did run, inlined
    into an adapter or a caller; or the row is a byte match and has no test."""
    return any(text == "{" for text in source_lines[max(first - 2, 0):first])


def snippet(source_lines, first, last):
    """The first line of the range with code on it."""
    for text in source_lines[first - 1:last]:
        text = text.strip()
        if text not in ("", "{", "}"):
            return text
    return ""


def report(source, lines, source_lines, show_all=False, width=90):
    """The text for one file: a totals line, then a line per range (destructor ranges counted unless show_all)."""
    ranges = missed_ranges(lines)
    missed = sum(1 for hits in lines.values() if not hits)
    out = [f"{source}: {len(lines)} lines with code, {missed} missed"]
    if missed == len(lines):
        return out + ["  the filter never reaches this file"]
    hidden = 0
    for first, last in ranges:
        if not show_all and is_destructor(source_lines, first, last):
            hidden += 1
            continue
        where = f"{first}" if first == last else f"{first}-{last}"
        tag = "fn " if is_whole_function(source_lines, first) else ""
        out.append(f"  {where:<11} {tag}{snippet(source_lines, first, last)[:width]}")
    if hidden:
        out.append(f"  ({hidden} destructor range(s) hidden, usually exception-unwind copies; --all shows them)")
    return out


def summary_line(output):
    """The runner's totals line, or None if the run never reached it."""
    found = re.findall(r"^\d+ run, \d+ failed.*$", output, re.M)
    return found[-1] if found else None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sources", nargs="+", help="src/ files to report")
    ap.add_argument("--filter", required=True, help="difftest filter: test names or tests/ file stems")
    ap.add_argument("--all", action="store_true", help="also show ranges inside destructors")
    ap.add_argument("--timeout", type=int, default=1200, help="seconds (default 1200)")
    args = ap.parse_args(argv)

    root = Path(__file__).resolve().parents[1]
    main_dir = main_root(root)
    occ = main_dir / OCC_RELATIVE
    if not occ.exists():
        sys.exit(f"coverage_lines: no {occ}")
    if hashlib.sha256(occ.read_bytes()).hexdigest() != OCC_SHA256:
        sys.exit(f"coverage_lines: {occ} is not the pinned build")
    exe = root / "build" / "RelWithDebInfo" / "difftest.exe"
    if not exe.exists():
        sys.exit("coverage_lines: no RelWithDebInfo build: tools/difftest.ps1 -Config RelWithDebInfo -BuildOnly")
    images = sorted((main_dir / "local" / "original").glob("*.unpacked.exe"))
    if not images:
        sys.exit("coverage_lines: no unpacked image in local/original")
    paths = [(root / s).resolve() for s in args.sources]
    for s, path in zip(args.sources, paths):
        if not path.exists():
            sys.exit(f"coverage_lines: no {s}")
        if path.stat().st_mtime > exe.stat().st_mtime:
            print(f"warning: {s} is newer than difftest.exe (the line numbers may be off); rebuild first")

    env = dict(os.environ, DIFFTEST_NO_MONITOR="1")
    env["_NO_DEBUG_HEAP"] = "1"  # under a debugger Windows' debug heap fills memory, and some tests depend on what's there
    with tempfile.TemporaryDirectory(prefix="coverage_lines_") as tmp:  # OpenCppCoverage leaves its log in the cwd
        xml_path = Path(tmp) / "coverage.xml"
        cmd = [str(occ), "--quiet", "--export_type", f"cobertura:{xml_path}", "--modules", "difftest.exe",
               "--continue_after_cpp_exception"]
        for path in paths:
            cmd += ["--sources", str(path)]
        cmd += ["--", str(exe), "--image", str(images[0]), "--filter", args.filter]
        try:
            run = subprocess.run(cmd, cwd=tmp, env=env, capture_output=True, text=True, errors="replace",
                                 timeout=args.timeout, creationflags=BELOW_NORMAL_PRIORITY_CLASS)
        except subprocess.TimeoutExpired:
            sys.exit(f"coverage_lines: no result in {args.timeout} s: narrow the filter or raise --timeout")
        totals = summary_line(run.stdout)
        print(totals or f"the run stopped early (exit {run.returncode}); lines after it read as missed")
        if totals and totals.startswith("0 run"):
            sys.exit(f"coverage_lines: --filter {args.filter} selected no test")
        for line in run.stdout.splitlines():
            if line.startswith(("[fail]", "[error]")):
                print(line[:100])
        if not xml_path.exists():
            sys.exit(f"coverage_lines: no report (exit {run.returncode}): {run.stderr.strip()[:200]}")
        xml_text = xml_path.read_text(encoding="utf-8", errors="replace")
    for s, path in zip(args.sources, paths):
        lines = parse_lines(xml_text, path.relative_to(root).as_posix())
        if not lines:
            print(f"{s}: no line data (a header inlined everywhere, or the wrong path)")
            continue
        source_lines = path.read_text(encoding="utf-8", errors="replace").split("\n")
        print("\n".join(report(s, lines, source_lines, args.all)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
