#!/usr/bin/env python3
"""User-data snapshots that cost only their changed files: every file of a snapshot is a hardlink to an object in a
content-addressed store (local/snapstore), so files a run left as they were (most of the ~1 GB) are stored once for
all snapshots, fixtures and instances.

    tools/py -I tools/snapstore.py snap SRC DEST        # new folder DEST = SRC's tree, files linked into the store
    tools/py -I tools/snapstore.py adopt DIR...         # existing snapshot/fixture folders: each file becomes a link
    tools/py -I tools/snapstore.py gc [--dry-run]       # delete objects no snapshot links any more
    tools/py -I tools/snapstore.py verify               # re-hash every object: was one written through a link?
    tools/py -I tools/snapstore.py stats                # objects, bytes, links

scenario.ps1's snap: step and lotcheck.py record use snap; smoke.py runs gc after pruning old batches.

An object is named by its content (sha256) and its modification time, and keeps both: a snapshot reads exactly as
a robocopy copy did (contents and modification times; lotcheck's lot_resaved compares times), and a file the
game rewrote with the same bytes keeps its own time. Readers (savediff, lotcheck, robocopy from a snapshot used as
-Baseline) see ordinary files.

Why a store and not links to the run's baseline: baselines are edited in place (tools/setfunds.py; a copy into an
existing folder may overwrite files where they are), and a write through a link changes every other link, so a
snapshot linked to its baseline would change with it; snapshots used as baselines (reload chains) are pruned
while snapshots made from them live on. Nothing writes into the store except this tool, and a snapshot is written
once, here, and then only read.

The rule that keeps this safe: never write into a snapshot or fixture in place. A tool that edits a file in place
calls break_link(path) first (setfunds.py does), which gives the file its own copy. `verify` finds an object whose
content or time no longer matches its name (written through a link). Objects are not
made read-only: the attribute is shared by every link and copied by robocopy and shutil, so a snapshot used as
-Baseline would make the game's live user data read-only.
"""
import argparse
import hashlib
import os
import shutil
import threading
import sys
import time
import uuid
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

STORE = tsdb.main_root() / "local" / "snapstore"
CHUNK = 1 << 20
THREADS = 8
MAX_LINKS = 1000          # NTFS allows 1023 links per file; past this a second copy of the object starts
ERROR_TOO_MANY_LINKS = 1142
ERROR_NOT_SAME_DEVICE = 17
ERROR_SHARING_VIOLATION = 32
ERROR_LOCK_VIOLATION = 33


def _retry(fn, *args, wait=30.0):
    """fn(*args), retried while the file is locked by a process that is still closing (the game after a forced
    stop: robocopy used /R:10 /W:2 for the same reason)."""
    deadline = time.time() + wait
    while True:
        try:
            return fn(*args)
        except PermissionError as e:
            if getattr(e, "winerror", None) not in (ERROR_SHARING_VIOLATION, ERROR_LOCK_VIOLATION) or time.time() > deadline:
                raise
            time.sleep(0.5)


def _hash_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(CHUNK)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def object_name(digest, mtime_ns):
    return f"{digest}-{mtime_ns:x}"


def parse_name(name):
    """(digest, mtime_ns) of an object file name ("<sha256>-<mtime hex>[.n]"), or None."""
    base = name.split(".")[0]
    digest, _, mt = base.partition("-")
    if len(digest) != 64 or not mt:
        return None
    try:
        return digest, int(mt, 16)
    except ValueError:
        return None


