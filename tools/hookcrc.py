"""Checks every hook entry's site CRC against the unpacked exe the way the host does (src/hook/hooks.cpp SiteCrc),
including entries hookspec.py couldn't produce (functions missing from db/functions.jsonl, CRCs computed by hand).

    tools/py -I tools/hookcrc.py [table.cpp ...]   (default: this checkout's src/hook/tables/*.cpp)

An entry is {"name", rva, checkLen, crc, ...}: the host compares zlib crc32 of the checkLen bytes at rva (on-disk bytes,
relocations undone) with crc, and skips the hook when they differ ("site at rva 0x... doesn't match"). checkLen is the
number of bytes the CRC covers, not the function's size: hookspec.py writes min(size, 32) and the CRC of that many bytes.
Lane DW's hand-written cTSPersistResKeyList_Create row said 64 (the function's size as it was known) with the CRC of
32 bytes, so the host skipped it in every run while this tool, which then took min(checkLen, 32), passed it (lane LP).
The host reads at most 64 bytes (a longer check never matches). Each bad entry prints the CRC the host would accept.
Exits 1 if any entry mismatches, hooks fewer than 5 bytes or checks more than 64. status.py --check runs the same check.
"""
import re
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsdb  # noqa: E402

ENTRY = re.compile(r'\{\s*"([^"]+)",\s*0x([0-9a-fA-F]+),\s*(\d+),\s*0x([0-9a-fA-F]+)')
MAX_CHECK = 64  # SiteCrc's buffer: a longer check returns 0


def site_crc(image, rva, n):
    """The CRC the host computes for a check of n bytes at rva (image: a pefile.PE of the unpacked exe)."""
    return zlib.crc32(image.get_data(rva, n)) & 0xFFFFFFFF


def check_text(path, text, image):
    """(entries, [problem]) for the hook rows in one table file's text."""
    problems, entries = [], 0
    for m in ENTRY.finditer(text):
        name, rva, n, crc = m[1], int(m[2], 16), int(m[3]), int(m[4], 16)
        entries += 1
        where = f"{path}: hook {name} 0x{rva:x}"
        if n < 5:
            problems.append(f"{where}: checkLen {n}: under 5 bytes (the hook's jump is 5)")
        elif n > MAX_CHECK:
            problems.append(f"{where}: checkLen {n}: the host checks at most {MAX_CHECK} bytes, so it never installs "
                            f"(use min(size, 32): 32 and 0x{site_crc(image, rva, 32):08x})")
        else:
            actual = site_crc(image, rva, n)
            if actual != crc:
                short = f"; or 32 and 0x{site_crc(image, rva, 32):08x}" if n > 32 else ""
                problems.append(f"{where}: crc 0x{crc:08x} of its {n} bytes, the exe's is 0x{actual:08x}{short}: the host "
                                "skips it (\"site ... doesn't match\")")
    return entries, problems


def check_tables(files=None, image=None):
    """(entries, [problem]) over the hook tables (default: this checkout's src/hook/tables/*.cpp)."""
    if image is None:
        import pefile
        image = pefile.PE(data=tsdb.unpacked_exe().read_bytes(), fast_load=True)
    paths = [Path(p) for p in files] if files else sorted((tsdb.ROOT / "src/hook/tables").glob("*.cpp"))
    total, problems = 0, []
    for p in paths:
        try:
            shown = p.resolve().relative_to(tsdb.ROOT).as_posix()
        except ValueError:
            shown = str(p)
        n, bad = check_text(shown, p.read_text(encoding="utf-8"), image)
        total += n
        problems += bad
    return total, problems


def main():
    entries, problems = check_tables(sys.argv[1:] or None)
    for p in problems:
        print(p)
    print(f"{entries} entries, {len(problems)} bad")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
