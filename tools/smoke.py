#!/usr/bin/env python3
"""Smoke batches on parallel game instances: build and deploy once, then run scenarios x repeats N at
a time on local/game-1..N (tools/instances.ps1), collecting each run under local/traces/<batch>/.

    tools/py -I tools/smoke.py --scenario hood-belladonna --scenario lot-townie-trace --repeats 3 --instances 3
    tools/py -I tools/smoke.py --scenario lot-townie-trace --repeats 3 --seeds 1,2,3 --reset-userdata --batch townieXY
    tools/py -I tools/smoke.py ... --dry-run            # what would be built, created, launched; nothing runs

Per run <scenario>-<rep> in local/traces/<batch>/: .log (scenario.ps1 output), .host.log (the
instance's ts2host.log), .txt (simtrace.txt, when the scenario traced) and the snapshot folder
(snap: steps). One line per run as it ends, then a summary; summary.json has the same. A run passes
when scenario.ps1 exits 0 (every until: matched, the game started and stopped). Exceptions are the
host's first-chance fault log ("exception 0x..."): listed, not judged (the game handles some itself).

Instances run quiet (window off-screen, muted) unless --visible. Never run this while another game is
running from local/game; one batch at a time. Snapshot folders of all but the newest --keep-batches
batches are deleted, and large files of all but the newest --keep-trace-batches; summary.json and small logs stay. A
batch folder holding a file named KEEP (milestone evidence) is never pruned.
Snapshot files are hardlinks into local/snapstore (tools/snapstore.py; a snapshot costs its changed files only), and
pruning ends with the store's gc. A batch refuses to start with less than --min-free-gb (5) free.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import snapstore  # noqa: E402
import tsdb  # noqa: E402

ROOT = tsdb.ROOT
LOCAL = tsdb.main_root() / "local"
PS = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File"]
LARGE_BATCH_FILE = 2**20  # bytes; prune_files deletes files from this size up
BENIGN = {"e06d7363", "40010006", "4001000a", "406d1388"}


def instance_bin(n):
    return LOCAL / f"game-{n}" / "EP9" / "TSBin"


def ps(script, *args, capture=True):
    cmd = PS + [str(ROOT / "tools" / script)] + [str(a) for a in args]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=capture, text=True)
    return r.returncode, (r.stdout or "") + (r.stderr or "")


def read_deployed(n):
    p = instance_bin(n) / "ts2deploy.txt"
    return p.read_text(encoding="ascii", errors="replace").splitlines()[0].strip() if p.exists() else None


def commit_of(deployed):
    """The build part of a ts2deploy.txt line (commit, dirty flag, build time), without the deploy time."""
    return deployed.split(", deployed ")[0] if deployed else None


def set_hooks(n, disabled):
    """Rewrites the [Hooks] section of instance n's ts2reimpl.ini: name=0 for each disabled hook."""
    ini = instance_bin(n) / "ts2reimpl.ini"
    text = ini.read_text(encoding="ascii").splitlines()
    out, skipping = [], False
    for line in text:
        s = line.strip()
        if s.startswith("["):
            skipping = s.lower() == "[hooks]"
            if skipping:
                out.append("[Hooks]")
                out.extend(f"{h}=0" for h in sorted(disabled))
                out.append("")
                continue
        if not skipping:
            out.append(line)
    if not any(l.strip().lower() == "[hooks]" for l in text):
        out = ["[Hooks]"] + [f"{h}=0" for h in sorted(disabled)] + [""] + out
    ini.write_text("\n".join(out) + "\n", encoding="ascii")


