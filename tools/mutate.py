"""Mutation check: apply each mutant to a source file, rebuild, run a difftest filter, restore.

    .venv/Scripts/python -I tools/mutate.py mutants.json --filter RZBlockPool [--timeout 180]

mutants.json is a list of {"file": "src/...", "old": "...", "new": "..."}; `old` must occur exactly
once; a mutant needing several edits in one file gives "edits": [{"old": ..., "new": ...}, ...]
instead (applied in order). A mutant is killed when the tests fail, error, crash or time out (a hang counts: the original
doesn't hang); one that doesn't build is reported as invalid, not killed. The source is restored and touched after every mutant, also on Ctrl+C or an error
(and from build/mutate-backup.json at the next start, or by `mutate.py --restore`, if the run was killed outright).
--filter is a test-name substring or a tests/ file stem, as difftest.ps1 -Filter; several filters are a comma list
(`--filter A,B`) or a repeated flag (`--filter A --filter B`, joined into `A,B`): a test counts when any part selects
it, for the reach check and the runs alike. Before building, the filter must
select tests that can reach what each mutant edits (tools/testselect.py: the function around the edit, by its status
entries' RVAs, else the src file by its entries' test files); it prints which tests count and why, and refuses (exit 2)
when the filter selects nothing or nothing reaching a target: a survivor under such a filter isn't one, and a kill
can't be trusted (lane KY's coordinator mutants). `--force-filter` runs anyway, said loudly in the output (say why in
the report: e.g. a caller's test reaches a helper that has no status entry).
Before any mutant, a clean run of the filter must pass (else nothing is counted); each mutant's run then times out
after 5x the clean run's time (at least 20 s, at most --timeout), so a hanging mutant costs about as much as a clean run.
Prints one line per mutant as it finishes, `N. [json i]` (N counts from 1 in the json's order, i is the mutant's
0-based index in the json list) and its "name" if it has one, with the tests that failed (or the test the runner died in, or the last
test that finished before a hang), then the totals and a last line `MUTATE DONE` (also after "not applicable" or a
refused filter, which exit 2 before changing anything; a wait loop can stop on it); exits 1 if any mutant survived.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import testselect  # noqa: E402
import checkout_lock  # noqa: E402
import tsdb  # noqa: E402

ROOT = tsdb.ROOT


def build():
    r = subprocess.run(["powershell", "-NoProfile", "-File", str(ROOT / "tools/difftest.ps1"), "-BuildOnly"],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode == 4:  # difftest.ps1 refused below its free-space floor: nothing was built (never a pass or kill)
        return [r.stdout.strip() or "difftest.ps1 refused: free-space floor"]
    errors = [l for l in r.stdout.splitlines() if " error " in l or "error C" in l or "error LNK" in l]
    return errors


RESULT_LINE = re.compile(r"^\[(pass|fail|error)\] (\S+) \(rva")
DIED_LINE = re.compile(r"^difftest\.exe died in test (\S+)")


def which_tests(out):
    """What the runner's output says about each test: {"ran": [names], "failed": [names], "died": name or None,
    "last": the last test that finished}."""
    ran, failed, died = [], [], None
    for line in (out or "").splitlines():
        m = RESULT_LINE.match(line)
        if m:
            ran.append(m.group(2))
            if m.group(1) != "pass":
                failed.append(m.group(2))
        m = DIED_LINE.match(line)
        if m:
            died = m.group(1)
    return {"ran": ran, "failed": failed, "died": died, "last": ran[-1] if ran else None}


def default_jobs():
    """difftest.ps1's -Jobs default: half the logical processors."""
    return max(1, (os.cpu_count() or 2) // 2)


def write_retrying(path, data, attempts=20):
    """Windows can refuse the open for a moment (EINVAL or a sharing error) while something else has just had the file:
    XG's run stopped so, with a mutant left in src/."""
    for attempt in range(attempts):
        try:
            path.write_bytes(data)
            return
        except OSError:
            if attempt == attempts - 1:
                raise
            time.sleep(0.25)


def run_tests(filt, timeout, jobs=1):
    """(outcome, detail, which_tests(output)). jobs > 1: the tests over that many runner processes (difftest.exe --jobs,
    harness/shards.h: one run's output; a timeout kills them all, as the runner holds them in a job object)."""
    exe = ROOT / "build/Release/difftest.exe"
    image = tsdb.unpacked_exe()
    cmd = [str(exe), "--image", str(image), "--filter", filt]
    if jobs > 1:
        cmd += ["--jobs", str(jobs), "--times", str(tsdb.main_root() / "local/difftest-times.txt")]
    try:
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired as e:  # run() has already killed this run's process (only that one)
        partial = e.stdout.decode("utf-8", "replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        return "timeout", f"no result after {timeout:.0f}s", which_tests(partial)
    out = (r.stdout or "").strip()
    last = out.splitlines()[-1] if out else ""
    if "NO TESTS MATCHED" in out:  # the runner's exit 3: a filter selecting nothing must never count as a kill
        return "no tests", last, which_tests(out)
    return ("failed" if r.returncode else "passed"), last, which_tests(out)


def killed_by(outcome, tests):
    """The tests a kill is owed to, for the mutant's line."""
    if outcome == "timeout":
        return f"hung after {tests['last']}" if tests.get("last") else "hung before any test finished"
    names = list(tests.get("failed", []))
    if tests.get("died"):
        names.append(f"{tests['died']} (runner died)")
    if not names:
        return "no test named (exit code only)"
    return ", ".join(names[:6]) + (f" (+{len(names) - 6})" if len(names) > 6 else "")


def edits_of(m):
    return m["edits"] if "edits" in m else [{"old": m["old"], "new": m["new"]}]


def apply(text, m):
    """The text with mutant m's edits applied in order; ValueError if an edit doesn't match exactly once. In a CRLF
    file a JSON newline matches the file's line ending."""
    crlf = "\r\n" in text
    for e in edits_of(m):
        if crlf and "\r" not in e["old"]:
            e = {k: e[k].replace("\n", "\r\n") for k in ("old", "new")}
        if text.count(e["old"]) != 1:
            raise ValueError(f"{m['file']}: {text.count(e['old'])} matches (needs exactly one) for {e['old'][:60]!r}")
        text = text.replace(e["old"], e["new"])
    return text


TIMEOUT_FACTOR = 5  # a mutant's run may take this many times the clean run...
TIMEOUT_FLOOR = 20  # ...but never less than this many seconds (timing noise, a cold image cache)


def mutant_timeout(clean_seconds, upper):
    """Seconds one mutant's test run may take: TIMEOUT_FACTOR x the clean run, at least TIMEOUT_FLOOR, at most `upper`
    (--timeout). A mutant that hangs is killed after that instead of after the full --timeout."""
    return min(float(upper), max(float(TIMEOUT_FLOOR), TIMEOUT_FACTOR * clean_seconds))


BACKUP = ROOT / "build" / "mutate-backup.json"


def restore_backup():
    """Undo a previous run that was killed before it could restore its sources."""
    if not BACKUP.exists():
        return
    for name, text in json.loads(BACKUP.read_text(encoding="utf-8")).items():
        path = Path(name)
        if path.read_bytes() != text.encode("utf-8"):
            print(f"restoring {path} from a killed run", flush=True)
            path.write_bytes(text.encode("utf-8"))
            os.utime(path, None)
    BACKUP.unlink()


def main():
    if sys.argv[1:] == ["--restore"]:  # undo a stopped run's mutant without starting another (lane QG)
        restore_backup()
        print("restored" if not BACKUP.exists() else "backup still present", flush=True)
        return
    tsdb.lower_lane_priority()  # the rebuilds and test runs of a mutation run in a lane
    ap = argparse.ArgumentParser()
    ap.add_argument("mutants")
    ap.add_argument("--filter", required=True, action="append",
                    help="a test-name substring or a tests/ file stem (`.cpp` optional), as difftest.ps1 -Filter; "
                         "several: comma-separated or the flag repeated (joined with commas: one filter for the reach "
                         "check and every run); `@file` (test names, one per line) only alone")
    ap.add_argument("--timeout", type=int, default=180,
                    help=f"upper bound in seconds per test run; each mutant gets {TIMEOUT_FACTOR}x the clean run's time "
                         f"(at least {TIMEOUT_FLOOR}s) up to this")
    ap.add_argument("--jobs", type=int, default=default_jobs(),
                    help="runner processes per test run (difftest.exe --jobs; default half the logical processors, as "
                         "difftest.ps1 -Jobs); 1 runs one process, as before")
    ap.add_argument("--log", help="also append each result line to this file as it is known (follow a background run)")
    ap.add_argument("--force-filter", action="store_true",
                    help="run even when the filter selects no test that can reach a mutated function or file (the "
                         "output says FORCED; the report must say why the tests reach the code)")
    args = ap.parse_args()
    parts = [p.strip() for f in args.filter for p in f.split(",") if p.strip()]
    if any(p.startswith("@") for p in parts) and len(parts) > 1:
        ap.error("--filter @file can't be combined with other filters (put every test name in the file)")
    if not parts:
        ap.error("--filter is empty")
    args.filter = ",".join(parts)

    def say(line):
        print(line, flush=True)
        if args.log:
            with open(args.log, "a", encoding="utf-8") as f:
                f.write(line + "\n")
    restore_backup()
    mutants = json.loads(Path(args.mutants).read_text(encoding="utf-8"))
    originals = {}
    bad = []
    for i, m in enumerate(mutants):  # check them all before changing anything
        path = ROOT / m["file"]
        originals.setdefault(path, path.read_bytes())
        try:
            apply(originals[path].decode("utf-8"), m)
        except ValueError as e:
            bad.append(f"mutant {i + 1} [json {i}]{' ' + m['name'] if m.get('name') else ''}: {e}")
    if bad:
        say("not applicable:\n  " + "\n  ".join(bad))
        say("MUTATE DONE")
        sys.exit(2)
    # The filter must select tests that can reach what each mutant edits (tools/testselect.py), before any build.
    targets = testselect.targets_of(mutants, ROOT)
    ok, lines, info = testselect.check(args.filter, targets, ROOT)
    for line in lines:
        say(line)
    if not ok and not args.force_filter:
        say(f"refused: --filter {args.filter!r} selects no test that can reach the code above, so a survivor wouldn't "
            "be one and a kill couldn't be trusted. Use a filter naming the tests listed (several: `--filter A,B` "
            "or `--filter A --filter B`), or --force-filter if you know a test reaches it (say why in your report). Nothing was built.")
        say("MUTATE DONE")
        sys.exit(2)
    if not ok:
        say("=" * 100)
        say(f"FORCED (--force-filter): --filter {args.filter!r} has no test that statically reaches the code above. "
            "Kills and survivors below count only with the report's reason why these tests reach it.")
        say("=" * 100)
    # The clean run: the filter must select tests that pass unmutated (else every mutant counts as killed), and its
    # time sets each mutant's timeout.
    errors = build()
    if errors:
        say(f"the unmutated source doesn't build: {errors[0][:100]}")
        say("MUTATE DONE")
        sys.exit(2)
    start = time.time()
    outcome, detail, ran = run_tests(args.filter, args.timeout, jobs=args.jobs)
    clean = time.time() - start
    if outcome != "passed":
        say(f"clean run {outcome} ({detail}): --filter {args.filter!r} must select passing tests; stopped (nothing counted)")
        say("MUTATE DONE")
        sys.exit(2)
    timeout = mutant_timeout(clean, args.timeout)
    expected = {t["name"] for t, _ in info.get("selected", []) if t["name"]}
    missing = sorted(expected - set(ran.get("ran", [])))
    say(f"clean run passed in {clean:.0f}s ({len(ran.get('ran', []))} tests ran): each mutant's run times out after {timeout:.0f}s"
        + (f"; selected by the static scan but not run: {', '.join(missing[:5])}{' ...' if len(missing) > 5 else ''}"
           if missing and ran.get("ran") else ""))
    BACKUP.parent.mkdir(exist_ok=True)
    BACKUP.write_text(json.dumps({str(p): d.decode("utf-8") for p, d in originals.items()}), encoding="utf-8")
    survived = invalid = 0
    try:
        for i, m in enumerate(mutants):
            path = ROOT / m["file"]
            write_retrying(path, apply(originals[path].decode("utf-8"), m).encode("utf-8"))
            start = time.time()
            errors = build()
            tests = {}
            if errors:
                outcome, detail = "build error", errors[0][:100]
            else:
                outcome, detail, tests = run_tests(args.filter, timeout, jobs=args.jobs)
            if outcome == "no tests":
                say(f"--filter {args.filter!r} selects no test: stopped (nothing counted)")
                say("MUTATE DONE")
                sys.exit(2)  # finally restores the sources
            write_retrying(path, originals[path])
            os.utime(path, None)
            verdict = "SURVIVED" if outcome == "passed" else "INVALID" if outcome == "build error" else "killed"
            survived += outcome == "passed"
            invalid += outcome == "build error"
            label = m.get("name") or edits_of(m)[-1]["new"].strip().replace("\n", " ")[:60]
            by = f" <- {killed_by(outcome, tests)}" if verdict == "killed" else ""
            say(f"{i + 1:2}. [json {i}] {verdict:8} ({outcome}, {time.time() - start:.0f}s) {label}{by}")
    finally:
        for path, data in originals.items():
            if path.read_bytes() != data:
                write_retrying(path, data)
                os.utime(path, None)
        BACKUP.unlink(missing_ok=True)
        build()
    say(f"{len(mutants)} mutants, {len(mutants) - survived - invalid} killed, {survived} survived, {invalid} invalid (didn't build)"
        + ("; FORCED filter (--force-filter): no selected test statically reaches the mutated code" if not ok else ""))
    say("MUTATE DONE")  # fixed end marker for wait loops (the line above varies)
    sys.exit(1 if survived or invalid else 0)


if __name__ == "__main__":
    # One mutation run (or validation, or deploy) per checkout at a time: a second run would restore or overwrite the
    # first's mutant mid-run (tools/checkout_lock.py; the lock lives in this checkout's build/).
    try:
        with checkout_lock.held("mutate.py " + " ".join(sys.argv[1:])):
            main()
    except RuntimeError as e:
        if not str(e).startswith("checkout busy"):
            raise
        print(e, flush=True)
        sys.exit(3)
