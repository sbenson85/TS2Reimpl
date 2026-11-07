#!/usr/bin/env python3
"""tools/validate.sh's decision and its bounded snapshot keep, apart from the script so they can be tested with no game.

    tools/py -I tools/validate_result.py decide FACTS [LOTCHECK_OUTPUT]    # prints the RESULT line; exit 0 / 2 / 1
    tools/py -I tools/validate_result.py keep RUN SNAPSHOT                 # a run that wasn't a clean PASS

FACTS is the run's facts.txt, one key=value a line, the last value of a key winning. A missing fact counts against the
run: a step that left no exit code did not pass. What the results mean, and that QUALIFIED never authorises a merge by
itself: docs/orchestrator.md, "Validation results". How a lotcheck outcome maps to a result: docs/save-diff.md's outcome
table. Rerun `decide` on any run's folder to see how its result was reached. Tests: tools/tests/test_validate_result.py.
"""
import argparse
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import snapstore  # noqa: E402

PASS, QUALIFIED, FAIL = 0, 2, 1                 # validate.sh's exit codes
ACCEPTED = {0: "MATCH", 2: "MATCH-OPEN"}        # lotcheck exits (docs/save-diff.md): anything else is listed or fails
QUALIFYING = {3: "UNSEEN", 5: "DIAGNOSTIC"}
KEEP_SNAPSHOTS = 3                              # newest not-clean runs with a snapshot; a folder with a KEEP file is apart
MIN_FREE_GB = 5                                 # as smoke.py: below this nothing is kept


def read_facts(path):
    facts = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        key, sep, value = line.partition("=")
        if sep:
            facts[key] = value
    return facts


def rc(facts, key):
    """The fact as an int, None when it is missing or not a number."""
    try:
        return int(facts[key])
    except (KeyError, ValueError):
        return None


def lot_detail(text):
    """(state and match kind, fixture, reason) from lotcheck's report, each "" when the report doesn't say."""
    kind = fixture = why = ""
    for line in text.splitlines():
        m = re.match(r"(\S+): (MATCH-OPEN|MATCH|UNSEEN|UNEXPECTED|NO-FIXTURE)\b", line)
        if m:
            kind = m[1] + " " + m[2]
            f = re.search(r"\(fixture (.+?)(;.*)?\)$", line)
            fixture = f[1] if f else fixture
        f = re.match(r"closest fixture (.+) \(\d+ differences\)", line)
        fixture = f[1] if f else fixture
        if line.startswith("diagnostic only: "):
            why = line[len("diagnostic only: "):]
    varying = re.findall(r"^  vary  (.+?)(?: changed)?:", text, re.M)
    if not why and varying:
        why = (f"{len(varying)} difference{'s' if len(varying) != 1 else ''} only where reference runs differ among"
               f" themselves ({varying[0]}{', ...' if len(varying) > 1 else ''})")
    return kind, fixture, why


def lot_reasons(facts, lot_text):
    """(failures, qualifications) of the lot save step and its check."""
    fails, quals = [], []
    code = rc(facts, "lotsave_rc")
    if code != 0:
        fails.append(f"lotsave rc={code}")
    if not facts.get("snapshot"):
        return fails + ["lotsave: no snapshot written by this run"], quals
    check = rc(facts, "lotcheck_rc")
    kind, fixture, why = lot_detail(lot_text)
    if check in QUALIFYING:
        quals.append(f"{QUALIFYING[check]} (lotcheck rc={check}{'; ' + kind if kind else ''}):"
                     f" fixture {fixture or 'not named'}: {why or 'no reason in the report'}")
    elif check not in ACCEPTED:
        last = (lot_text.strip().splitlines() or [""])[-1]
        fails.append(f"lotcheck rc={check}: {kind or last or 'no report'}")
    return fails, quals