def analyse(host_log, run_log):
    """What a run's logs say: hooks installed, skipped and disabled, exceptions (code, where), deploy line, failure
    text."""
    info = {"hooks": None, "installed": None, "registered": None, "skipped": [], "disabled": [], "exceptions": [],
            "deployed": None, "failed": None, "instance_lines": 0}
    if host_log.exists():
        for line in host_log.read_text(encoding="latin-1", errors="replace").splitlines():
            m = re.search(r"(\d+) of (\d+) hooks installed", line)
            if m:
                info["hooks"] = f"{m.group(1)}/{m.group(2)}"
                info["installed"], info["registered"] = int(m.group(1)), int(m.group(2))
            # src/hook/hooks.cpp InstallHooks: "hook X skipped: site at rva 0x... doesn't match (...)" or "...: rva 0x...
            # is already hooked by Y"; "hook X disabled by ini" ([Hooks] X=0: --disable-hooks, bisecting).
            m = re.search(r"\bhook (\S+) skipped: (.*)", line)
            if m:
                reason = "site doesn't match" if "doesn't match" in m.group(2) else m.group(2).strip()[:80]
                info["skipped"].append((m.group(1), reason))
            m = re.search(r"\bhook (\S+) disabled by ini", line)
            if m:
                info["disabled"].append(m.group(1))
            m = re.search(r"exception 0x([0-9a-fA-F]{8}) at \S+ \(([^)]*)\)", line)
            if m and m.group(1).lower() not in BENIGN:
                info["exceptions"].append((m.group(1).lower(), m.group(2)))
            if "deployed: " in line and not info["deployed"]:
                info["deployed"] = line.split("deployed: ", 1)[1].strip()
            if re.search(r"\binstance \d+: ", line):
                info["instance_lines"] += 1
    if run_log.exists():
        for line in run_log.read_text(encoding="utf-8", errors="replace").splitlines():
            if (line.startswith("failed:") or "Exception" in line) and not info["failed"]:
                info["failed"] = line.strip()[:200]
    return info


def hook_problem(info):
    """Why a run's hooks fail it (None: every registered hook not disabled by the ini is installed). A skipped hook is
    original code running where we think ours does (and its Hooks.<name> switch is 0, so the traps resting on it stay
    unarmed): lane DW's hand-written cTSPersistResKeyList_Create row was skipped in every run for days ("hooks 5463/5464"
    in each PASS line). No skip has been legitimate so far (every host log in local/saves and local/traces, 2026-10-23),
    so there is no known-skip list: a site that really changed needs its row fixed (tools/hookcrc.py)."""
    if info["registered"] is None:
        return None  # no host log, or the host stopped before installing: the run's own result says why
    if info["skipped"]:
        return "hooks skipped: " + ", ".join(f"{n} ({why})" for n, why in info["skipped"])
    missing = info["registered"] - len(info["disabled"]) - info["installed"]
    if missing > 0:
        return f"{missing} hook(s) not installed and not disabled by the ini (hooks {info['hooks']}; no skip line names them)"
    return None


def skipped_line(results):
    """The summary's line naming every hook some run skipped (None when none was)."""
    runs = {}
    for r in results:
        for h in r.get("skipped_hooks", []):
            runs.setdefault(h, []).append(r["run"])
    if not runs:
        return None
    return ("HOOKS SKIPPED (each fails its run): " + "; ".join(f"{h} in {len(v)} run(s)" for h, v in sorted(runs.items()))
            + " -- fix the rows (tools/hookcrc.py)")


def describe(exceptions):
    if not exceptions:
        return "no faults"
    counts = {}
    for code, _ in exceptions:
        counts[code] = counts.get(code, 0) + 1
    first = exceptions[0]
    return "faults " + " ".join(f"{c} x{k}" for c, k in counts.items()) + f" (first {first[0]} at {first[1]})"


def batches_newest_first():
    """Finished batches, not KEEP-marked. Sorted by summary.json's time: a pruned folder's own time is not the run's."""
    traces = LOCAL / "traces"
    return sorted((d for d in traces.iterdir()
                   if d.is_dir() and (d / "summary.json").exists() and not (d / "KEEP").exists()),
                  key=lambda d: (d / "summary.json").stat().st_mtime, reverse=True)


def prune_files(keep, dry_run=False):
    """Deletes large files (traces, big logs) of all but the newest `keep` batches; summary.json and small files stay."""
    n = size = 0
    for old in batches_newest_first()[keep:]:
        times = old.stat()
        for f in old.iterdir():
            if not f.is_file() or f.name == "summary.json":
                continue
            length = f.stat().st_size
            if length >= LARGE_BATCH_FILE:
                n += 1
                size += length
                if not dry_run:
                    f.unlink()
        if not dry_run:
            os.utime(old, ns=(times.st_atime_ns, times.st_mtime_ns))
    print(f"traces: {'would delete' if dry_run else 'deleted'} {n} files ({size / 2**30:.1f} GB)")


