"""Finds library code that byte matching (tools/libmatch.py) misses, by BSim similarity: a local H2 BSim
database per library group, built from the library objects we have, queried with the game's functions.

Everything runs on our own copies under local/bsim/ (main checkout), never on the shared Ghidra server or
its project: library objects are imported read-only into throwaway projects (nothing saved), and the game
is queried from a copy of local/ghidra/Sims2EP9.rep. Ghidra runs at below-normal priority with few threads.

  tools/py -I tools/bsim.py build GROUP [--cpu 2] [--only LABEL]
                                                       import GROUP's objects and add their signatures
                                                       (groups: msvc, ucrt, stl, thirdparty; see groups());
                                                       objects already in the database are skipped
  tools/py -I tools/bsim.py status                     database files, sizes, executables per group
  tools/py -I tools/bsim.py query [--min-size 16] [--cpu 2] [--rvas FILE] [--out FILE] [GROUP ...]
                                                       sign every game function (or the hex RVAs listed in FILE),
                                                       query the groups' databases, write local/bsim/query.jsonl
  tools/py -I tools/bsim.py candidates [--min-sim 0.9] [--top 40] [--json] [--exclude FILE.jsonl ...]
                                                       rank functions not yet known as library code by bytes
                                                       (known: db/library-matches.jsonl, db/crt-equivalents.jsonl
                                                       and any --exclude file of {"rva"} lines);
                                                       writes local/bsim/candidates.jsonl

Candidates are leads, not library code: check each by hand (an exact match under other flags or another
version via libmatch, or structurally the same function under the db/crt-equivalents.jsonl rules).
Logs: local/bsim/<group>.log, local/bsim/query.log (Ghidra's output, filtered to the scripts' lines).
"""
import json
import os
import re
import shutil
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tsdb  # noqa: E402

LOCAL = tsdb.main_root() / "local"
BSIM = LOCAL / "bsim"
SCRIPTS = ROOT / "tools" / "ghidra"
TEMPLATE = "medium_32"
BELOW_NORMAL = 0x4000  # Windows priority class for child processes

MSVC = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.40.33807\lib\x86")
# The open-source builds of tools/thirdparty.ps1, one flag set per library version (BSim is fuzzy about
# flags; these are the sets libmatch matches best, or /O1 where libmatch doesn't use the library).
THIRDPARTY = ["lua50-O2", "zlib114-O1", "zlib113-O1", "jpeg6a-O1OiGS", "jpeg6b-O1OiGS", "libpng105-O1OiGS",
              "speex103-O1OiGS", "expat1950ns-O1", "expat1951ns-O1", "openssl097d-O1"]


def groups():
    """group -> [(label, path)]: a .lib (imported member by member), a directory of .obj files, or a glob of .obj
    files (copied to a directory first)."""
    import sdkucrt
    return {
        "msvc": [(p.stem + "-14.40", MSVC / p) for p in map(Path, ["libcmt.lib", "libvcruntime.lib", "libcpmt.lib", "libconcrt.lib"])],
        "ucrt": [("libucrt-" + sdkucrt.UCRT_VERSION, sdkucrt.lib_path())],
        # probe0-2 only: the probe_sizes* objects repeat the same templates at other element sizes (near-duplicates
        # for BSim) and each takes ~7 min to analyse.
        "stl": [("stlprobe", LOCAL / "stlprobe" / "probe[0-9].obj")],
        "thirdparty": [(d, LOCAL / "thirdparty" / "obj" / d) for d in THIRDPARTY],
    }


def ghidra_dir():
    return next((LOCAL / "tools").glob("ghidra_*"))


def db_path(group):
    return BSIM / f"{group}.mv.db"


def run(cmd, log, env=None):
    """Runs a Ghidra command at below-normal priority, keeping only our scripts' lines and errors in the log."""
    keep = re.compile(r"BSimAdd:|BSimQuery:|ERROR|Exception|Created database")
    # Harmless: demangled data symbols in objects; non-object files in an imported directory.
    noise = re.compile(r"Insufficent memory at address|could not successfully load|Import failed for file")
    with open(log, "a", encoding="utf-8") as out:
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                             creationflags=BELOW_NORMAL, env=env)
        for line in p.stdout:
            if keep.search(line) and not noise.search(line):
                out.write(line.strip() + "\n")
                out.flush()
        return p.wait()


def headless_env(cpu):
    env = dict(os.environ)
    env["GHIDRA_HEADLESS_MAXMEM"] = "4G"
    # Ghidra's settings and OSGi script-bundle cache live under %APPDATA%\ghidra, shared with the coordinator's
    # Ghidra server; concurrent instances race on it ("Unable to cache bundle info"). Give ours their own.
    for var in ("APPDATA", "LOCALAPPDATA"):
        d = BSIM / "home" / var.lower()
        d.mkdir(parents=True, exist_ok=True)
        env[var] = str(d)
    return env


