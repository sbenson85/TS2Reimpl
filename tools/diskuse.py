#!/usr/bin/env python3
"""What local/ and .claude/worktrees use on disk, counting a hardlinked file once (by its file id).

    tools/py -I tools/diskuse.py                     # free space, totals, the biggest items two levels down
    tools/py -I tools/diskuse.py --depth 3 --top 30  # finer items (local/traces/<batch>/<run>)
    tools/py -I tools/diskuse.py --files 20          # also the 20 biggest files
    tools/py -I tools/diskuse.py local/traces        # other folders (relative to the main checkout)
    tools/py -I tools/diskuse.py --json out.json     # the same as JSON

Per item: "size" counts each file id once within the item (what the item holds), "own" counts the file ids
none of whose links (among the scanned folders) lie outside the item: roughly what deleting it would free.
Links outside the scanned folders are not seen, so "own" can overstate for files also linked from elsewhere.
Links from local/snapstore (tools/snapstore.py) don't count against a snapshot's "own" (deleting the snapshot and
running `snapstore.py gc` frees them); the store's own "own" is what gc would free now.
"logical" is the plain sum of file sizes (each link counted), as Explorer shows it; the difference is what
hardlinks (game-N, snapshot store) save. Reparse points (junctions, symlinks) are not followed.
"""
import argparse
import json
import os
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

FILE_ATTRIBUTE_DIRECTORY = 0x10
FILE_ATTRIBUTE_REPARSE_POINT = 0x400

if os.name == "nt":
    import ctypes
    from ctypes import wintypes

    _k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _k32.CreateFileW.restype = wintypes.HANDLE
    _k32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                                 wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    _k32.GetFileInformationByHandleEx.argtypes = [wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID, wintypes.DWORD]
    _k32.GetFileInformationByHandleEx.restype = wintypes.BOOL
    _k32.CloseHandle.argtypes = [wintypes.HANDLE]
    _INVALID = wintypes.HANDLE(-1).value
    _BUF = 256 * 1024

    def list_dir(path):
        """(name, is_dir, is_reparse, size, file_id) for each entry of a directory, from one handle
        (FileIdBothDirectoryInfo): no per-file open, so ids of 500k files come in seconds."""
        h = _k32.CreateFileW("\\\\?\\" + os.path.abspath(path), 0x1, 0x7, None, 3, 0x02000000, None)
        if h == _INVALID or h is None:
            raise OSError(ctypes.get_last_error(), "cannot open directory", path)
        buf = ctypes.create_string_buffer(_BUF)
        out = []
        try:
            cls = 11  # FileIdBothDirectoryRestartInfo, then FileIdBothDirectoryInfo
            while True:
                if not _k32.GetFileInformationByHandleEx(h, cls, buf, _BUF):
                    err = ctypes.get_last_error()
                    if err == 18:  # ERROR_NO_MORE_FILES
                        break
                    raise OSError(err, "cannot list directory", path)
                cls = 10
                raw = buf.raw
                off = 0
                while True:
                    nxt = struct.unpack_from("<I", raw, off)[0]
                    size = struct.unpack_from("<q", raw, off + 40)[0]
                    attrs, nlen = struct.unpack_from("<II", raw, off + 56)
                    fid = struct.unpack_from("<Q", raw, off + 96)[0]
                    name = raw[off + 104:off + 104 + nlen].decode("utf-16-le")
                    if name not in (".", ".."):
                        out.append((name, bool(attrs & FILE_ATTRIBUTE_DIRECTORY),
                                    bool(attrs & FILE_ATTRIBUTE_REPARSE_POINT), size, fid))
                    if not nxt:
                        break
                    off += nxt
        finally:
            _k32.CloseHandle(h)
        return out
else:
    def list_dir(path):
        out = []
        for e in os.scandir(path):
            st = e.stat(follow_symlinks=False)
            out.append((e.name, e.is_dir(follow_symlinks=False), e.is_symlink(), st.st_size, st.st_ino))
        return out


def scan(root):
    """Yields (relative parts, size, file id key) for every file under root (reparse points skipped)."""
    drive = os.path.splitdrive(os.path.abspath(root))[0].upper()
    stack = [(root, ())]
    while stack:
        d, parts = stack.pop()
        try:
            entries = list_dir(d)
        except OSError as e:
            print(f"warning: {e}", file=sys.stderr)
            continue
        for name, is_dir, is_reparse, size, fid in entries:
            if is_reparse:
                continue
            if is_dir:
                stack.append((os.path.join(d, name), parts + (name,)))
            else:
                yield parts + (name,), size, (drive, fid)


