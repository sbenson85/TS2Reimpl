#!/usr/bin/env python3
"""Unattended hook bisect: which hook group (src/hook/tables/<group>.cpp), then which hook in it, has
to be off for a failing smoke scenario to pass. Each probe is a smoke.py batch (--repeats runs on
--instances parallel instances, the hooks under test set to 0 in ts2reimpl.ini [Hooks]); a probe fails
when any run fails (scenario.ps1 error, or --fail-regex in the host log).

    tools/py -I tools/hookbisect.py --scenario load-check --repeats 3 --instances 3 --reset-userdata
    tools/py -I tools/hookbisect.py ... --fail-regex "exception 0xc0000094" --groups cTSPerson,Prim_*
    tools/py -I tools/hookbisect.py ... --dry-run      # the groups and the probe plan; nothing runs

Order: build and deploy once; probe 1 all hooks on (must fail, else nothing to find); probe 2 the
suspect groups all off (must pass, else the cause is outside them); then halve the suspects by group
until one group is left, then halve its hooks. Every probe's batch is local/traces/<name>-pN.

Pitfall: our replacements call each other directly, so turning a hook off removes only the jump from
the original's entry; any replacement still reached from another hooked replacement keeps running.
Group probes (a group = a class, whose functions mostly call each other) cut more of that path than
single hooks, which is why groups come first. The answer is "these hooks off makes it pass": the faulty
code is in that group or called by it. A final probe confirms the answer alone.
"""
import argparse
import fnmatch
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

ROOT = tsdb.ROOT
LOCAL = tsdb.main_root() / "local"
ENTRY = re.compile(r'^\s*\{"([A-Za-z0-9_]+)",\s*0x[0-9a-fA-F]+')


def hook_groups():
    groups = {}
    for f in sorted((ROOT / "src" / "hook" / "tables").glob("*.cpp")):
        names = [m.group(1) for line in f.read_text(encoding="utf-8", errors="replace").splitlines() if (m := ENTRY.match(line))]
        if names:
            groups[f.stem] = names
    return groups


class Prober:
    def __init__(self, a, name):
        self.a, self.name, self.count, self.built = a, name, 0, a.no_build

    def probe(self, off_hooks, label):
        """True when the scenario passes with these hooks off."""
        self.count += 1
        batch = f"{self.name}-p{self.count}"
        listing = LOCAL / "traces" / f"{batch}.disabled.txt"
        cmd = [sys.executable, "-I", str(ROOT / "tools" / "smoke.py"), "--batch", batch, "--repeats", str(self.a.repeats),
               "--instances", str(self.a.instances), "--disable-hooks", f"@{listing}"]
        for s in self.a.scenario:
            cmd += ["--scenario", s]
        if self.a.reset_userdata:
            cmd.append("--reset-userdata")
        if self.a.seeds:
            cmd += ["--seeds", self.a.seeds]
        if self.a.fail_regex:
            cmd += ["--fail-regex", self.a.fail_regex]
        if self.built:
            cmd.append("--no-build")
        print(f"probe {self.count}: {label} ({len(off_hooks)} hooks off) -> local/traces/{batch}", flush=True)
        if self.a.dry_run:
            return None
        listing.parent.mkdir(parents=True, exist_ok=True)
        listing.write_text("\n".join(sorted(off_hooks)) + "\n", encoding="utf-8")
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        self.built = True
        summary = LOCAL / "traces" / batch / "summary.json"
        if not summary.exists():
            sys.exit(f"probe {self.count} produced no summary:\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")
        results = json.loads(summary.read_text(encoding="utf-8"))["results"]
        passed = all(x["pass"] for x in results)
        print(f"  {'PASS' if passed else 'FAIL'}: " + ", ".join(f"{x['run']} {'ok' if x['pass'] else 'fail'}" for x in results), flush=True)
        return passed


def bisect(prober, items, expand, always_off, what):
    """Narrows `items` (each expands to hook names) to the one whose hooks off make the run pass."""
    suspects = list(items)
    while len(suspects) > 1:
        half = suspects[: len(suspects) // 2]
        off = always_off + [h for i in half for h in expand(i)]
        passed = prober.probe(off, f"{what} off: {', '.join(half[:6])}{' ...' if len(half) > 6 else ''}")
        if passed is None:  # dry run: show the shape of the search only
            suspects = half
            continue
        suspects = half if passed else suspects[len(suspects) // 2:]
    return suspects[0] if suspects else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scenario", action="append", required=True)
    ap.add_argument("--repeats", type=int, default=3, help="runs per probe (a probe fails if any run fails)")
    ap.add_argument("--instances", type=int, default=3)
    ap.add_argument("--seeds")
    ap.add_argument("--reset-userdata", action="store_true")
    ap.add_argument("--fail-regex")
    ap.add_argument("--groups", help="comma list of group names or globs to suspect (default: all)")
    ap.add_argument("--name", default="bisect")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    groups = hook_groups()
    if a.groups:
        pats = a.groups.split(",")
        suspects = [g for g in groups if any(fnmatch.fnmatch(g, p) for p in pats)]
    else:
        suspects = list(groups)
    total = sum(len(groups[g]) for g in suspects)
    print(f"{len(groups)} hook groups, {sum(map(len, groups.values()))} hooks; suspects: {len(suspects)} groups, {total} hooks")
    if not suspects:
        sys.exit("no group matches --groups")
    p = Prober(a, a.name)
    if p.probe([], "all hooks on") is True:
        sys.exit("all hooks on passes: nothing to bisect (more --repeats, or a --fail-regex?)")
    every = [h for g in suspects for h in groups[g]]
    if p.probe(every, "all suspect groups off") is False:
        sys.exit("still fails with every suspect hook off: the cause is outside these groups (or not in a hook)")
    group = bisect(p, suspects, lambda g: groups[g], [], "groups")
    print(f"group: {group} ({len(groups[group])} hooks)")
    hook = bisect(p, groups[group], lambda h: [h], [], f"{group} hooks")
    if not a.dry_run:
        alone = p.probe([hook], f"confirm: only {hook} off")
        print(f"result: {hook} (group {group}); alone off: {'passes' if alone else 'still fails (it needs other hooks of the group off too: report the group)'}")
    else:
        print(f"dry run: about {p.count + 1} probes x {a.repeats} runs")


if __name__ == "__main__":
    main()
