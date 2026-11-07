#!/usr/bin/env python3
"""Client for the persistent Ghidra server (tools/ghidra/DecompServer.java): decompile, annotate, export.

  tools/py -I tools/ghidra.py start [--facts F] [--maxmem 6G]   start (or reuse) the server; returns once it answers
  tools/py -I tools/ghidra.py decompile 0x1b5c5b 0x1b5d00         annotated pseudo-C, printed and written to local/decomp-annotated/
        --addr        prefix each line with the RVA of its first instruction (7 hex digits; jump to classinfo --disasm)
        --callees     list the functions it calls, with their current name and db/status status
        --max-lines N stop each function after N lines
        --raw         the analysed project as is (local/decomp/)
        --overlay f.json   try facts (annotate's file format) in a transaction, rolled back afterwards
        --diff        with --overlay: print only the diff of plain vs overlay, per function (decompiler renumbering such as
                      uVar3 -> uVar4 ignored; --full-diff keeps it)
  tools/py -I tools/ghidra.py vars 0x1330                          the decompiler's variables: name, type, storage, first use
  tools/py -I tools/ghidra.py annotate --apply f.json --lane BZ [--tentative|--confirmed] [--dry-run]
        record facts: the same file as --overlay, applied and kept (see docs/lane/understand.md). Prints each entry's
        result, then the diff of the pseudo-code of the affected functions (callers: only hunks naming what changed;
        --full-diff for everything). All or nothing. Symbol names (functions, methods, fields, params, locals, types)
        may hold letters, digits and punctuation such as _:~<>,()&*[] but no spaces or control characters: such a
        file is refused before any server work (--fix-names: replace them with _ and say so).
  tools/py -I tools/ghidra.py annotate --lane BZ --confirm KEY... | --delete KEY... | --rename KEY NEW_NAME
        promote, remove or rename recorded facts by key (0x5890c0, 0x5890c0@-8, SimDataRef, SimDataRef.index, cTSObject::Prim00,
        cTSString:40, equate:0x..., callsite:0x...; `annotate` without arguments lists them). The program follows: a deleted
        fact is undone (the function goes back to the analysed program, then the remaining facts apply again).
  tools/py -I tools/ghidra.py recorded [--lane BZ] [--all]         recorded facts not in db/ yet, each with its key
  tools/py -I tools/ghidra.py facts cTSFamily [--out f.json]        a class's current facts as an annotate file (no server: db/)
  tools/py -I tools/ghidra.py cancel [--id N]                     stop the request the server is running (status names it)
  tools/py -I tools/ghidra.py export [--lane BZ] [--no-snapshot]   write db/annotations.jsonl and db/methods/*.jsonl
  tools/py -I tools/ghidra.py snapshot                             zip the annotated project into local/ghidra-backups/ (last 10 kept)
  tools/py -I tools/ghidra.py reload [--facts F]                   re-derive facts from this checkout's db/ and re-apply (if they changed)
  tools/py -I tools/ghidra.py status | stop                        status also says which request runs and how many wait

The server is shared by every checkout (state in local/ghidra-server/; GHIDRA_SERVER_DIR overrides, for tests). Start-up:
copies local/ghidra (never opened itself), derives facts with tools/annotate.py, and either opens the annotated project saved
by an earlier run with the same facts (about 15 s) or builds it (ApplyDb, about 3 minutes, then saves it). Launches
analyzeHeadless below normal priority with a bounded heap and two CPUs. Exit code 3: no server running.
"""
import argparse
import ctypes
import difflib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import ghidra_ledger as ledger  # noqa: E402
import ifacegen  # noqa: E402


def main_root():
    out = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute", "--git-common-dir"],
                         capture_output=True, text=True)
    return Path(out.stdout.strip()).parent if out.returncode == 0 else ROOT


LOCAL = main_root() / "local"
DIR = Path(os.environ["GHIDRA_SERVER_DIR"]) if os.environ.get("GHIDRA_SERVER_DIR") else LOCAL / "ghidra-server"
BACKUPS = Path(os.environ["GHIDRA_BACKUP_DIR"]) if os.environ.get("GHIDRA_BACKUP_DIR") else LOCAL / "ghidra-backups"
ANN = DIR / "ann"
STATE = DIR / "server.json"
NO_SERVER = 3
KEEP_SNAPSHOTS = 10
# Facts-file sections tools/annotate.py derives (ApplyDb applies them at start-up); an overlay written by hand never needs them.
DERIVED_SECTIONS = ("classes", "impls", "compiled", "members", "missed", "resized")