def extract(lib, dest):
    """Writes the members of a .lib that hold code to dest/NNNN_<name>.obj (headless Ghidra imports nothing from
    a .lib archive, and its launcher can't take paths with parentheses such as Program Files (x86))."""
    import libmatch
    dest.mkdir(parents=True, exist_ok=True)
    n = 0
    for i, (name, obj) in enumerate(libmatch.archive_members(lib.read_bytes())):
        if any(True for _ in libmatch.coff_functions(obj)):
            # short member names keep the archive's trailing "/" ("foo.obj/"); long ones are paths
            base = re.sub(r"[^\w.-]", "_", name.rstrip("/").replace("\\", "/").split("/")[-1])
            (dest / f"{i:04d}_{base}").write_bytes(obj)
            n += 1
    return n


def build(group, cpu, only=""):
    BSIM.mkdir(parents=True, exist_ok=True)
    log = BSIM / f"{group}.log"
    g = ghidra_dir()
    if not db_path(group).exists():
        run([str(g / "support" / "bsim.bat"), "createdatabase", "file:/" + str(BSIM / group).replace("\\", "/"), TEMPLATE,
             "--name", group], log)
    for label, path in groups()[group]:
        if only and label != only:
            continue
        tmp = None
        if path.suffix.lower() == ".lib":
            tmp = BSIM / "extract" / label
            if tmp.exists():
                shutil.rmtree(tmp)
            with open(log, "a", encoding="utf-8") as out:
                out.write(f"== {label}: {extract(path, tmp)} members with code extracted from {path}\n")
            path = tmp
        elif "*" in path.name or "[" in path.name:
            tmp = BSIM / "extract" / label
            if tmp.exists():
                shutil.rmtree(tmp)
            tmp.mkdir(parents=True)
            for f in sorted(path.parent.glob(path.name)):
                shutil.copy2(f, tmp / f.name)
            path = tmp
        proj = BSIM / "proj"
        proj.mkdir(exist_ok=True)
        # A whole directory: a wildcard (dir\*.obj) overflows the launcher's command line for the UCRT's 740 objects.
        # Other files in it (build.cmd, logs) fail to import, harmlessly.
        cmd = [str(g / "support" / "analyzeHeadless.bat"), str(proj), f"Libs-{group}", "-import", str(path), "-recursive",
               "-readOnly", "-max-cpu", str(cpu), "-scriptPath", str(SCRIPTS), "-preScript", "BSimPrep.java",
               "-postScript", "BSimAdd.java", str(db_path(group)), label]
        with open(log, "a", encoding="utf-8") as out:
            out.write(f"== {label}: {path}\n")
        code = run(cmd, log, headless_env(cpu))
        with open(log, "a", encoding="utf-8") as out:
            out.write(f"== {label}: exit {code}\n")
        if tmp:
            shutil.rmtree(tmp)
    print(summary_line(group))


def summary_line(group):
    log = BSIM / f"{group}.log"
    exes = funcs = skipped = 0
    if log.exists():
        for line in open(log, encoding="utf-8", errors="replace"):
            m = re.search(r"BSimAdd: \S+: (\d+) functions", line)
            if m:
                exes += 1
                funcs += int(m.group(1))
            elif "skipped" in line:
                skipped += 1
    size = db_path(group).stat().st_size if db_path(group).exists() else 0
    return f"{group}: {exes} objects, {funcs} functions, {skipped} skipped, {size / 1e6:.1f} MB"


def game_project():
    """A private copy of the analysed game project (the shared server copies the same source, never opens it)."""
    dst = BSIM / "game"
    if not (dst / "Sims2EP9.rep").exists():
        dst.mkdir(parents=True, exist_ok=True)
        shutil.copy2(LOCAL / "ghidra" / "Sims2EP9.gpr", dst / "Sims2EP9.gpr")
        shutil.copytree(LOCAL / "ghidra" / "Sims2EP9.rep", dst / "Sims2EP9.rep")
    return dst


def query(names, min_size, cpu, rvas=None, out=None, max_matches=3, thresh=0.7):
    dbs = [str(db_path(n)) for n in names if db_path(n).exists()]
    if rvas:
        dbs += ["RVAS", str(Path(rvas).resolve())]
    proj = game_project()
    log = BSIM / "query.log"
    log.write_text("")  # one run per log, so a watcher can wait for its last line
    out = Path(out).resolve() if out else BSIM / "query.jsonl"
    cmd = [str(ghidra_dir() / "support" / "analyzeHeadless.bat"), str(proj), "Sims2EP9", "-process", "-noanalysis",
           "-readOnly", "-max-cpu", str(cpu), "-scriptPath", str(SCRIPTS), "-postScript", "BSimQuery.java",
           str(out), str(min_size), str(max_matches), str(thresh)] + dbs
    env = headless_env(cpu)
    env["GHIDRA_HEADLESS_MAXMEM"] = "16G"
    code = run(cmd, log, env)
    print(f"query exit {code}; {sum(1 for _ in open(out))} functions with matches -> {out}")


