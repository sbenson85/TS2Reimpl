"""Function-boundary fixes for a Ghidra program (tools/ghidra/ApplyBoundaries.java), from lane HW's boundary list
(tools/libmatch.py --boundaries --json): the _EH_prolog noreturn flag cleared and its callers' flows repaired, function
starts that are pieces of a library body removed, missing library bodies created, wrongly sized ones resized.

  tools/py -I tools/boundaries.py report [--list FILE] [--cpu 2] [--maxmem 8G]
        on a fresh copy of the analysed project (local/boundaries/proj, from local/ghidra; never the shared server's):
        apply the fixes without saving and report how the function list changes. Writes local/boundaries/out/
        (before.tsv, after.tsv, actions.tsv) and local/boundaries/report.json; prints the summary.
        --probe 0x..,0x..   also report what is at these RVAs before any fix (instruction, data, function, bookmarks)
  tools/py -I tools/boundaries.py summarise [DIR]      the summary of an earlier run's out directory again
  tools/py -I tools/boundaries.py actions [--list FILE] [--out FILE]   only write the actions file the script reads
  tools/py -I tools/boundaries.py apply --project DIR [--list FILE] [--cpu 2] [--maxmem 8G]
        apply and SAVE in the Ghidra project DIR (DIR/Sims2EP9.gpr). For the coordinator: nothing may have the project
        open (stop the Ghidra server first if DIR is one it uses). A zip of DIR goes to local/ghidra-backups/ first.
        Applied to local/ghidra (the analysed project every server start copies), the next `ghidra.py start` rebuilds the
        annotated project (the script touches Sims2EP9.rep/project.prp, which is part of the server's cache key).

Without --list the fixes come from `tools/libmatch.py --boundaries --json` (written to local/boundaries/boundaries.jsonl).
Rows: resize with evidence "_EH_prolog caller" are checked (`expect`) after the flow repair, not forced: the repaired flow
decides the body; library-body resizes and creates set the body to [rva, rva+size); removes delete the function (not its
code). Ghidra runs at below-normal priority with its own APPDATA (local/boundaries/home).
"""
import json
import os
import re
import shutil
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tsdb  # noqa: E402

LOCAL = tsdb.main_root() / "local"
WORK = LOCAL / "boundaries"
EH_PROLOG = "0x8248b0"  # _EH_prolog: the analysis took it for noreturn (lane HW: 4,293 callers sized 10 bytes)
BELOW_NORMAL = 0x4000


# ---- pure parts (tools/tests/test_boundaries.py) ----

def actions(rows, returns=(EH_PROLOG,)):
    """HW's boundary rows -> [(action, rva, arg)] for ApplyBoundaries.java (arg: size, container or "")."""
    out = [("returns", r, "") for r in returns]
    for e in rows:
        fix = e["fix"]
        if fix.startswith("remove"):
            m = re.search(r"at (0x[0-9a-f]+)", fix)
            out.append(("remove", e["rva"], m.group(1) if m else "?"))
        elif fix == "create":
            out.append(("create", e["rva"], str(e["size"])))
        elif fix == "resize":
            flow = "_EH_prolog" in e.get("evidence", "") and e.get("ghidra_size") is not None
            out.append(("expect" if flow else "resize", e["rva"], str(e["size"])))
        else:
            raise ValueError(f"unknown fix {fix!r} at {e['rva']}")
    return out


def action_lines(acts):
    return "".join("\t".join(a for a in act if a != "") + "\n" for act in acts)


def read_list(text):
    """before.tsv / after.tsv text -> {rva: (size, name)}."""
    out = {}
    for line in text.splitlines():
        if line.strip():
            rva, size, name = (line.split("\t") + ["", ""])[:3]
            out[rva] = (int(size), name)
    return out


def read_results(text):
    """actions.tsv text -> [(action, rva, result, detail)]."""
    return [tuple((l.split("\t") + ["", "", ""])[:4]) for l in text.splitlines() if l.strip()]


def compare(before, after):
    """How the function list changed: {"removed": [(rva, size)], "created": [(rva, size)], "resized": [(rva, old, new)]}."""
    removed = sorted(((r, before[r][0]) for r in before if r not in after), key=lambda x: int(x[0], 16))
    created = sorted(((r, after[r][0]) for r in after if r not in before), key=lambda x: int(x[0], 16))
    resized = sorted(((r, before[r][0], after[r][0]) for r in before if r in after and before[r][0] != after[r][0]),
                     key=lambda x: int(x[0], 16))
    return {"removed": removed, "created": created, "resized": resized}