def state():
    try:
        return json.loads(STATE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


class Cancelled(RuntimeError):
    """The server stopped the request because someone ran `ghidra.py cancel`."""


def request(path, body=None, timeout=3600):
    st = state()
    if not st:
        raise ConnectionError("no server")
    req = urllib.request.Request(f"http://127.0.0.1:{st['port']}{path}", data=json.dumps(body or {}).encode(),
                                 headers={"Content-Type": "application/json"}, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read())
    except urllib.error.HTTPError as e:
        reply = json.loads(e.read() or b"{}")
        raise (Cancelled if reply.get("cancelled") else RuntimeError)(reply.get("error", str(e)))
    except (urllib.error.URLError, ConnectionError, OSError) as e:
        raise ConnectionError(str(e))


def alive(pid):
    k = ctypes.windll.kernel32
    h = k.OpenProcess(0x00100000 | 0x1000, False, pid)  # SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return False
    try:
        return k.WaitForSingleObject(h, 0) == 0x102  # WAIT_TIMEOUT: still running
    finally:
        k.CloseHandle(h)


def running():
    try:
        return request("/status", timeout=10)
    except (ConnectionError, RuntimeError):
        return None


# ---------------------------------------------------------------- facts, ledger and the annotated-project cache

def annotate_proc(ledger_path, out):
    return subprocess.Popen([sys.executable, "-I", str(TOOLS / "annotate.py"), "--ledger", str(ledger_path), "--out", str(out)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def make_facts(ledger_path, out):
    p = annotate_proc(ledger_path, out)
    text = p.communicate()[0]
    if p.returncode != 0:
        raise RuntimeError("tools/annotate.py failed:\n" + text)
    return text


def facts_key(facts):
    """The cache key of an annotated project: the facts, the code that applies them, and the analysed project they apply to."""
    h = hashlib.sha256()
    h.update(Path(facts).read_bytes())
    h.update((TOOLS / "ghidra" / "ApplyDb.java").read_bytes())
    for p in (LOCAL / "ghidra" / "Sims2EP9.gpr", LOCAL / "ghidra" / "Sims2EP9.rep" / "project.prp"):
        try:
            h.update(f"{p.name}:{p.stat().st_size}:{int(p.stat().st_mtime)}".encode())
        except OSError:
            pass
    return h.hexdigest()[:16]


def read_mirror():
    try:
        return json.loads((ANN / "ledger.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def manifest_keys():
    try:
        return json.loads((ANN / "manifest.json").read_text(encoding="utf-8"))["keys"]
    except (OSError, ValueError, KeyError):
        return []


def write_manifest(keys):
    if ANN.exists():
        (ANN / "manifest.json").write_text(json.dumps({"keys": keys[-6:]}), encoding="utf-8")


def start_ledger(path):
    """db/'s hand facts of this checkout, plus the annotations a previous server run recorded that db/ does not contain
    yet (replayed from the mirror of its ledger). Written to path."""
    files = ledger.read_db(ROOT)
    mirror = read_mirror()
    if mirror is None and path.exists():  # a build that died after deleting the old mirror: the last start's ledger has the operations
        try:
            mirror = json.loads(path.read_text(encoding="utf-8"))
        except ValueError:
            pass
    version = 1
    if mirror:
        version = int(mirror.get("version", 0)) + 1
        # migrate_ops: method deletes recorded under the old class+slot key become interface+slot deletes.
        pending = ledger.pending(files, ledger.migrate_ops(files, ledger.ops(mirror["files"])))
        ledger.replay(files, pending)
        files[ledger.OPS] = "".join(json.dumps(op) + "\n" for op in pending)
        if pending:
            print(f"replaying {len(pending)} annotation(s) recorded earlier that db/ does not contain yet")
    path.write_text(json.dumps({"version": version, "files": files}), encoding="utf-8")


def summary_line(text):
    return next((l for l in text.splitlines() if "names," in l), "").strip()


def cmd_start(a):
    s = running()
    if s:
        print(f"server already running: pid {s['pid']}, port {state()['port']}")
        return 0
    DIR.mkdir(parents=True, exist_ok=True)
    lock = DIR / "start.lock"
    if not take_start_lock(lock):
        print(f"another start is in progress ({lock_owner_text(lock)}); waiting for it")
        rc = wait_up(time.time() + 1500, lock)
        if rc == STALE:
            return cmd_start(a)  # its starter and server died while we waited: take over
        return rc
    try:
        STATE.unlink(missing_ok=True)
        clean_old_projects()
        t0 = time.time()
        led = DIR / "ledger-start.json"
        start_ledger(led)
        facts = Path(a.facts) if a.facts else DIR / "facts.json"
        if not a.facts:
            print(summary_line(make_facts(led, facts)))
        key = facts_key(facts)
        mode = "open" if key in manifest_keys() and (ANN / "ann.gpr").exists() and read_mirror() else "build"
        print(f"annotated project: {'cached (facts unchanged), opening' if mode == 'open' else 'building (ApplyDb; ~3 minutes)'}"
              f" [key {key}, facts in {time.time() - t0:.1f} s]")
        proj = DIR / ("proj-" + time.strftime("%Y%m%d-%H%M%S"))
        proj.mkdir()
        shutil.copy2(LOCAL / "ghidra" / "Sims2EP9.gpr", proj)
        shutil.copytree(LOCAL / "ghidra" / "Sims2EP9.rep", proj / "Sims2EP9.rep")
        ghidra = next(LOCAL.joinpath("tools").glob("ghidra_*"))
        env = dict(os.environ, GHIDRA_HEADLESS_MAXMEM=a.maxmem)
        cmd = [str(ghidra / "support" / "analyzeHeadless.bat"), str(proj), "Sims2EP9", "-process", "-noanalysis",
               "-readOnly", "-max-cpu", "2", "-scriptPath", str(TOOLS / "ghidra"), "-postScript", "DecompServer.java",
               str(facts), str(STATE), str(LOCAL / "decomp"), str(LOCAL / "decomp-annotated"), str(LOCAL / "decomp-overlay"),
               str(ANN), mode, str(led), str(LOCAL)]
        log = open(DIR / "server.log", "w", encoding="utf-8")
        flags = 0x00004000 | 0x00000200 | 0x08000000  # BELOW_NORMAL_PRIORITY | NEW_PROCESS_GROUP | NO_WINDOW
        try:
            server = subprocess.Popen(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                      creationflags=flags | 0x01000000)  # BREAKAWAY_FROM_JOB: outlive the calling shell
        except OSError:
            server = subprocess.Popen(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, creationflags=flags)
        write_lock(lock, server.pid)  # a start that finds this lock waits for this server while it lives, even if we die
        t1 = time.time()
        rc = wait_up(t1 + 1500)
        if rc == 0:
            if mode == "build":
                write_manifest([key])
            st = running()
            print(f"started in {time.time() - t1:.1f} s: annotated project {st['annotated']}"
                  + (f", ApplyDb {st['apply_ms'] / 1000:.1f} s" if st["annotated"] == "built" else "")
                  + f", ledger version {st['ledger_version']} (log {DIR / 'server.log'})")
        return rc
    finally:
        lock.unlink(missing_ok=True)


def clean_old_projects(keep=None):
    for d in DIR.glob("proj-*"):
        if d == keep:
            continue
        if any(d.rglob("*.lock*")):
            continue  # open in a Ghidra
        shutil.rmtree(d, ignore_errors=True)


STALE = 4  # wait_up: the start being waited for died (its lock is stale)
OLD_LOCK_S = 600  # a lock without owner information (older client) is trusted this long


def write_lock(lock, server_pid=None):
    lock.write_text(json.dumps({"pid": os.getpid(), "server": server_pid, "t": time.time()}), encoding="utf-8")


def take_start_lock(lock):
    """Creates the start lock; True when we hold it. A lock whose starter and server processes are both gone is stale
    (a caller killed mid-start): it is taken over at once instead of waited on."""
    for _ in range(3):
        try:
            fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.close(fd)
            write_lock(lock)
            return True
        except FileExistsError:
            if not lock_is_stale(lock):
                return False
            print(f"taking over a stale start.lock ({lock_owner_text(lock)})")
            lock.unlink(missing_ok=True)
    return False


def lock_owner(lock):
    try:
        return json.loads(lock.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def lock_owner_text(lock):
    o = lock_owner(lock)
    if not o:
        return "no owner recorded"
    return f"started by pid {o['pid']}" + (f", server pid {o['server']}" if o.get("server") else "")


def lock_is_stale(lock):
    o = lock_owner(lock)
    if o is None:
        try:
            age = time.time() - lock.stat().st_mtime
        except OSError:
            return False  # gone already: not stale, just retry the create
        return age > (OLD_LOCK_S if lock.stat().st_size == 0 else 30)  # empty: an old client; unreadable: being written
    return not alive(o["pid"]) and not (o.get("server") and alive(o["server"]))


def wait_up(deadline, lock=None):
    while time.time() < deadline:
        s = running()
        if s:
            print(f"server up: pid {s['pid']}, port {state()['port']}, heap {s['heap_used_mb']}/{s['heap_max_mb']} MB")
            return 0
        if lock is not None and (not lock.exists() or lock_is_stale(lock)):
            if running():
                continue
            print("the start we waited for is gone (its process died); starting over")
            return STALE
        try:
            log = (DIR / "server.log").read_text(encoding="utf-8", errors="replace")
        except OSError:
            log = ""
        if "SCRIPT ERROR" in log:
            i = log.index("SCRIPT ERROR")
            print("the server script failed:\n" + log[i:i + 600], file=sys.stderr)
            return 1
        time.sleep(1)
    print(f"server did not come up; see {DIR / 'server.log'}", file=sys.stderr)
    return 1


def cmd_stop(a):
    st = state()
    if not st:
        print("no server running")
        return 0
    try:
        request("/shutdown", timeout=30)
    except (ConnectionError, RuntimeError):
        pass
    deadline = time.time() + 60
    while alive(st["pid"]) and time.time() < deadline:
        time.sleep(0.5)
    if alive(st["pid"]):
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(st["pid"])], capture_output=True)
    STATE.unlink(missing_ok=True)
    clean_old_projects()  # the 600 MB project copy
    print(f"stopped (pid {st['pid']})")
    return 0


def describe_request(c):
    return f"request {c['id']}: {c['path']} {c['what']} (running {c['running_s']} s)"


def cmd_status(a):
    s = running()
    if not s:
        print("no server running")
        return NO_SERVER
    cur = s.pop("current", None)
    print(json.dumps(s, indent=1))
    print("busy: " + describe_request(cur) + f", {s['queued']} queued" if cur else "idle")
    return 0


def cmd_cancel(a):
    """Stops the request the server is running (a decompile that takes too long); queued requests then run."""
    try:
        r = request("/cancel", {"id": a.id} if a.id else {}, timeout=60)
    except ConnectionError:
        print("no server running", file=sys.stderr)
        return NO_SERVER
    if "request" not in r:
        print(f"nothing running ({r['queued']} queued)")
        return 0
    what = describe_request(r["request"])
    if not r["cancelled"]:
        print(f"not cancelled: {what}: {r['reason']}")
        return 1
    print(f"cancelled {what}; " + (f"stopped in {r['stop_ms']} ms" if r["stopped"] else f"still stopping after {r['stop_ms']} ms")
          + f"; {r['queued']} queued request(s) continue")
    return 0


def cmd_reload(a):
    led = DIR / "ledger-start.json"
    start_ledger(led)
    facts = Path(a.facts) if a.facts else DIR / "facts.json"
    if not a.facts:
        print(summary_line(make_facts(led, facts)))
    key = facts_key(facts)
    if key in manifest_keys() and not a.force:
        print("the annotated project already has these facts (use --force to rebuild)")
        return 0
    r = request("/reload", {"facts": str(facts), "ledger": str(led)})
    write_manifest([key])
    print("\n".join(l for l in r["log"].splitlines() if "ApplyDb: " in l and ("structures," in l or "elapsed" in l)))
    print(f"reloaded in {r['ms'] / 1000:.1f} s")
    return 0


# ---------------------------------------------------------------- decompile

def status_of():
    try:
        import tsdb
        return tsdb.load_status()
    except Exception:  # noqa: BLE001 - the status column is a courtesy
        return {}


def unified(before, after, label, context=3, limit=None):
    a = before.replace("\r\n", "\n").splitlines()
    b = after.replace("\r\n", "\n").splitlines()
    lines = list(difflib.unified_diff(a, b, f"{label} before", f"{label} after", n=context, lineterm=""))
    if limit and len(lines) > limit:
        lines = lines[:limit] + [f"... ({len(lines) - limit} more diff lines; --max-diff-lines)"]
    return lines


def cmd_decompile(a):
    for x in a.rva:
        if not re.fullmatch(r"0x[0-9a-fA-F]+", x):
            print(f"not an RVA: {x!r} (want 0x...)", file=sys.stderr)
            return 2
    if a.diff and not a.overlay:
        print("--diff needs --overlay", file=sys.stderr)
        return 2
    body = {"rvas": a.rva, "raw": a.raw, "addr": a.addr and not a.diff, "callees": a.callees and not a.diff}
    if a.overlay:
        try:
            sections = json.loads(Path(a.overlay).read_text(encoding="utf-8"))
        except (OSError, ValueError) as e:
            print(f"cannot read the overlay: {e}", file=sys.stderr)
            return 2
        derived = [s for s in sections if s in DERIVED_SECTIONS + ("methods",)]  # methods: only annotate derives them
        if derived:
            print(f"overlay sections {', '.join(derived)} are derived by tools/annotate.py (from db/methods/, db/status, our build), "
                  "not written by hand: write the fact they come from (`annotations`; a `methods` fact with "
                  "`annotate --dry-run`)", file=sys.stderr)
            return 2
    try:
        if a.diff:
            plain = request("/decompile", {"rvas": a.rva})
        if a.overlay:
            body["overlay"] = str(Path(a.overlay).resolve())
        r = request("/decompile", body)
    except ConnectionError:
        print("no server running (tools/py -I tools/ghidra.py start)", file=sys.stderr)
        return NO_SERVER
    except RuntimeError as e:
        print(f"server error: {str(e).splitlines()[0]}", file=sys.stderr)
        return 1
    for line in r.get("log", "").splitlines():
        if "ApplyDb: " in line:
            print(line)
    status = status_of() if a.callees and not a.diff else {}
    for i, res in enumerate(r["results"]):
        if "error" in res:
            print(f"Decompile: no function at {res['rva']}")
        elif a.diff:
            if a.full_diff:
                d = unified(plain["results"][i]["text"], res["text"], res["rva"], a.context)
            else:
                d = smart_diff(plain["results"][i]["text"], res["text"], res["rva"], a.context)[0]
            print("\n".join(d) if d else f"// {res['rva']}: no change with the overlay")
        elif a.files:
            print(f"Decompile: {res['rva']} -> {res['file']}")
        else:
            text = res["text"].replace("\r\n", "\n")
            if a.max_lines:
                lines = text.splitlines()
                if len(lines) > a.max_lines:
                    text = "\n".join(lines[:a.max_lines]) + f"\n// ... {len(lines) - a.max_lines} more lines (--max-lines)\n"
            sys.stdout.write(text if text.endswith("\n") else text + "\n")
            if a.callees:
                print("// callees:")
                for c in res.get("callees", []):
                    st = status.get(c["rva"], {}).get("status", "")
                    print(f"//   {c['rva']} {c['name']}" + (f"  [{st}]" if st else ""))
    print(f"// {len(r['results'])} function(s) in {r['ms']} ms", file=sys.stderr)
    return 0


def cmd_vars(a):
    """The decompiler's variables of a function (annotated program): what a `local` entry can name by `var` or `at`."""
    try:
        r = request("/vars", {"rva": a.rva})
    except ConnectionError:
        print("no server running (tools/py -I tools/ghidra.py start)", file=sys.stderr)
        return NO_SERVER
    except RuntimeError as e:
        print(f"server error: {str(e).splitlines()[0]}", file=sys.stderr)
        return 1
    print(f"// {r['function']} at {r['rva']}: name, type, storage, first use (`at`) or stack offset")
    rows = [(v["name"], v["type"], v["storage"], ("param" if v["param"] else
            (f"at {v['at']}" if "at" in v and "offset" not in v else f"offset {v.get('offset', '?')}"))) for v in r["vars"]]
    w = [max([len(x[i]) for x in rows] + [1]) for i in range(3)]
    for row in rows:
        print(f"{row[0]:<{w[0]}}  {row[1]:<{w[1]}}  {row[2]:<{w[2]}}  {row[3]}")
    return 0


# ---------------------------------------------------------------- annotate

class Lock:
    """Serialises annotate commands across processes (the server serialises the requests themselves)."""

    def __enter__(self):
        DIR.mkdir(parents=True, exist_ok=True)
        self.path = DIR / "annotate.lock"
        deadline = time.time() + 300
        while True:
            try:
                os.close(os.open(self.path, os.O_CREAT | os.O_EXCL | os.O_WRONLY))
                return self
            except FileExistsError:
                if time.time() - self.path.stat().st_mtime > 900:
                    self.path.unlink(missing_ok=True)  # a crashed run
                elif time.time() > deadline:
                    raise RuntimeError("another annotate has been running for 5 minutes; try again")
                time.sleep(0.5)

    def __exit__(self, *exc):
        self.path.unlink(missing_ok=True)


def resolve_locals(entries):
    """A local named by the decompiler's name ("var": iVar3) is recorded by what survives later annotations: its stack
    offset, or the address of its first use ("at"; the decompiler renumbers its names whenever types change). Asks the
    server for the function's variables; returns problems."""
    problems, cache = [], {}
    for file, rec in entries:
        if rec.get("kind") != "local" or "offset" in rec or "at" in rec:
            continue
        rva = rec["rva"]
        if rva not in cache:
            cache[rva] = request("/vars", {"rva": rva})["vars"]
        v = next((v for v in cache[rva] if v["name"] == rec["var"]), None)
        if v is None:
            problems.append(f"local {rva}@{rec['var']}: the decompiler has no such variable now (tools/py -I tools/ghidra.py vars {rva})")
        elif v["param"]:
            problems.append(f"local {rva}@{rec['var']}: a parameter; give the function's signature instead")
        elif "offset" in v:
            rec["offset"] = v["offset"]
            del rec["var"]
        elif "at" in v:
            rec["at"] = v["at"]
            if sum(1 for x in cache[rva] if x.get("at") == v["at"] and not x["param"]) > 1:
                rec["storage"] = v["storage"]
            del rec["var"]
        else:
            problems.append(f"local {rva}@{rec['var']}: no first-use address or stack offset to record it by")
    return problems


def key_ops(a, files):
    """(operations, lines, symbols, problems) for --confirm / --delete / --rename."""
    ops_, lines, symbols, problems = [], [], set(), []
    specs = a.confirm or a.delete or [a.rename[0]]
    for spec in specs:
        found = ledger.find(files, spec)
        if not found:
            problems.append(f"no recorded fact matches {spec!r}")
            continue
        if a.rename and len(found) > 1:
            problems.append(f"{spec!r} matches {len(found)} entries; name one:\n" + "\n".join("  " + ledger.describe_match(*m) for m in found))
            continue
        for m in found:
            symbols.add(ledger.bare_name(m[2]["name"] if m[2] else m[1].get("name", "")))
        if a.confirm:
            new = ledger.confirm_ops(files, found, a.lane)
            for m in found:
                tentative = (m[2] or m[1]).get("tentative") or any(f.get("tentative") for f in m[1].get("fields", []) if m[2] is None)
                lines.append(("confirm" if tentative else "unchanged", ledger.describe_match(*m).replace(" (tentative)", "")))
        elif a.delete:
            new = ledger.delete_ops(files, found, a.lane)
            lines += [("delete", ledger.describe_match(*m)) for m in found]
        else:
            new = ledger.rename_ops(files, found[0], a.rename[1], a.lane)
            symbols.add(a.rename[1])
            lines.append(("rename", ledger.describe_match(*found[0]) + f" -> {a.rename[1]}"))
        ops_ += new
    return ops_, lines, symbols, problems


def cmd_annotate(a):
    if sum(bool(x) for x in (a.apply, a.confirm, a.delete, a.rename)) != 1:
        print("give one of --apply FILE, --confirm KEY..., --delete KEY..., --rename KEY NEW_NAME\n" + ledger.KEY_HELP, file=sys.stderr)
        return 2
    if a.rename and ledger.ILLEGAL_SYMBOL_CHAR.search(a.rename[1]):
        print(f"problem: {a.rename[1]!r}: Ghidra symbol names can't hold spaces or control characters; use "
              f"{ledger.legal_symbol(a.rename[1])!r}\nnothing recorded", file=sys.stderr)
        return 2
    entries = []
    if a.apply:
        overlay = json.loads(Path(a.apply).read_text(encoding="utf-8"))
        entries, problems = ledger.convert_overlay(overlay, a.lane, not a.confirmed)
        name_out = ledger.check_symbol_names(entries, fix=a.fix_names)  # before the server: Ghidra refuses these late
        if a.fix_names:
            for p in name_out:
                print("note:", p, file=sys.stderr)
        else:
            problems += name_out
        for p in problems:
            print("problem:", p, file=sys.stderr)
        if problems or not entries:
            print("nothing recorded" if problems else "the file has no facts", file=sys.stderr)
            return 2
    t_start = time.time()
    with Lock():
        try:
            cur = request("/ledger")
        except ConnectionError:
            print("no server running (tools/py -I tools/ghidra.py start)", file=sys.stderr)
            return NO_SERVER
        old_files = cur["files"]
        new_files = dict(old_files)
        replaced, renames, key_lines, symbols = {}, [], [], set()
        if a.apply:
            try:
                problems = resolve_locals(entries)
            except RuntimeError as e:
                problems = [f"looking up the function's variables: {str(e).splitlines()[0]}"]
            if problems:
                print("\n".join("problem: " + p for p in problems) + "\nnothing recorded", file=sys.stderr)
                return 2
            for file, rec in entries:
                old = ledger.find_existing(new_files, file, rec)
                if old is not None and rec.get("kind") != "fields" and \
                        {k: v for k, v in old.items() if k not in ("lane", "tentative", "source")} != \
                        {k: v for k, v in rec.items() if k not in ("lane", "tentative", "source")}:
                    replaced[id(rec)] = old
                ledger.upsert(new_files, file, json.loads(json.dumps(rec)))
                ledger.add_op(new_files, a.lane, file, rec)
                symbols |= entry_symbols(rec)
        else:
            ops_, key_lines, symbols, problems = key_ops(a, old_files)
            if problems:
                print("\n".join("problem: " + p for p in problems) + "\nnothing recorded\n" + ledger.KEY_HELP, file=sys.stderr)
                return 2
            for op in ops_:
                ledger.replay(new_files, [op])
                ledger.add_raw_op(new_files, op)
            renames = [op["rename_type"] for op in ops_ if "rename_type" in op]
        refused = ifacegen.refusals(old_files, new_files, entries, ifacegen.load_classes(ROOT / "db"))
        if refused:  # what `tools/ifacegen.py --check` would reject: the generated headers could not be built
            print("\n".join("refused: " + p for p in refused) + "\nnothing recorded", file=sys.stderr)
            return 2
        if {k: v for k, v in new_files.items() if k != ledger.OPS} == {k: v for k, v in old_files.items() if k != ledger.OPS}:
            print("no change: the ledger already holds these facts")
            return 0
        scratch = DIR / "annotate"
        scratch.mkdir(exist_ok=True)
        paths = {}
        for tag, files in (("old", old_files), ("new", new_files)):
            paths[tag] = scratch / f"ledger-{tag}.json"
            paths[tag].write_text(json.dumps({"files": files}), encoding="utf-8")
        procs = {tag: annotate_proc(paths[tag], scratch / f"facts-{tag}.json") for tag in paths}
        out = {tag: p.communicate()[0] for tag, p in procs.items()}
        for tag, p in procs.items():
            if p.returncode != 0:
                print(f"tools/annotate.py failed on the {tag} ledger:\n{out[tag]}", file=sys.stderr)
                return 1
        derive_s = time.time() - t_start
        new_problems = [l for l in out["new"].splitlines() if l.startswith("problem:") and l not in out["old"]]
        if new_problems and not a.force:
            print("the facts conflict with db/'s other facts (tools/annotate.py --check); nothing recorded (--force to record anyway):")
            print("\n".join(new_problems))
            return 1
        old_facts = json.loads((scratch / "facts-old.json").read_text(encoding="utf-8"))
        new_facts = json.loads((scratch / "facts-new.json").read_text(encoding="utf-8"))
        delta, revert = ledger.facts_changes(old_facts, new_facts, renames)
        delta_path = scratch / "delta.json"
        delta_path.write_text(json.dumps(delta), encoding="utf-8")
        new_ledger = {"version": cur["version"] + 1, "files": new_files}
        watch = list(a.watch) + method_functions(entries)
        body = {"overlay": str(delta_path), "revert": revert, "ledger": new_ledger, "base_version": cur["version"],
                "commit": not a.dry_run, "watch": watch, "callers": a.callers, "max": a.max}
        try:
            r = request("/annotate", body)
        except Cancelled:
            print("cancelled (tools/ghidra.py cancel): nothing recorded or changed", file=sys.stderr)
            return 1
        except RuntimeError as e:
            print(f"server error: {str(e).splitlines()[0]}", file=sys.stderr)
            return 1
        unapplied = [l.split("ApplyDb: unapplied ", 1)[1] for l in r["log"].splitlines() if "ApplyDb: unapplied " in l]
        mode = "confirmed" if a.confirmed else "tentative unless an entry says otherwise"
        if a.apply:
            print(f"annotate lane {a.lane} ({mode}): {len(entries)} entr{'y' if len(entries) == 1 else 'ies'}")
            for file, rec in entries:
                what = ledger.describe(file, rec)
                bad = [u for u in unapplied if any(t and t in u for t in (rec.get("rva"), rec.get("name") if "rva" not in rec else None))]
                flag = "unapplied" if bad else ("applied" if r["applied"] else "rolled back")
                old = replaced.get(id(rec))
                note = ""
                if old is not None:
                    note = f"  [replaces {old.get('name', '')} by lane {old.get('lane', 'db/')}{', tentative' if old.get('tentative') else ''}]"
                print(f"  {flag:<10}{what}" + (f": {bad[0]}" if bad else "") + ("  (tentative)" if rec.get("tentative") else "") + note)
        else:
            print(f"annotate lane {a.lane}: {len(key_lines)} entr{'y' if len(key_lines) == 1 else 'ies'}")
            for verb, what in key_lines:
                print(f"  {(verb if r['applied'] else 'rolled back'):<10}{what}")
        for l in r["log"].splitlines():
            if "DecompServer: revert" in l:
                print("  note: " + l.split("DecompServer: ", 1)[1])
        if not r["applied"]:
            print(f"NOTHING recorded or changed: {len(unapplied)} fact(s) could not be applied:")
            for u in unapplied:
                print("  " + u)
            return 1
        if a.dry_run:
            print("dry run: applied, shown and rolled back (nothing recorded)")
        else:
            append_key(new_facts_key(scratch / "facts-new.json"))
            print(f"recorded: ledger version {r['version']} (derive {derive_s:.1f} s, apply {r.get('apply_ms', 0)} ms, save {r.get('save_ms', 0)} ms);"
                  f" `export --lane {a.lane}` writes it to db/")
        if r.get("after_error"):
            print(f"(the pseudo-code after the change could not be produced: {r['after_error'][:200]}; the facts are recorded)")
        print_diffs(r["before"], r["after"], r.get("roles", {}), symbols, a)
    return 0


def entry_symbols(rec):
    """Names an annotate entry introduces or changes: what a caller's diff is filtered by."""
    out = set()
    for k in ("name", "class", "this"):
        if isinstance(rec.get(k), str):
            out.add(ledger.bare_name(rec[k].split("::")[-1].rstrip("*")))
    for f in rec.get("fields", []):
        out.add(ledger.bare_name(f["name"]))
    out |= set(rec.get("values", {}))
    if rec.get("kind") == "equate" and rec.get("enum"):
        out.add(rec["enum"])
    return {s for s in out if s}


def print_diffs(before_all, after_all, roles, symbols, a):
    """Diffs of the affected functions: decompiler-local renames (uVar3 -> uVar4) normalised away; for callers and users
    of a changed type, only the hunks that mention a changed name (--full-diff: plain unified diffs of everything)."""
    shown, hidden = 0, 0
    budget = a.max_diff_lines
    for rva, before in before_all.items():
        after = after_all.get(rva, before)
        role = roles.get(rva, "named")
        if a.full_diff:
            d = unified(before, after, rva, a.context)
        else:
            d, dropped = smart_diff(before, after, f"{rva} ({role})", a.context, symbols if role != "named" else None)
            hidden += dropped
        if not d:
            continue
        shown += 1
        if budget <= 0:
            print(f"--- {rva}: changed ({len(d)} diff lines; over the --max-diff-lines budget; decompile it to see)")
            continue
        cut = min(len(d), budget)
        print("\n".join(d[:cut]) + (f"\n... ({len(d) - cut} more diff lines for {rva})" if cut < len(d) else ""))
        budget -= cut
    checked = len(before_all)
    if not shown:
        print(f"// pseudo-code of the {checked} checked function(s) is unchanged"
              + ("" if a.full_diff else " (decompiler renumbering ignored)") + f": {', '.join(before_all)}"
              + ("" if before_all else " (none affected; pass --watch 0x... to look at others)"))
    else:
        print(f"// {shown} of {checked} checked function(s) changed")
    if hidden:
        print(f"// {hidden} hunk(s) of callers/users without a changed name hidden (--full-diff shows everything)")


DECOMP_LOCAL = re.compile(r"\b(?:([a-z]{1,6}Var)\d+|(local_)[0-9a-f]+|([a-z]{0,6}Stack_)[0-9a-f]+|(in_|unaff_|extraout_)\w+"
                          r"|(LAB_)[0-9a-f]+|(param_)\d+)\b")


def normalise(line):
    """A pseudo-code line with the decompiler's own numbering removed: uVar3 -> uVar#, local_1c -> local_#."""
    return DECOMP_LOCAL.sub(lambda m: next(g for g in m.groups() if g) + "#", line)


def smart_diff(before, after, label, context=2, symbols=None):
    """(unified diff lines, hunks dropped): hunks only where lines differ after normalise(); printed with the real text.
    With symbols, hunks whose changed lines mention none of them are dropped (counted)."""
    a = before.replace("\r\n", "\n").splitlines()
    b = after.replace("\r\n", "\n").splitlines()
    sm = difflib.SequenceMatcher(None, [normalise(x) for x in a], [normalise(x) for x in b], autojunk=False)
    out, dropped = [], 0
    word = re.compile(r"\b(" + "|".join(re.escape(s) for s in sorted(symbols, key=len, reverse=True)) + r")\b") if symbols else None
    for group in sm.get_grouped_opcodes(context):
        hunk, changed = [], []
        i1, i2, j1, j2 = group[0][1], group[-1][2], group[0][3], group[-1][4]
        hunk.append(f"@@ -{i1 + 1},{i2 - i1} +{j1 + 1},{j2 - j1} @@")
        for tag, x1, x2, y1, y2 in group:
            if tag == "equal":
                hunk += [" " + l for l in b[y1:y2]]
                continue
            hunk += ["-" + l for l in a[x1:x2]] + ["+" + l for l in b[y1:y2]]
            changed += a[x1:x2] + b[y1:y2]
        if word is not None and not any(word.search(l) for l in changed):
            dropped += 1
            continue
        out += hunk
    if out:
        out = [f"--- {label} before", f"+++ {label} after"] + out
    return out, dropped


def method_functions(entries):
    """RVAs of the functions behind the vtable slots that method facts in the entries name (their pseudo-code is worth a look)."""
    wanted = {(r["class"], r.get("vtable", 0), r["slot"]) for f, r in entries if f != "annotations.jsonl" and r.get("class") and "slot" in r}
    if not wanted:
        return []
    out = []
    for line in (ROOT / "db" / "classes.jsonl").read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        c = json.loads(line)
        for t in c["vftables"]:
            for cls, off, slot in wanted:
                if cls == c["name"] and off == t["offset"] and slot < len(t["entries"]):
                    out.append(t["entries"][slot])
    return out


def new_facts_key(path):
    return facts_key(path)


def append_key(key):
    keys = manifest_keys()
    if key not in keys:
        write_manifest(keys + [key])


# ---------------------------------------------------------------- export, snapshot

def current_ledger():
    try:
        return request("/ledger")
    except ConnectionError:
        mirror = read_mirror()
        if mirror:
            print("(no server: using the mirror of the last saved ledger)", file=sys.stderr)
            return mirror
        raise


def cmd_recorded(a):
    """The facts recorded through annotate (by lane), each with the key that --confirm/--delete/--rename take."""
    try:
        files = current_ledger()["files"]
    except ConnectionError:
        print("no server running and no saved ledger", file=sys.stderr)
        return NO_SERVER
    base = ledger.read_db(ROOT)
    shown = 0
    for file in sorted(f for f in files if f != ledger.OPS):
        have = set(ledger.lines_of(base.get(file, "")))
        for line in ledger.lines_of(files[file]):
            if not line.strip() or (line in have and not a.all):
                continue
            rec = json.loads(line)
            if a.lane and rec.get("lane") != a.lane:
                continue
            print(f"{ledger.entry_key(file, rec):<28} {ledger.describe_match(file, rec, None)}")
            shown += 1
    print(f"// {shown} entr{'y' if shown == 1 else 'ies'}" + ("" if a.all else " not in this checkout's db/ yet (--all: every entry)"))
    return 0


def cmd_facts(a):
    """A class's current facts as an annotate file (edit it and --apply it back)."""
    try:
        files, where = current_ledger()["files"], "the server's ledger"
    except ConnectionError:
        files, where = ledger.read_db(ROOT), "this checkout's db/ (no server)"
    overlay = ledger.class_facts(files, a.cls)
    text = ledger.dump_overlay(overlay) if overlay else "{}\n"
    n = len(overlay.get("annotations", [])) + sum(len(v) for v in overlay.get("methods", {}).values())
    if a.out:
        Path(a.out).write_text(text, encoding="utf-8")
        print(f"{a.cls}: {n} fact(s) from {where} -> {a.out}")
    else:
        sys.stdout.write(text)
        print(f"// {a.cls}: {n} fact(s) from {where}", file=sys.stderr)
    return 0


def cmd_export(a):
    try:
        cur = current_ledger()
    except ConnectionError:
        print("no server running and no saved ledger (tools/py -I tools/ghidra.py start)", file=sys.stderr)
        return NO_SERVER
    files = cur["files"]
    if a.lane:
        # Replay this lane's operations on the checkout's own files: the branch carries exactly its change.
        mine = [op for op in ledger.ops(files) if op["lane"] == a.lane]
        target = ledger.read_db(ROOT)
        ledger.replay(target, mine)
        changed = ledger.write_files(ROOT, target)
        print(f"exported lane {a.lane}: {len(mine)} operation(s) replayed on this checkout's db/")
    else:
        changed = ledger.write_files(ROOT, files)
        print("exported the whole ledger (all lanes' recorded annotations)")
    print("changed: " + (", ".join("db/" + c for c in changed) if changed else "nothing (db/ already matches)"))
    if not a.no_snapshot:
        return cmd_snapshot(a)
    return 0


def cmd_snapshot(a):
    BACKUPS.mkdir(parents=True, exist_ok=True)
    zip_path = BACKUPS / (time.strftime("%Y%m%d-%H%M%S") + ".zip")
    try:
        r = request("/snapshot", {"zip": str(zip_path)})
    except ConnectionError:
        print("no server running: no snapshot", file=sys.stderr)
        return NO_SERVER
    old = sorted(BACKUPS.glob("*.zip"))
    for p in old[:-KEEP_SNAPSHOTS]:
        p.unlink(missing_ok=True)
    print(f"snapshot {zip_path} ({r['bytes'] >> 20} MB, {r['files']} files, {r['ms'] / 1000:.1f} s); kept {min(len(old), KEEP_SNAPSHOTS)}")
    return 0


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("start")
    s.add_argument("--facts", help="facts file to apply (default: derive from this checkout's db/)")
    s.add_argument("--maxmem", default="6G", help="Java heap bound")
    sub.add_parser("stop")
    sub.add_parser("status")
    c = sub.add_parser("cancel")
    c.add_argument("--id", type=int, help="cancel only if this request (from `status`) is the one running")
    v = sub.add_parser("vars")
    v.add_argument("rva")
    r = sub.add_parser("reload")
    r.add_argument("--facts")
    r.add_argument("--force", action="store_true")
    d = sub.add_parser("decompile")
    d.add_argument("rva", nargs="+")
    d.add_argument("--raw", action="store_true", help="the analysed project without db/'s facts")
    d.add_argument("--overlay", help="JSON facts applied for this request only")
    d.add_argument("--diff", action="store_true", help="with --overlay: only the unified diff of plain vs overlay")
    d.add_argument("--full-diff", action="store_true", help="with --diff: keep the decompiler's renumbering (uVar3 -> uVar4) in the diff")
    d.add_argument("--addr", action="store_true", help="prefix lines with the RVA of their first instruction")
    d.add_argument("--callees", action="store_true", help="list called functions with name and status")
    d.add_argument("--max-lines", type=int, help="stop each function after N lines")
    d.add_argument("--context", type=int, default=3, help="diff context lines")
    d.add_argument("--files", action="store_true", help="print only the written files")
    n = sub.add_parser("annotate")
    n.add_argument("--apply", metavar="FILE", help="facts file (the --overlay format, plus a methods section)")
    n.add_argument("--confirm", nargs="+", metavar="KEY", help="promote recorded tentative facts to confirmed (keys below)")
    n.add_argument("--delete", nargs="+", metavar="KEY", help="remove recorded facts (the program goes back to what remains)")
    n.add_argument("--rename", nargs=2, metavar=("KEY", "NEW_NAME"), help="rename one fact: a field, function, local, method; "
                   "a structure, union or enum with every use of it")
    n.add_argument("--lane", required=True, help="your lane letter(s); recorded with every fact")
    n.add_argument("--full-diff", action="store_true", help="plain unified diffs (default: decompiler renumbering ignored, "
                   "callers show only hunks naming what changed)")
    g = n.add_mutually_exclusive_group()
    g.add_argument("--tentative", action="store_true", help="facts are guesses (the default; an entry can say otherwise)")
    g.add_argument("--confirmed", action="store_true", help="facts are established (an entry can say tentative: true)")
    n.add_argument("--dry-run", action="store_true", help="apply, show the diff, roll back; record nothing")
    n.add_argument("--fix-names", action="store_true", help="replace spaces/control characters in symbol names with _ "
                   "(noted) instead of refusing the file")
    n.add_argument("--force", action="store_true", help="record even if tools/annotate.py reports new conflicts")
    n.add_argument("--watch", nargs="*", default=[], help="more RVAs whose pseudo-code to diff")
    n.add_argument("--callers", type=int, default=3, help="callers of a changed function to diff (each)")
    n.add_argument("--max", type=int, default=8, help="functions to diff at most")
    n.add_argument("--context", type=int, default=2)
    n.add_argument("--max-diff-lines", type=int, default=100, help="in total, over all functions")
    rr = sub.add_parser("recorded")
    rr.add_argument("--lane", help="only this lane's entries")
    rr.add_argument("--all", action="store_true", help="also the entries this checkout's db/ already has")
    fc = sub.add_parser("facts", help="a class's current facts as an annotate file")
    fc.add_argument("cls", metavar="CLASS")
    fc.add_argument("--out", help="write it here (default: stdout)")
    e = sub.add_parser("export")
    e.add_argument("--lane", help="only this lane's annotations, replayed on this checkout's db/ (default: the whole ledger)")
    e.add_argument("--no-snapshot", action="store_true")
    sub.add_parser("snapshot")
    a = p.parse_args()
    return {"start": cmd_start, "stop": cmd_stop, "status": cmd_status, "cancel": cmd_cancel, "vars": cmd_vars,
            "reload": cmd_reload, "decompile": cmd_decompile,
            "annotate": cmd_annotate, "recorded": cmd_recorded, "facts": cmd_facts, "export": cmd_export, "snapshot": cmd_snapshot}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
