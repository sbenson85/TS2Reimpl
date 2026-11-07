#!/usr/bin/env python3
"""Tests for the Ghidra decompile server (tools/ghidra.py, tools/ghidra/DecompServer.java). Run directly:

  tools/py -I tools/tests/test_ghidra_server.py [--keep]

Works on a server of its own in local/ghidra-server-test/ (GHIDRA_SERVER_DIR when set; never the shared one): uses it
when running, or starts it (the first start builds the annotated project, ~3-4 minutes; later ones reuse it) and stops
it at the end (the stop test), unless --keep. Skips when local/ghidra or Ghidra is absent. Decompiler output is only
compared in memory and written to local/; nothing is committed. Prints timings.
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path


def _main_local():
    out = subprocess.run(["git", "-C", str(Path(__file__).resolve().parents[2]), "rev-parse", "--path-format=absolute",
                          "--git-common-dir"], capture_output=True, text=True)
    return Path(out.stdout.strip()).parent / "local"


os.environ.setdefault("GHIDRA_SERVER_DIR", str(_main_local() / "ghidra-server-test"))
os.environ.setdefault("GHIDRA_BACKUP_DIR", str(Path(os.environ["GHIDRA_SERVER_DIR"]) / "backups"))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ghidra as client  # noqa: E402

RVA = "0x5890c0"     # a mid-sized function; renamed by the overlay test
CALLER_RVA = "0x1b5c5b"
SLOW_RVA = "0x58bc50"  # GenericSimCall, ~52 KB once resized: its decompile takes minutes (the cancel test)
failures = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def decompile(rva, **kw):
    body = {"rvas": [rva], **kw}
    r = client.request("/decompile", body)
    return r["results"][0]["text"], r["ms"]


def body_only(text):
    return text.replace("\r\n", "\n").split("\n", 1)[1]  # drop our header line; the reply keeps the decompiler's CRLFs


def test_matches_headless():
    # The server's raw instance is the analysed project as headless opens it: same text as tools/decompile.ps1.
    srv, _ = decompile(RVA, raw=True)
    out = Path(tempfile.mkdtemp(prefix="ghs-", dir=client.LOCAL / "scratch"))
    ghidra = next(client.LOCAL.joinpath("tools").glob("ghidra_*"))
    t0 = time.time()
    subprocess.run([str(ghidra / "support" / "analyzeHeadless.bat"), str(client.LOCAL / "ghidra"), "Sims2EP9", "-process",
                    "-noanalysis", "-readOnly", "-max-cpu", "2", "-scriptPath", str(client.TOOLS / "ghidra"),
                    "-postScript", "Decompile.java", str(out), RVA], capture_output=True,
                   env=dict(__import__("os").environ, GHIDRA_HEADLESS_MAXMEM="4G"), creationflags=0x00004000)
    print(f"     headless decompile of one function: {time.time() - t0:.1f} s")
    head = (out / f"{RVA}.c").read_text(encoding="utf-8")
    check(body_only(head) == body_only(srv), "raw server decompile identical to the headless path")
    # Structure of the annotated output: same function, a body, and db/'s names applied.
    ann, _ = decompile(RVA)
    check(ann.count("{") == ann.count("}") and ann.count("{") > 0, "annotated output is a balanced function body")


def test_overlay_rollback():
    before, _ = decompile(CALLER_RVA)
    target_before, _ = decompile(RVA)
    overlay = {
        "functions": [{"rva": RVA, "name": "t_OverlayProbeName"}],
        "annotations": [{"kind": "struct", "name": "t_OverlayProbeStruct", "size": 8,
                         "fields": [{"off": 4, "type": "i32", "name": "probeField"}]}],
    }
    f = client.LOCAL / "scratch" / "ghs-overlay.json"
    f.write_text(json.dumps(overlay), encoding="utf-8")
    t0 = time.time()
    r = client.request("/decompile", {"rvas": [RVA, CALLER_RVA], "overlay": str(f)})
    print(f"     overlay round trip (apply, 2 decompiles, roll back): {(time.time() - t0) * 1000:.0f} ms")
    with_target, with_caller = (x["text"] for x in r["results"])
    check("t_OverlayProbeName" in with_target, "overlay rename shows in the function")
    check("t_OverlayProbeName" in with_caller or RVA not in before, "overlay rename shows in a caller")
    after, _ = decompile(CALLER_RVA)
    target_after, _ = decompile(RVA)
    check(after == before and target_after == target_before, "after the overlay, plain decompiles are identical to before")
    check("t_OverlayProbe" not in after + target_after, "no trace of the overlay names")
    # A struct the overlay created must be gone too: an overlay retyping a local with it now fails to resolve it.
    f.write_text(json.dumps({"globals": [{"rva": "0x0", "type": "t_OverlayProbeStruct"}]}), encoding="utf-8")
    r = client.request("/decompile", {"rvas": [RVA], "overlay": str(f)})
    check("unknown type t_OverlayProbeStruct" in r.get("log", ""), "overlay structure rolled back (unknown afterwards)")


def test_concurrent():
    results, errors = {}, []

    def worker(i):
        try:
            results[i] = decompile(RVA if i % 2 else CALLER_RVA)[0]
        except Exception as e:  # noqa: BLE001
            errors.append(repr(e))

    t0 = time.time()
    threads = [threading.Thread(target=worker, args=(i,)) for i in range(6)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    print(f"     6 concurrent clients: {time.time() - t0:.2f} s")
    check(not errors and len(results) == 6, "6 concurrent clients all answered" + (f": {errors}" if errors else ""))
    check(len({results[i] for i in range(0, 6, 2)}) == 1 and len({results[i] for i in range(1, 6, 2)}) == 1,
          "concurrent answers consistent")


def test_cancel():
    """A slow decompile is cancelled; the request queued behind it still runs; the decompiler works afterwards."""
    out = {}

    def slow():
        t = time.time()
        try:
            out["slow"] = decompile(SLOW_RVA)[0]
        except client.Cancelled as e:
            out["slow"] = e
        out["slow_s"] = time.time() - t

    def queued():
        try:
            out["queued"] = decompile(RVA)[0]
        except Exception as e:  # noqa: BLE001
            out["queued"] = e

    a = threading.Thread(target=slow)
    a.start()
    deadline = time.time() + 30
    while time.time() < deadline and not (client.running() or {}).get("current"):
        time.sleep(0.2)
    st = client.running()
    check(st is not None and st.get("current", {}).get("what", "").find(SLOW_RVA) >= 0,
          "status answers while a decompile runs, and names it")
    b = threading.Thread(target=queued)
    b.start()
    time.sleep(3)  # let the slow one get into the decompiler, and the second one queue
    st = client.running()
    still = st and st.get("current", {}).get("what", "").find(SLOW_RVA) >= 0
    if not still:
        print("     (the slow decompile finished within 3 s: nothing to cancel; the test machine is fast or the function small)")
    check(still and st["queued"] == 1, "the second request is queued behind the slow one")
    t0 = time.time()
    rc, text = run_cli("cancel")
    print(f"     {text.strip()}")
    a.join(60)
    b.join(120)
    print(f"     cancel latency (client view): {(time.time() - t0) * 1000:.0f} ms; slow request ended after {out.get('slow_s', -1):.1f} s")
    check(rc == 0 and isinstance(out.get("slow"), client.Cancelled), "the slow request fails with Cancelled")
    check(isinstance(out.get("queued"), str) and "{" in out["queued"], "the queued request ran after the cancel")
    check(len(decompile(CALLER_RVA)[0]) > 100, "the decompiler works after the cancel (new process)")
    rc, text = run_cli("cancel")
    check(rc == 0 and "nothing running" in text, "cancel with nothing running says so")


def run_cli(*argv):
    import contextlib
    import io
    buf = io.StringIO()
    old = sys.argv
    sys.argv = ["ghidra.py", *argv]
    try:
        with contextlib.redirect_stdout(buf):
            rc = client.main()
    finally:
        sys.argv = old
    return rc, buf.getvalue()


def test_stale_lock():
    """A start.lock whose starter died is taken over at once; a live owner's lock is not."""
    lock = client.DIR / "start.lock"
    p = subprocess.Popen([sys.executable, "-c", "pass"])
    p.wait()
    lock.write_text(json.dumps({"pid": p.pid, "server": None, "t": time.time()}), encoding="utf-8")
    t0 = time.time()
    took = client.take_start_lock(lock)
    check(took and time.time() - t0 < 2 and client.lock_owner(lock)["pid"] == os.getpid(), "a dead starter's lock is taken over at once")
    lock.unlink()
    lock.write_text(json.dumps({"pid": os.getpid(), "server": None, "t": time.time()}), encoding="utf-8")
    check(not client.take_start_lock(lock), "a live starter's lock is respected")
    lock.write_text(json.dumps({"pid": p.pid, "server": os.getpid(), "t": time.time()}), encoding="utf-8")
    check(not client.lock_is_stale(lock), "a lock whose server process still lives is not stale")
    lock.unlink()


