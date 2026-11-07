"""Read files from a git revision without a process per file, for tools/guard.py and the checks it runs.

show(rev, path) answers like `git show rev:path` read through subprocess in text mode as UTF-8: same text, universal
newlines and all, and the same subprocess.CalledProcessError for a path that isn't there. The guard reads every status
file twice over (some 2,000 `git show` calls, about 90 s of a run); here one long-lived `git cat-file --batch` serves
them, and a repeated read at a full commit id (which can't change) is answered from memory.

The batch process holds its working directory until this process exits, which on Windows keeps a temporary repository
from being deleted: tools whose tests build one in-process keep their plain `git show`.

A plain name cat-file reports missing raises what `git show` would (status 128). Anything else it can't answer plainly
goes to the real `git show`, so that command stays the definition: an ambiguous name, a tree (it lists), a path with .
or .. or a line break, a batch process that died or spoke out of turn.
"""
import atexit
import os
import re
import subprocess

_batches = {}  # working directory -> running `git cat-file --batch`
_read = {}  # (cwd, rev, path) -> bytes, or the CalledProcessError it raised; rev a full id
FULL_ID = re.compile(r"[0-9a-f]{40}")


def _batch(cwd):
    p = _batches.get(cwd)
    if p is not None and p.poll() is not None:
        _drop(cwd)
        p = None
    if p is None:
        p = subprocess.Popen(["git", "cat-file", "--batch"], cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL)
        _batches[cwd] = p
    return p


def _drop(cwd):
    p = _batches.pop(cwd, None)
    if p is not None:
        try:
            p.stdin.close()
            p.stdout.close()
            p.wait(timeout=5)
        except (OSError, ValueError, subprocess.TimeoutExpired):
            p.kill()


def _close_all():
    for cwd in list(_batches):
        _drop(cwd)


atexit.register(_close_all)


def _from_batch(name, cwd):
    """The blob's bytes, the CalledProcessError of a missing name, or None when `git show` has to answer."""
    if "\n" in name or "\r" in name:
        return None
    try:
        p = _batch(cwd)
        p.stdin.write(name.encode("utf-8") + b"\n")
        p.stdin.flush()
        header = p.stdout.readline().rstrip(b"\n").split(b" ")
        if len(header) == 3 and header[1] in (b"blob", b"tree", b"commit", b"tag"):
            size = int(header[2])
            data = p.stdout.read(size + 1)  # the object and its line break
            if len(data) != size + 1 or data[-1:] != b"\n":
                raise ValueError("short read")
            return data[:-1] if header[1] == b"blob" else None
        if header[-1:] == [b"missing"]:
            # `git show` tidies a path with . or .. in it and may yet find it, so that one is its to answer.
            plain = all(part not in ("", ".", "..") for part in name.split(":", 1)[-1].split("/"))
            return subprocess.CalledProcessError(128, ["git", "show", name]) if plain else None
        if header[-1:] == [b"ambiguous"]:
            return None
        raise ValueError(f"unexpected reply {header!r}")
    except (OSError, ValueError):
        _drop(cwd)
        return None


def _bytes(rev, path, cwd):
    key = (cwd, rev, path)
    if key in _read:
        return _read[key]
    name = f"{rev}:{path}"
    data = _from_batch(name, cwd)
    if data is None:
        try:
            data = subprocess.run(["git", "show", name], cwd=cwd, capture_output=True, check=True).stdout
        except subprocess.CalledProcessError as ex:
            data = ex
    if FULL_ID.fullmatch(rev):
        _read[key] = data
    return data


def show(rev, path, cwd=None, errors="strict"):
    """`git show rev:path` as text (the module docstring); `cwd` is where git runs, `errors` as in bytes.decode."""
    data = _bytes(rev, path, str(cwd or os.getcwd()))
    if isinstance(data, Exception):
        raise data
    return data.decode("utf-8", errors).replace("\r\n", "\n").replace("\r", "\n")