def decide(facts, lot_text=""):
    """(exit code, the RESULT line). FAIL beats QUALIFIED; PASS needs every step clean and an accepted lot outcome."""
    fails = []
    if rc(facts, "deploy_rc") != 0:
        fails.append(f"deploy rc={rc(facts, 'deploy_rc')}")

    code, summary = rc(facts, "smoke_rc"), facts.get("smoke_summary", "")
    m = re.search(r": (\d+)/(\d+) passed", summary)
    if code != 0 or not m or m[1] != m[2] or m[2] == "0":
        fails.append(f"smoke rc={code}: {summary or 'no summary'}")

    code, line = rc(facts, "difftest_rc"), facts.get("difftest_line", "")
    if code != 0 or not re.search(r"\d+ run, 0 failed", line):
        fails.append(f"difftest rc={code}: {line or 'no run line'}")

    if not facts.get("services_log"):
        fails.append("services: no log written by this run")
    else:
        if not facts.get("services_line"):
            fails.append("services: no services line")
        if rc(facts, "traps") != 0:
            fails.append(f"traps {facts.get('traps')}")
        pattern = facts.get("hook_pat", "")
        if pattern != "nothing" and (rc(facts, "hooks") or 0) <= 0:
            fails.append(f"hook pattern '{pattern}' not seen")
    if rc(facts, "services_rc") != 0:
        fails.append(f"services scenario rc={rc(facts, 'services_rc')}")

    lot_fails, quals = lot_reasons(facts, lot_text)
    fails += lot_fails
    if fails:
        return FAIL, "RESULT FAIL: " + "; ".join(fails)
    if quals:
        return QUALIFIED, "RESULT QUALIFIED: " + "; ".join(quals)
    return PASS, "RESULT PASS"


def prune(root, limit=KEEP_SNAPSHOTS):
    """Deletes the snapshot of all but the newest `limit` run folders (named by stamp) that hold one; a folder holding a
    file named KEEP is neither pruned nor counted. The evidence files stay. Returns the folders pruned."""
    held = sorted((d for d in Path(root).iterdir() if (d / "snapshot").is_dir() and not (d / "KEEP").exists()),
                  key=lambda d: d.name, reverse=True)
    for old in held[limit:]:
        shutil.rmtree(old / "snapshot", ignore_errors=True)
    return held[limit:]


def keep(run, snapshot, store=None, limit=KEEP_SNAPSHOTS, min_free_gb=MIN_FREE_GB):
    """Holds a not-clean run's snapshot against scenario.ps1's pruning of local/saves: run/snapshot, links into the
    store (free while the original folder lives, its changed files after), with the log and manifest beside it so that
    lotcheck can reread it. Returns the line for the step log; never fails the run."""
    run, snapshot = Path(run), Path(snapshot)
    free = shutil.disk_usage(run).free / 2**30
    if free < min_free_gb:
        return f"snapshot not kept: {free:.1f} GB free, under {min_free_gb} ({snapshot} goes with its normal pruning)"
    stores = snapstore.Store(store)
    stores.snap(snapshot, run / "snapshot")
    for suffix in (".ts2host.log", ".run.json"):
        beside = snapshot.with_name(snapshot.name + suffix)
        if beside.exists():
            shutil.copy2(beside, run / ("snapshot" + suffix))
    pruned = prune(run.parent, limit)
    if pruned:
        stores.gc()
    return f"snapshot kept: {run / 'snapshot'} (newest {limit} kept, {len(pruned)} older pruned)"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="action", required=True)
    d = sub.add_parser("decide")
    d.add_argument("facts")
    d.add_argument("lotcheck", nargs="?", help="lotcheck's full output, for the reason")
    k = sub.add_parser("keep")
    k.add_argument("run")
    k.add_argument("snapshot")
    a = ap.parse_args(argv)
    if a.action == "keep":
        print(keep(a.run, a.snapshot))
        return 0
    lot = Path(a.lotcheck) if a.lotcheck else None
    code, line = decide(read_facts(a.facts), lot.read_text(encoding="utf-8", errors="replace") if lot and lot.exists() else "")
    print(line)
    return code


if __name__ == "__main__":
    sys.exit(main())