def prune(keep, keep_files):
    """Deletes snapshot folders of all but the newest `keep` batches and large files of all but the newest
    `keep_files`; KEEP-marked batches are skipped."""
    for old in batches_newest_first()[keep:]:
        times = old.stat()
        for sub in old.iterdir():
            if sub.is_dir():
                shutil.rmtree(sub, ignore_errors=True)
        os.utime(old, ns=(times.st_atime_ns, times.st_mtime_ns))
    prune_files(keep_files)
    # Snapshot files are links into local/snapstore (tools/snapstore.py): drop the objects nothing links now.
    n, size = snapstore.Store().gc()
    if n:
        print(f"snapstore: deleted {n} unlinked objects ({size / 2**30:.2f} GB)")


def past_seconds(newest=10):
    """Longest run time of each scenario over the newest `newest` batches (summary.json), for longest-first scheduling."""
    traces = LOCAL / "traces"
    if not traces.exists():
        return {}
    batches = sorted((d for d in traces.iterdir() if d.is_dir() and (d / "summary.json").exists()),
                     key=lambda d: (d / "summary.json").stat().st_mtime, reverse=True)[:newest]
    seconds = {}
    for d in batches:
        try:
            results = json.loads((d / "summary.json").read_text(encoding="utf-8")).get("results", [])
        except (OSError, ValueError):
            continue
        for r in results:
            name, took = str(r.get("run", "")), r.get("seconds")
            if "-" in name and isinstance(took, (int, float)):
                s = name.rsplit("-", 1)[0]
                seconds[s] = max(seconds.get(s, 0), took)
    return seconds


def free_ram_gb():
    """Free physical memory in GB (Windows), or None when it cannot be read."""
    import ctypes

    class Status(ctypes.Structure):
        _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong), ("total", ctypes.c_ulonglong),
                    ("avail", ctypes.c_ulonglong), ("total_page", ctypes.c_ulonglong), ("avail_page", ctypes.c_ulonglong),
                    ("total_virtual", ctypes.c_ulonglong), ("avail_virtual", ctypes.c_ulonglong),
                    ("avail_extended", ctypes.c_ulonglong)]
    s = Status()
    s.length = ctypes.sizeof(Status)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(s)):
        return None
    return s.avail / 2**30