class Store:
    def __init__(self, root=None):
        self.root = Path(root or STORE)
        self.objects = self.root / "objects"
        self.tmp = self.root / "tmp"
        self.linked = self.copied = 0          # files linked to an existing object / copied in as a new one
        self.linked_bytes = self.copied_bytes = 0
        self.no_links = False                  # store on another volume: plain copies
        self._lock = threading.Lock()

    def _count(self, linked, size):
        with self._lock:
            if linked:
                self.linked += 1
                self.linked_bytes += size
            else:
                self.copied += 1
                self.copied_bytes += size

    def path_of(self, name, n=0):
        return self.objects / name[:2] / (name if n == 0 else f"{name}.{n}")

    def _new_object(self, src, name, n, mtime_ns):
        """Copies src into the store as object `name` (variant n); returns (its path, whether this call made it).
        Another process or thread may create the same object meanwhile: then theirs is used."""
        obj = self.path_of(name, n)
        obj.parent.mkdir(parents=True, exist_ok=True)
        self.tmp.mkdir(parents=True, exist_ok=True)
        tmp = self.tmp / f"{os.getpid()}-{uuid.uuid4().hex}"     # unique across processes and threads
        h = hashlib.sha256()
        with open(src, "rb") as fi, open(tmp, "wb") as fo:
            while True:
                b = fi.read(CHUNK)
                if not b:
                    break
                h.update(b)
                fo.write(b)
        shutil.copystat(src, tmp)
        actual = object_name(h.hexdigest(), os.stat(tmp).st_mtime_ns)
        if actual != name:          # src changed since it was hashed: store what was copied, under its own name
            obj = self.path_of(actual, n)
            obj.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.rename(tmp, obj)
        except FileExistsError:
            os.unlink(tmp)
            return obj, False
        return obj, True

    def link(self, src, dest):
        """dest (absent) becomes a link to src's object, created from src if the store lacks it."""
        st = os.stat(src)
        mtime_ns, size = st.st_mtime_ns, st.st_size
        name = object_name(_retry(_hash_file, src), mtime_ns)
        if self.no_links:
            shutil.copy2(src, dest)
            self._count(False, size)
            return
        for n in range(1000):
            obj = self.path_of(name, n)
            created = False
            if not obj.exists():
                obj, created = _retry(self._new_object, src, name, n, mtime_ns)
            elif os.stat(obj).st_nlink >= MAX_LINKS:
                continue
            try:
                os.link(obj, dest)
            except FileNotFoundError:
                if not os.path.exists(os.path.dirname(dest)):
                    raise
                return self.link(src, dest)            # gc removed it meanwhile: start again
            except OSError as e:
                if getattr(e, "winerror", None) == ERROR_TOO_MANY_LINKS:
                    continue
                if getattr(e, "winerror", None) == ERROR_NOT_SAME_DEVICE:
                    print(f"warning: {self.root} is on another volume than {dest}: plain copies", file=sys.stderr)
                    self.no_links = True
                    shutil.copy2(src, dest)
                    self._count(False, size)
                    return
                raise
            self._count(not created, size)
            return
        raise OSError(f"no usable object variant for {name}")

    def snap(self, src, dest):
        """A new folder dest holding src's tree (empty folders too), every file a link into the store."""
        src, dest = Path(src), Path(dest)
        if dest.exists():
            raise FileExistsError(f"{dest} exists: a snapshot is written once")
        if not src.is_dir():
            raise NotADirectoryError(src)
        dest.mkdir(parents=True)
        pairs = []
        for d, dirs, files in os.walk(src):
            rel = Path(d).relative_to(src)
            for sub in dirs:
                (dest / rel / sub).mkdir()
            pairs += [(Path(d) / f, dest / rel / f) for f in files]
        # Threads: hashing and file I/O release the GIL (a warm 1 GB snapshot: 7 s on one thread, ~2 s on 8).
        with ThreadPoolExecutor(max_workers=THREADS) as pool:
            for _ in pool.map(lambda p: self.link(*p), pairs):
                pass

    def is_object(self, path):
        """Is path (a file) one of the store's objects (same file id as the object its content names)?"""
        st = os.stat(path)
        if st.st_nlink < 2:
            return False
        name = object_name(_hash_file(path), st.st_mtime_ns)
        for n in range(1000):
            obj = self.path_of(name, n)
            if not obj.exists():
                return False
            if os.path.samefile(obj, path):
                return True
        return False

    def adopt(self, folder):
        """Turns each file of an existing folder into a link to its object. A file no other path links becomes
        the object itself (no copy); others are replaced through a temporary link and an atomic rename."""
        paths = [Path(d) / f for d, _, files in os.walk(folder) for f in files if not f.endswith(".snapstore-tmp")]
        with ThreadPoolExecutor(max_workers=THREADS) as pool:
            for _ in pool.map(self._adopt_file, paths):
                pass

    def _adopt_file(self, p):
        st = os.stat(p)
        if st.st_nlink > 1 and self.is_object(p):
            self._count(True, st.st_size)
            return
        name = object_name(_hash_file(p), st.st_mtime_ns)
        if st.st_nlink == 1 and not self.path_of(name).exists():
            obj = self.path_of(name)
            obj.parent.mkdir(parents=True, exist_ok=True)
            try:
                os.link(p, obj)
                self._count(False, st.st_size)
                return
            except FileExistsError:
                pass
        tmp = p.with_name(p.name + ".snapstore-tmp")
        self.link(p, tmp)
        os.replace(tmp, p)

    def iter_objects(self):
        if not self.objects.is_dir():
            return
        for sub in sorted(self.objects.iterdir()):
            for o in sorted(sub.iterdir()):
                yield o

    def gc(self, dry_run=False):
        """Deletes objects with no link but their own; returns (count, bytes). Run it when no snapshot is being
        written (smoke.py runs it after a batch)."""
        n = size = 0
        for o in self.iter_objects():
            st = os.stat(o)
            if st.st_nlink == 1:
                n += 1
                size += st.st_size
                if not dry_run:
                    o.unlink()
        if self.tmp.is_dir() and not dry_run:
            for t in self.tmp.iterdir():   # left by an interrupted copy
                if time.time() - t.stat().st_mtime > 3600:
                    t.unlink()
        return n, size

    def verify(self):
        """Objects whose content or time no longer matches their name: [(object, why)]."""
        bad = []
        for o in self.iter_objects():
            parsed = parse_name(o.name)
            if not parsed:
                bad.append((o, "not an object name"))
                continue
            digest, mtime_ns = parsed
            st = os.stat(o)
            if _hash_file(o) != digest:
                bad.append((o, "content changed (written through a link)"))
            elif st.st_mtime_ns != mtime_ns:
                bad.append((o, "modification time changed"))
        return bad