# ---- candidate ranking (pure; tested in tools/tests/test_bsim.py) ----

def library_of(exe):
    """'libucrt-10.0.22621.3233/strtod.obj' -> 'libucrt'; 'lua50-O2/lapi.obj' -> 'lua50'; 'stlprobe/x.obj' -> 'stlprobe'."""
    head = exe.split("/", 1)[0]
    return re.sub(r"-(\d[\d.]*|O\w*)$", "", head)


def known_rvas(root=ROOT):
    """Functions already identified as library code: byte matches and structural equivalents."""
    known = set()
    for name in ("db/library-matches.jsonl", "db/crt-equivalents.jsonl"):
        p = root / name
        if p.exists():
            known |= {int(json.loads(l)["rva"], 16) for l in p.read_text().splitlines() if l.strip()}
    return known


def confidence(sim, sig, self_sig):
    """high: near-identical and significant; medium: similar; low: the rest. Significance grows with function
    size, so a near-1 similarity on a tiny function is still weak evidence."""
    if sim >= 0.95 and sig >= 20:
        return "high"
    if sim >= 0.85 and sig >= 10:
        return "medium"
    return "low"


def rank(rows, known, min_sim):
    """rows: parsed query.jsonl lines. Returns candidates (best match per function), largest first, skipping
    known library functions and matches under min_sim."""
    out = []
    for r in rows:
        rva = int(r["rva"], 16)
        if rva in known or not r["matches"]:
            continue
        best = max(r["matches"], key=lambda m: (m["sim"], m["sig"]))
        if best["sim"] < min_sim:
            continue
        out.append({"rva": r["rva"], "size": r["size"], "lib": library_of(best["exe"]), "name": best["name"],
                    "exe": best["exe"], "sim": best["sim"], "sig": best["sig"],
                    "confidence": confidence(best["sim"], best["sig"], r.get("self", 0.0))})
    out.sort(key=lambda c: (-c["size"], c["rva"]))
    return out


def by_library(cands):
    """{lib: [count, bytes]} per confidence level summed."""
    acc = defaultdict(lambda: [0, 0])
    for c in cands:
        acc[c["lib"]][0] += 1
        acc[c["lib"]][1] += c["size"]
    return dict(acc)


def candidates(min_sim, top, as_json, exclude=()):
    rows = [json.loads(l) for l in open(BSIM / "query.jsonl") if l.strip()]
    known = known_rvas()
    for p in exclude:  # more known library code, e.g. libmatch --dry-run results not yet in db/
        known |= {int(json.loads(l)["rva"], 16) for l in Path(p).read_text().splitlines() if l.strip()}
    cands = rank(rows, known, min_sim)
    with open(BSIM / "candidates.jsonl", "w", newline="\n") as f:
        for c in cands:
            f.write(json.dumps(c) + "\n")
    if as_json:
        print(json.dumps(cands[:top]))
        return
    print(f"{len(cands)} candidates (sim >= {min_sim}), {sum(c['size'] for c in cands)} bytes -> local/bsim/candidates.jsonl")
    for conf in ("high", "medium", "low"):
        sub = [c for c in cands if c["confidence"] == conf]
        libs = by_library(sub)
        print(f"  {conf}: {len(sub)} functions, {sum(c['size'] for c in sub)} bytes: "
              + ", ".join(f"{k} {n}/{b}B" for k, (n, b) in sorted(libs.items(), key=lambda kv: -kv[1][1])))
    for c in cands[:top]:
        print(f"{c['rva']:>10} {c['size']:6} {c['sim']:.3f} {c['sig']:6.1f} {c['confidence']:6} {c['lib']:10} {c['name'][:70]}")


def status():
    for g in groups():
        print(summary_line(g))


def main():
    a = sys.argv[1:]
    opt = lambda k, d: type(d)(a[a.index(k) + 1]) if k in a else d  # noqa: E731
    pos = [x for i, x in enumerate(a) if not x.startswith("--") and (i == 0 or not a[i - 1].startswith("--"))]
    if not pos:
        print(__doc__)
    elif pos[0] == "build" and len(pos) == 2:
        build(pos[1], opt("--cpu", 2), opt("--only", ""))
    elif pos[0] == "status":
        status()
    elif pos[0] == "query":
        query(pos[1:] or list(groups()), opt("--min-size", 16), opt("--cpu", 2), opt("--rvas", ""), opt("--out", ""))
    elif pos[0] == "candidates":
        candidates(opt("--min-sim", 0.9), opt("--top", 40), "--json" in a,
                   [a[i + 1] for i, x in enumerate(a) if x == "--exclude"])
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