def differs_kind(detail):
    """An `expect ... differs` detail -> which parts of the expected range the flow body lacks: "free" (code in no
    function: catch blocks or handlers reached only through the EH tables), "other" (another function), "none" (no code),
    joined with "+"; "beyond" when the body only reaches past the expected end; "?" when the detail doesn't parse."""
    m = re.search(r"(\d+) B code in no function, (\d+) B in other functions.*?, (\d+) B no code; body beyond: (\d+) B", detail)
    if not m:
        return "?"
    free, other, none, beyond = map(int, m.groups())
    parts = [k for k, v in (("free", free), ("other", other), ("none", none)) if v]
    return "+".join(parts) if parts else ("beyond" if beyond else "ranges")


def summary(acts, results, change):
    """Counts and the cases the coordinator must look at (as lines of text) plus the report dict."""
    by = Counter((a, r) for a, _, r, _ in results)
    wanted = {rva: (act, arg) for act, rva, arg in acts}
    grown = [c for c in change["resized"] if c[2] > c[1]]
    shrunk = [c for c in change["resized"] if c[2] < c[1]]
    # Functions whose size changed though no action names them: the flow repair (callers of _EH_prolog that the list
    # doesn't have, or callers of those) or a side effect of a body set nearby.
    unlisted = [c for c in change["resized"] if c[0] not in wanted]
    problems = [x for x in results if x[2] in ("conflict", "not-code", "error", "differs", "absent")
                and not (x[0] == "remove" and x[2] == "absent")]
    lines = [
        f"actions: {len(acts)} ({dict(Counter(a for a, _, _ in acts))})",
        "results: " + ", ".join(f"{a} {r} {n}" for (a, r), n in sorted(by.items())),
        f"function list: {len(change['removed'])} removed ({sum(s for _, s in change['removed'])} B), "
        f"{len(change['created'])} created ({sum(s for _, s in change['created'])} B), {len(change['resized'])} resized "
        f"({len(grown)} grown by {sum(n - o for _, o, n in grown)} B, {len(shrunk)} shrunk by {sum(o - n for _, o, n in shrunk)} B)",
        f"resized without an action of their own: {len(unlisted)}" + (" (e.g. " + ", ".join(
            f"{r} {o}->{n}" for r, o, n in unlisted[:8]) + ")" if unlisted else ""),
        f"to look at: {len(problems)}",
    ]
    lines += [f"  {a} {r} {res}: {d}" for a, r, res, d in problems[:40]]
    lines += [f"probe {r}: {d}" for a, r, _, d in results if a == "probe"]
    if len(problems) > 40:
        lines.append(f"  ... {len(problems) - 40} more in report.json")
    differs = [differs_kind(d) for a, _, res, d in results if a == "expect" and res == "differs"]
    if differs:
        lines.append("expect differs, by what the flow body lacks: " + ", ".join(
            f"{k} {n}" for k, n in Counter(differs).most_common()))
    orphans = [x for x in results if x[0] == "orphan"]
    if orphans:
        lines.append(f"resized functions that left code in no function: {len(orphans)}")
        lines += [f"  {r}: {d}" for _, r, _, d in orphans[:20]]
    report = {"actions": len(acts), "results": {f"{a} {r}": n for (a, r), n in sorted(by.items())},
              "removed": change["removed"], "created": change["created"], "resized": change["resized"],
              "resized_unlisted": unlisted, "problems": problems, "orphans": orphans}
    return lines, report


# ---- running Ghidra ----

def ghidra_dir():
    return next((LOCAL / "tools").glob("ghidra_*"))


def env_for(maxmem):
    env = dict(os.environ, GHIDRA_HEADLESS_MAXMEM=maxmem)
    for var in ("APPDATA", "LOCALAPPDATA"):  # not the shared server's %APPDATA%\ghidra (script-bundle cache races)
        d = WORK / "home" / var.lower()
        d.mkdir(parents=True, exist_ok=True)
        env[var] = str(d)
    return env


def load_rows(path):
    if path:
        return [json.loads(l) for l in open(path, encoding="utf-8") if l.strip()]
    WORK.mkdir(parents=True, exist_ok=True)
    out = WORK / "boundaries.jsonl"
    with open(out, "w", encoding="utf-8") as f:
        subprocess.run([sys.executable, "-I", str(ROOT / "tools/libmatch.py"), "--boundaries", "--json"], stdout=f, check=True)
    return [json.loads(l) for l in open(out, encoding="utf-8") if l.strip()]