def free_space_problem(min_gb, path=LOCAL):
    """A message when the disk holding local/ has less than min_gb free (a full disk fails runs half-way and
    corrupts snapshots), else None."""
    free = shutil.disk_usage(str(path)).free
    if free >= min_gb * 2**30:
        return None
    return (f"only {free / 2**30:.1f} GB free on the disk of {path} (floor {min_gb:g} GB, --min-free-gb): "
            f"free space first (tools/diskuse.py shows what uses it; tools/snapstore.py gc, old trace batches)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--scenario", action="append", help="tools/scenarios/<name>.txt (repeatable)")
    ap.add_argument("--repeats", type=int, default=1)
    ap.add_argument("--instances", type=int, default=0,
                    help="parallel instances (local/game-1..N); 0 (default): one per run")
    ap.add_argument("--min-free-ram-gb", type=float, default=4,
                    help="launch no further run while free physical memory is below this (default 4)")
    ap.add_argument("--batch", help="folder name under local/traces (default smoke-<time>)")
    ap.add_argument("--seed", help="fixed seed for every run (game.ps1 -Seed)")
    ap.add_argument("--seeds", help="comma list: repeat r uses seeds[r-1] (cycled)")
    ap.add_argument("--reset-userdata", action="store_true", help="scenario.ps1 -ResetUserData (the instance's folder)")
    ap.add_argument("--baseline", help="scenario.ps1 -Baseline DIR")
    ap.add_argument("--reference", action="store_true", help="no replacements (TS2HOST_NOHOOKS)")
    ap.add_argument("--draws", action="store_true")
    ap.add_argument("--visible", action="store_true", help="not quiet (windows on screen)")
    ap.add_argument("--no-build", action="store_true", help="use what is deployed (all instances must agree)")
    ap.add_argument("--disable-hooks", default="", help="comma list (or @file, one per line) of hook names set to 0 "
                                                         "in every instance's ini for this batch")
    ap.add_argument("--fail-regex", default=r"exception 0xc0000094|exception 0xc0000374|trap: original",
                    help="a run also fails when its host log matches this (default: integer divide by zero, heap "
                         "corruption, or a trap on an original function; pass '' to turn it off)")
    ap.add_argument("--timeout", type=int, default=1800, help="seconds per run before it is killed")
    ap.add_argument("--keep-batches", type=int, default=3)
    ap.add_argument("--keep-trace-batches", type=int, default=10,
                    help="batches that keep their large files (traces, big logs); older ones keep summary.json and small files")
    ap.add_argument("--prune-only", action="store_true", help="prune large files and exit, with --dry-run only report")
    ap.add_argument("--stagger", type=int, default=20, help="seconds between two launches (boots load the disk)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--min-free-gb", type=float, default=5, help="refuse to start below this much free space (default 5)")
    a = ap.parse_args()

    if a.prune_only:
        prune_files(a.keep_trace_batches, a.dry_run)
        return
    if not a.scenario:
        ap.error("the following arguments are required: --scenario")

    low = free_space_problem(a.min_free_gb)
    if low and not a.dry_run:
        sys.exit("smoke: " + low)
    if low:
        print("would refuse: " + low)

    for s in a.scenario:
        if not (ROOT / "tools" / "scenarios" / f"{s}.txt").exists():
            sys.exit(f"no scenario tools/scenarios/{s}.txt")
    batch = a.batch or datetime.now().strftime("smoke-%Y%m%d-%H%M%S")
    out = LOCAL / "traces" / batch
    seeds = a.seeds.split(",") if a.seeds else None
    runs = [(s, r) for s in a.scenario for r in range(1, a.repeats + 1)]
    # Longest first (by earlier batches; scenarios never timed count as longest), so the slowest runs don't start last.
    took = past_seconds()
    runs.sort(key=lambda run: -took.get(run[0], float("inf")))
    if a.instances <= 0:
        a.instances = len(runs)
    if a.disable_hooks.startswith("@"):  # a file of hook names, one per line (tools/hookbisect.py)
        disabled = [h.strip() for h in Path(a.disable_hooks[1:]).read_text(encoding="utf-8").splitlines() if h.strip()]
    else:
        disabled = [h for h in a.disable_hooks.split(",") if h]

    missing = [n for n in range(1, a.instances + 1) if not instance_bin(n).exists()]
    if a.dry_run:
        print(f"batch {batch}: {len(runs)} runs on {a.instances} instances, output {out}")
        if missing:
            print(f"would create instances {missing}: instances.ps1 create -Count {a.instances}")
            print(ps("instances.ps1", "create", "-Count", a.instances, "-DryRun")[1].strip()[:3000])
        print(ps("deploy.ps1", "-DryRun")[1].strip() if not a.no_build else "no build: deployed hosts as they are")
        for i, (s, r) in enumerate(runs):
            n = i % a.instances + 1
            seed = seeds[(r - 1) % len(seeds)] if seeds else a.seed
            print(f"run {s}-{r}: instance {n}" + (f", seed {seed}" if seed else "") + (f", {len(disabled)} hooks off" if disabled else ""))
        n = 1
        args = scenario_args(a, runs[0][0], runs[0][1], n, out, seeds)
        print("first run's command: " + " ".join(PS + [str(ROOT / "tools" / "scenario.ps1")] + args))
        print(ps("scenario.ps1", *(args + ["-DryRun"]))[1].strip())
        return

    if subprocess.run(["powershell", "-NoProfile", "-Command", "Get-Process Sims2EP9,ts2host -ErrorAction SilentlyContinue"],
                      capture_output=True, text=True).stdout.strip():
        sys.exit("a game is already running: stop it first (smoke batches need every instance free)")
    if missing:
        code, text = ps("instances.ps1", "create", "-Count", a.instances)
        print(text.strip())
        if code:
            sys.exit("creating instances failed")
    if not a.no_build:
        code, text = ps("deploy.ps1")  # refuses in a worktree: the game runs what's merged
        print(text.strip().splitlines()[-1] if text.strip() else "")
        if code:
            sys.exit("build/deploy failed")
    deployed = {n: commit_of(read_deployed(n)) for n in range(1, a.instances + 1)}
    if None in deployed.values() or len(set(deployed.values())) != 1:
        sys.exit(f"instances disagree on what is deployed: {deployed} (deploy.ps1 copies one build to all)")
    tested = next(iter(deployed.values()))
    print(f"batch {batch}: {len(runs)} runs on {a.instances} instances; {tested}")
    out.mkdir(parents=True, exist_ok=True)
    for n in range(1, a.instances + 1):
        set_hooks(n, disabled)

    queue = list(runs)
    active = {}  # instance -> (name, process, log file, start time)
    results = []
    last_launch = 0.0
    low_ram_noted = False
    try:
        while queue or active:
            for n in range(1, a.instances + 1):
                if n in active or not queue or time.time() - last_launch < a.stagger:
                    continue
                ram = free_ram_gb()
                if active and ram is not None and ram < a.min_free_ram_gb:
                    if not low_ram_noted:
                        print(f"free memory {ram:.1f} GB < {a.min_free_ram_gb} GB: holding the next launch until a run ends")
                        low_ram_noted = True
                    break
                last_launch = time.time()
                s, r = queue.pop(0)
                name = f"{s}-{r}"
                (instance_bin(n) / "simtrace.txt").unlink(missing_ok=True)
                log = open(out / f"{name}.log", "w", encoding="utf-8")
                cmd = PS + [str(ROOT / "tools" / "scenario.ps1")] + scenario_args(a, s, r, n, out, seeds)
                proc = subprocess.Popen(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, text=True)
                active[n] = (name, proc, log, time.time())
            time.sleep(2)
            for n, (name, proc, log, started) in list(active.items()):
                code = proc.poll()
                timed_out = code is None and time.time() - started > a.timeout
                if code is None and not timed_out:
                    continue
                if timed_out:
                    subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)], capture_output=True)
                    code = "timeout"
                log.close()
                ps("game.ps1", "stop", "-Instance", n)
                bin_ = instance_bin(n)
                if (bin_ / "ts2host.log").exists():
                    shutil.copy2(bin_ / "ts2host.log", out / f"{name}.host.log")
                if (bin_ / "simtrace.txt").exists():
                    shutil.copy2(bin_ / "simtrace.txt", out / f"{name}.txt")
                info = analyse(out / f"{name}.host.log", out / f"{name}.log")
                ok = code == 0
                if ok and a.fail_regex and (out / f"{name}.host.log").exists() and re.search(
                        a.fail_regex, (out / f"{name}.host.log").read_text(encoding="latin-1", errors="replace")):
                    ok = False
                    info["failed"] = f"host log matches {a.fail_regex!r}"
                hooks_bad = hook_problem(info)
                if ok and hooks_bad:
                    ok = False
                    info["failed"] = hooks_bad
                result = {"run": name, "instance": n, "pass": ok, "exit": code, "seconds": int(time.time() - started),
                          "hooks": info["hooks"], "skipped_hooks": [h for h, _ in info["skipped"]],
                          "exceptions": info["exceptions"], "deployed": info["deployed"], "failed": info["failed"]}
                results.append(result)
                del active[n]
                print(f"{'PASS' if ok else 'FAIL'} {name:28} i{n} {result['seconds']:5}s hooks {info['hooks'] or '?'}  "
                      f"{describe(info['exceptions'])}" + (f"  [{info['failed']}]" if info["failed"] and not ok else ""), flush=True)
    finally:
        for n, (name, proc, log, _) in active.items():
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)], capture_output=True)
            log.close()
            ps("game.ps1", "stop", "-Instance", n)
        if disabled:
            for n in range(1, a.instances + 1):
                set_hooks(n, [])

    passed = sum(r["pass"] for r in results)
    codes = sorted({c for r in results for c, _ in r["exceptions"]})
    print(f"summary {batch}: {passed}/{len(results)} passed; fault codes {', '.join(codes) or 'none'}; tested {tested}")
    line = skipped_line(results)
    if line:
        print(line)
    (out / "summary.json").write_text(json.dumps({"batch": batch, "tested": tested, "disabled_hooks": disabled,
                                                  "args": vars(a), "results": results}, indent=1), encoding="utf-8")
    prune(a.keep_batches, a.keep_trace_batches)
    sys.exit(0 if passed == len(results) else 1)


def scenario_args(a, s, r, n, out, seeds):
    args = [f"@{s}", "-Instance", str(n), "-NoDeploy", "-SnapDir", str(out / f"{s}-{r}")]
    if not a.visible:
        args.append("-Quiet")
    if a.reset_userdata:
        args.append("-ResetUserData")
    if a.baseline:
        args += ["-Baseline", a.baseline]
    seed = seeds[(r - 1) % len(seeds)] if seeds else a.seed
    if seed:
        args += ["-Seed", seed]
    if a.reference:
        args.append("-Reference")
    if a.draws:
        args.append("-Draws")
    return args


if __name__ == "__main__":
    main()
