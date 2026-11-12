"""Hand out a fresh lane name and reserve it.

    .venv/Scripts/python -I tools/lanename.py ["what the lane does"] [--date 2026-11-08] [--dry-run]

Names are the start date and a letter for that day, YYMMDD + a..z (261108a, 261108b, ...): unique by construction,
in time order, and never picked by hand (two-letter names were reused by mistake in 2026-11; notes/log.md 11-08).
Older lanes keep their two-letter names. A name counts as taken when notes/feedback/<name>.md exists, a branch
mentions it, or db/annotations.jsonl records it as a lane. Reserving writes notes/feedback/<name>.md with a heading:
the coordinator commits it with the launch (by path), and the lane fills it in.
"""
import argparse
import datetime
import json
import os
import string
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FEEDBACK = os.path.join(ROOT, "notes", "feedback")


def taken():
    names = {os.path.splitext(f)[0].lower() for f in os.listdir(FEEDBACK)}
    r = subprocess.run(["git", "branch", "-a", "--format=%(refname:short)"], cwd=ROOT, capture_output=True, text=True)
    branches = r.stdout.lower()
    path = os.path.join(ROOT, "db", "annotations.jsonl")
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            try:
                lane = json.loads(line).get("lane")
            except ValueError:
                continue
            if isinstance(lane, str):
                names.add(lane.lower())
    return names, branches


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("what", nargs="?", default="", help="one line for the feedback file's heading")
    ap.add_argument("--date", help="start date, YYYY-MM-DD (default: today, UTC)")
    ap.add_argument("--dry-run", action="store_true", help="print the name, reserve nothing")
    a = ap.parse_args()
    day = (datetime.date.fromisoformat(a.date) if a.date
           else datetime.datetime.now(datetime.timezone.utc).date())
    names, branches = taken()
    for letter in string.ascii_lowercase:
        name = day.strftime("%y%m%d") + letter
        if name not in names and name not in branches:
            break
    else:
        sys.exit(f"all 26 names for {day} are taken")
    if not a.dry_run:
        with open(os.path.join(FEEDBACK, name + ".md"), "x", encoding="utf-8", newline="\n") as f:
            f.write(f"# Lane {name} feedback" + (f" ({a.what})" if a.what else "") + "\n")
    print(name)


if __name__ == "__main__":
    main()
