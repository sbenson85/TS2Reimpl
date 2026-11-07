"""One lock per checkout for the work that rebuilds or deploys its build/ tree: mutation runs, in-game validation,
and anything else that calls Invoke-Build (deploy, difftest, manual builds) while one of those runs.

The lock is build/checkout.lock (each worktree has its own build/, so lanes don't contend with main). It records the
owner's Windows process id, what it is doing, when it started and a token. Children of the owner (mutate.py's difftest
builds, validate.sh's deploy) inherit TS2_CHECKOUT_LOCK=<token> and pass the check. A lock whose owner process is gone
is stale: `acquire` and `check` say so and take it over or ignore it.

  checkout_lock.py acquire --what "mutate X.json" [--pid N]   prints the token; exit 1 if a live owner holds it
  checkout_lock.py release --token T                           removes the lock if T owns it
  checkout_lock.py check                                       exit 0 if free, stale, or held by $TS2_CHECKOUT_LOCK
  checkout_lock.py show                                        prints the lock, if any

Python callers use `held(what)` (a context manager).
"""
import argparse
import contextlib
import ctypes
import datetime
import json
import os
import pathlib
import sys
import uuid

ROOT = pathlib.Path(__file__).resolve().parent.parent
LOCK = ROOT / "build" / "checkout.lock"
ENV = "TS2_CHECKOUT_LOCK"


def alive(pid):
    """Whether a Windows process with this id is still running."""
    if not pid:
        return False
    kernel = ctypes.windll.kernel32
    handle = kernel.OpenProcess(0x1000, False, int(pid))  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    try:
        code = ctypes.c_ulong()
        if not kernel.GetExitCodeProcess(handle, ctypes.byref(code)):
            return False
        return code.value == 259  # STILL_ACTIVE
    finally:
        kernel.CloseHandle(handle)


def read():
    try:
        return json.loads(LOCK.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def describe(lock):
    return f"pid {lock.get('pid')} ({lock.get('what')}, since {lock.get('started')})"


def acquire(what, pid=None):
    """Takes the lock and returns its token; raises RuntimeError naming the live owner if it is held."""
    pid = int(pid or os.getpid())
    current = read()
    if current and current.get("token") != os.environ.get(ENV):
        if alive(current.get("pid")):
            raise RuntimeError(f"checkout busy: {describe(current)}; wait for it (or, if it is truly gone, delete {LOCK})")
        print(f"checkout_lock: stale lock from {describe(current)} taken over", file=sys.stderr)
    elif current:
        return current["token"]  # nested: our own owner already holds it
    token = uuid.uuid4().hex
    LOCK.parent.mkdir(exist_ok=True)
    tmp = LOCK.with_suffix(".tmp")
    tmp.write_text(json.dumps({"pid": pid, "what": what, "token": token,
                               "started": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}), encoding="utf-8")
    os.replace(tmp, LOCK)
    again = read()
    if not again or again.get("token") != token:
        raise RuntimeError(f"checkout busy: lost the race to {describe(again or {})}")
    return token


def release(token):
    current = read()
    if current and current.get("token") == token:
        LOCK.unlink(missing_ok=True)


def check():
    """None if the caller may build; else the owner's description."""
    current = read()
    if not current or current.get("token") == os.environ.get(ENV) or not alive(current.get("pid")):
        return None
    return describe(current)


@contextlib.contextmanager
def held(what):
    """Holds the lock for the block (nested use under the same owner is a no-op) and exports the token to children."""
    nested = bool(read() and read().get("token") == os.environ.get(ENV))
    token = acquire(what)
    previous = os.environ.get(ENV)
    os.environ[ENV] = token
    try:
        yield token
    finally:
        if previous is None:
            os.environ.pop(ENV, None)
        else:
            os.environ[ENV] = previous
        if not nested:
            release(token)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("acquire")
    a.add_argument("--what", required=True)
    a.add_argument("--pid", type=int)
    r = sub.add_parser("release")
    r.add_argument("--token", required=True)
    sub.add_parser("check")
    sub.add_parser("show")
    args = ap.parse_args()
    if args.cmd == "acquire":
        try:
            print(acquire(args.what, args.pid))
        except RuntimeError as e:
            print(e, file=sys.stderr)
            return 1
    elif args.cmd == "release":
        release(args.token)
    elif args.cmd == "check":
        owner = check()
        if owner:
            print(f"checkout busy: {owner}", file=sys.stderr)
            return 1
    elif args.cmd == "show":
        current = read()
        print("free" if not current else describe(current) + ("" if alive(current.get("pid")) else " [stale]"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