def is_store(item):
    """Items inside a snapstore folder (tools/snapstore.py): their links don't keep a snapshot's files alive."""
    return "snapstore" in item.lower().split("/")


class Usage:
    """Sizes per item (a folder `depth` levels below a root, or a file above that depth)."""

    def __init__(self, depth):
        self.depth = depth
        self.items = {}       # item -> {id: size}
        self.where = {}       # id -> set of items holding a link
        self.logical = {}     # item -> plain sum of sizes
        self.files = {}       # id -> (size, first path)

    def add_root(self, label, root):
        for parts, size, fid in scan(root):
            item = label + "/" + "/".join(parts[:self.depth])
            self.items.setdefault(item, {})[fid] = size
            self.where.setdefault(fid, set()).add(item)
            self.logical[item] = self.logical.get(item, 0) + size
            self.files.setdefault(fid, (size, label + "/" + "/".join(parts)))

    def report(self):
        rows = []
        for item, ids in self.items.items():
            size = sum(ids.values())
            if is_store(item):   # the store's own: objects nothing else links (what snapstore.py gc frees)
                own = sum(s for i, s in ids.items() if len(self.where[i]) == 1)
            else:                # a snapshot's: files no other item links, besides the store (gc frees them after)
                own = sum(s for i, s in ids.items() if len([w for w in self.where[i] if not is_store(w)]) == 1)
            rows.append({"item": item, "size": size, "own": own, "logical": self.logical[item], "files": len(ids)})
        return rows


def gb(n):
    return f"{n / 2**30:8.2f} GB"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("paths", nargs="*", help="folders to scan (default: local and .claude/worktrees of the main checkout)")
    ap.add_argument("--depth", type=int, default=2, help="item depth below each folder (default 2)")
    ap.add_argument("--top", type=int, default=25, help="items listed (default 25)")
    ap.add_argument("--files", type=int, default=0, help="also list the N biggest files")
    ap.add_argument("--json", help="write the report here")
    a = ap.parse_args(argv)

    main_root = tsdb.main_root()
    paths = a.paths or ["local", ".claude/worktrees"]
    roots = []
    for p in paths:
        full = Path(p) if Path(p).is_absolute() else main_root / p
        if full.is_dir():
            roots.append((p.replace("\\", "/").rstrip("/"), str(full)))
        else:
            print(f"warning: {full} is not a folder", file=sys.stderr)
    u = Usage(max(1, a.depth))
    for label, full in roots:
        u.add_root(label, full)
    rows = u.report()
    total = sum(s for s, _ in u.files.values())
    logical = sum(r["logical"] for r in rows)
    free = shutil.disk_usage(str(main_root))
    per_root = {}
    for label, _ in roots:
        ids = {}
        for item, d in u.items.items():
            if item.startswith(label + "/"):
                ids.update(d)
        per_root[label] = sum(ids.values())

    print(f"free {gb(free.free)} of {gb(free.total)} on {os.path.splitdrive(str(main_root))[0] or '/'}")
    for label, size in per_root.items():
        print(f"{label:40} {gb(size)}")
    print(f"{'all scanned (each file id once)':40} {gb(total)}   logical {gb(logical).strip()}, "
          f"hardlinks save {gb(logical - total).strip()}; {len(u.files)} distinct files")
    print(f"\nbiggest items (depth {a.depth}): size / own (frees if deleted) / logical")
    for r in sorted(rows, key=lambda r: -r["own"])[:a.top]:
        print(f"{gb(r['size'])} {gb(r['own'])} {gb(r['logical'])}  {r['item']}")
    big = sorted(u.files.values(), key=lambda f: -f[0])[:a.files]
    if big:
        print(f"\nbiggest files:")
        for size, path in big:
            print(f"{gb(size)}  {path}")
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump({"free": free.free, "disk": free.total, "total": total, "logical": logical, "roots": per_root,
                       "items": sorted(rows, key=lambda r: -r["own"]),
                       "files": [{"size": s, "path": p} for s, p in big]}, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