def test_warm_timing():
    times = [decompile(RVA)[1] for _ in range(5)]
    print(f"     warm decompile of {RVA}: {sorted(times)[2]} ms median (server time)")


def test_stop(pid):
    client.cmd_stop(None)
    time.sleep(2)
    check(not client.alive(pid), "stop leaves no server process")
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          f"Get-CimInstance Win32_Process -Filter 'ParentProcessId={pid}' | Measure-Object | % Count"],
                         capture_output=True, text=True)
    check(out.stdout.strip() in ("", "0"), "no decompiler child left")


def main():
    if not (client.LOCAL / "ghidra" / "Sims2EP9.gpr").exists() or not list(client.LOCAL.joinpath("tools").glob("ghidra_*")):
        print("SKIP: local/ghidra or Ghidra not present")
        return 0
    (client.LOCAL / "scratch").mkdir(exist_ok=True)
    client.DIR.mkdir(parents=True, exist_ok=True)
    started = False
    if not client.running():
        test_stale_lock()
        # A start that finds a stale lock (its starter was killed) takes it over instead of waiting 10 minutes.
        p = subprocess.Popen([sys.executable, "-c", "pass"])
        p.wait()
        (client.DIR / "start.lock").write_text(json.dumps({"pid": p.pid, "server": None, "t": time.time()}), encoding="utf-8")
        t0 = time.time()
        rc, out = run_cli("start")
        print(out)
        check(rc == 0 and "taking over a stale start.lock" in out, "start takes over a stale start.lock")
        if rc != 0:
            print("FAIL server did not start")
            return 1
        print(f"     start-up: {time.time() - t0:.1f} s")
        started = True
    pid = client.running()["pid"]
    test_matches_headless()
    test_overlay_rollback()
    test_concurrent()
    test_cancel()
    test_warm_timing()
    if started and "--keep" not in sys.argv:
        test_stop(pid)
    print("FAILED: " + "; ".join(failures) if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