def break_link(path):
    """Gives a hardlinked file its own copy before it is edited in place, so the edit doesn't reach the other links
    (snapshots sharing a store object). No-op for a file with one link."""
    path = Path(path)
    if os.stat(path).st_nlink <= 1:
        return False
    tmp = path.with_name(path.name + ".unlink-tmp")
    shutil.copy2(path, tmp)
    os.replace(tmp, path)
    return True


def adopt_refusal(folder, store_root):
    """Why a folder must not become store links (something writes into it in place), or None. Live user data
    (the game writes it), baselines (edited in place, robocopy targets), game copies and the store itself."""
    p = Path(os.path.abspath(folder))
    if p == Path(store_root) or Path(store_root) in p.parents:
        return "is inside the store"
    if "ea games" in [x.lower() for x in p.parts]:
        return "is the game's user data"
    local = tsdb.main_root() / "local"
    if local in p.parents or p == local:
        top = p.relative_to(local).parts[0].lower() if p != local else ""
        if not top or top.startswith(("game", "userdata", "baselines")) or top == "original":
            return f"is local/{top} (written in place)" if top else "is all of local/"
    return None


def gb(n):
    return f"{n / 2**30:.2f} GB"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--store", default=str(STORE), help="store folder (default local/snapstore of the main checkout)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("snap", help="copy SRC to a new folder DEST, files linked into the store")
    s.add_argument("src")
    s.add_argument("dest")
    s = sub.add_parser("adopt", help="turn existing folders' files into store links")
    s.add_argument("dirs", nargs="+")
    s.add_argument("--force", action="store_true", help="also folders the tool refuses (baselines, live user data)")
    s = sub.add_parser("gc", help="delete objects nothing links")
    s.add_argument("--dry-run", action="store_true")
    sub.add_parser("verify", help="re-hash every object")
    sub.add_parser("stats", help="objects, bytes, links")
    a = ap.parse_args(argv)
    st = Store(a.store)
    t0 = time.time()
    if a.cmd == "snap":
        st.snap(a.src, a.dest)
        print(f"snapshot {a.dest}: {st.linked} files linked to stored objects ({gb(st.linked_bytes)}), "
              f"{st.copied} new ({gb(st.copied_bytes)}) in {time.time() - t0:.1f} s")
    elif a.cmd == "adopt":
        for d in a.dirs:
            if not os.path.isdir(d):
                sys.exit(f"{d} is not a folder")
            why = adopt_refusal(d, st.root)
            if why and not a.force:
                sys.exit(f"refused: {d} {why} (--force if you are sure nothing writes into it in place)")
        for d in a.dirs:
            st.adopt(d)
        print(f"adopted {len(a.dirs)} folders: {st.linked} files now share an object ({gb(st.linked_bytes)} freed), "
              f"{st.copied} became objects ({gb(st.copied_bytes)}) in {time.time() - t0:.1f} s")
    elif a.cmd == "gc":
        n, size = st.gc(a.dry_run)
        print(f"{'would delete' if a.dry_run else 'deleted'} {n} unlinked objects ({gb(size)})")
    elif a.cmd == "verify":
        bad = st.verify()
        for o, why in bad:
            print(f"BAD {o}: {why}")
        print(f"{'FAIL' if bad else 'ok'}: {len(bad)} bad objects")
        return 1 if bad else 0
    elif a.cmd == "stats":
        n = size = links = logical = 0
        for o in st.iter_objects():
            s_ = os.stat(o)
            n += 1
            size += s_.st_size
            links += s_.st_nlink - 1
            logical += s_.st_size * (s_.st_nlink - 1)
        print(f"{st.root}: {n} objects, {gb(size)}; {links} links, which as plain copies would take {gb(logical)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