def run_script(proj_dir, acts, out, cpu, maxmem, read_only):
    WORK.mkdir(parents=True, exist_ok=True)
    act_file = WORK / "actions-in.tsv"
    act_file.write_text(action_lines(acts), encoding="utf-8")
    if out.exists():
        shutil.rmtree(out)
    cmd = [str(ghidra_dir() / "support" / "analyzeHeadless.bat"), str(proj_dir), "Sims2EP9", "-process", "-noanalysis",
           "-max-cpu", str(cpu), "-scriptPath", str(ROOT / "tools" / "ghidra"), "-postScript", "ApplyBoundaries.java",
           str(act_file), str(out)]
    if read_only:
        cmd.insert(cmd.index("-noanalysis") + 1, "-readOnly")
    log = WORK / "ghidra.log"
    keep = re.compile(r"ApplyBoundaries|ERROR|Exception|SCRIPT ERROR")
    t0 = time.time()
    with open(log, "w", encoding="utf-8") as lf:
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                             creationflags=BELOW_NORMAL, env=env_for(maxmem))
        for line in p.stdout:
            if keep.search(line):
                lf.write(line.rstrip() + "\n")
                lf.flush()
        code = p.wait()
    print(f"Ghidra exit {code} in {time.time() - t0:.0f} s (log {log})", flush=True)
    if code != 0 or not (out / "after.tsv").exists():
        sys.exit(f"ApplyBoundaries did not finish; see {log}")


def summarise(out, acts=None):
    if acts is None:
        acts = [tuple((l.split("\t") + ["", ""])[:3]) for l in (WORK / "actions-in.tsv").read_text(encoding="utf-8").splitlines()
                if l.strip()]
    before = read_list((out / "before.tsv").read_text(encoding="utf-8"))
    after = read_list((out / "after.tsv").read_text(encoding="utf-8"))
    results = read_results((out / "actions.tsv").read_text(encoding="utf-8"))
    lines, report = summary(acts, results, compare(before, after))
    (WORK / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    print("\n".join(lines))
    print(f"details: {WORK / 'report.json'}")


def arg(args, name, default=None):
    return args[args.index(name) + 1] if name in args else default


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return
    cmd = args[0]
    cpu, maxmem = arg(args, "--cpu", "2"), arg(args, "--maxmem", "8G")
    if cmd == "summarise":
        return summarise(Path(args[1]) if len(args) > 1 else WORK / "out")
    acts = actions(load_rows(arg(args, "--list")))
    if "--probe" in args:  # what is at these RVAs before any fix (instruction, data, function, bookmarks)
        acts = [("probe", r, "") for r in arg(args, "--probe").split(",")] + acts
    if cmd == "actions":
        out = Path(arg(args, "--out", WORK / "actions-in.tsv"))
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(action_lines(acts), encoding="utf-8")
        print(f"{len(acts)} actions -> {out}")
    elif cmd == "report":
        proj = WORK / "proj"
        if proj.exists():
            shutil.rmtree(proj)
        proj.mkdir(parents=True)
        shutil.copy2(LOCAL / "ghidra" / "Sims2EP9.gpr", proj / "Sims2EP9.gpr")
        shutil.copytree(LOCAL / "ghidra" / "Sims2EP9.rep", proj / "Sims2EP9.rep")
        run_script(proj, acts, WORK / "out", cpu, maxmem, read_only=True)
        summarise(WORK / "out", acts)
        shutil.rmtree(proj, ignore_errors=True)  # 600+ MB
    elif cmd == "apply":
        proj = Path(arg(args, "--project") or sys.exit("apply needs --project DIR"))
        if not (proj / "Sims2EP9.gpr").exists():
            sys.exit(f"no Sims2EP9.gpr in {proj}")
        if any((proj / "Sims2EP9.rep").rglob("*.lock*")) or any(proj.glob("*.lock*")):
            sys.exit(f"{proj} is open in a Ghidra (lock files): stop it first")
        backups = LOCAL / "ghidra-backups"
        backups.mkdir(parents=True, exist_ok=True)
        zip_base = backups / f"boundaries-before-{proj.name}-{time.strftime('%Y%m%d-%H%M%S')}"
        shutil.make_archive(str(zip_base), "zip", proj)
        print(f"backup: {zip_base}.zip", flush=True)
        run_script(proj, acts, WORK / "out-apply", cpu, maxmem, read_only=False)
        os.utime(proj / "Sims2EP9.rep" / "project.prp")  # the server's cache key (tools/ghidra.py facts_key)
        summarise(WORK / "out-apply", acts)
    else:
        sys.exit(f"unknown command {cmd!r}; --help")


if __name__ == "__main__":
    main()
